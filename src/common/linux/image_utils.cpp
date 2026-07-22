#include "common/linux/image_utils.h"

#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace nativekit::platform {
namespace {

constexpr std::size_t kMaximumDataUrlLength = 32 * 1024 * 1024;
constexpr std::uint64_t kMaximumDecodedBytes = 64 * 1024 * 1024;
constexpr int kMaximumImageDimension = 8192;

template <typename Type>
struct GObjectDeleter {
  void operator()(Type* value) const {
    if (value != nullptr) g_object_unref(value);
  }
};

using AppInfoPtr = std::unique_ptr<GAppInfo, GObjectDeleter<GAppInfo>>;
using FileInfoPtr = std::unique_ptr<GFileInfo, GObjectDeleter<GFileInfo>>;
using FilePtr = std::unique_ptr<GFile, GObjectDeleter<GFile>>;
using IconInfoPtr = std::unique_ptr<GtkIconInfo, GObjectDeleter<GtkIconInfo>>;
using PixbufPtr = std::unique_ptr<GdkPixbuf, GObjectDeleter<GdkPixbuf>>;
using PixbufLoaderPtr =
    std::unique_ptr<GdkPixbufLoader, GObjectDeleter<GdkPixbufLoader>>;

struct GErrorDeleter {
  void operator()(GError* error) const {
    if (error != nullptr) g_error_free(error);
  }
};

using ErrorPtr = std::unique_ptr<GError, GErrorDeleter>;

std::runtime_error gtk_error(const char* operation, GError* error) {
  return std::runtime_error(
      std::string(operation) + " failed" +
      (error == nullptr ? "" : std::string(": ") + error->message));
}

std::string lower_ascii(std::string value) {
  std::transform(
      value.begin(),
      value.end(),
      value.begin(),
      [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
  return value;
}

bool valid_base64(const std::string& value) {
  if (value.empty() || value.size() % 4 != 0) return false;
  const std::size_t padding_start = value.find('=');
  if (padding_start != std::string::npos) {
    const std::size_t padding = value.size() - padding_start;
    if (padding > 2 ||
        value.find_first_not_of('=', padding_start) != std::string::npos) {
      return false;
    }
  }
  const std::size_t content_size =
      padding_start == std::string::npos ? value.size() : padding_start;
  for (std::size_t index = 0; index < content_size; ++index) {
    const unsigned char character = value[index];
    if (!(std::isalnum(character) || character == '+' || character == '/')) {
      return false;
    }
  }
  return true;
}

struct PreparedImageSize {
  bool invalid = false;
};

void validate_prepared_size(
    GdkPixbufLoader* loader,
    int width,
    int height,
    gpointer user_data) {
  auto& state = *static_cast<PreparedImageSize*>(user_data);
  const std::uint64_t pixel_count =
      width > 0 && height > 0
          ? static_cast<std::uint64_t>(width) * height
          : 0;
  state.invalid = width <= 0 || height <= 0 ||
                  width > kMaximumImageDimension ||
                  height > kMaximumImageDimension ||
                  pixel_count > kMaximumDecodedBytes / 4;
  if (state.invalid) {
    // Prevent the loader from allocating the source image after its header has
    // exposed an unsafe size. The caller still rejects the image.
    gdk_pixbuf_loader_set_size(loader, 1, 1);
  }
}

PixbufPtr exact_square_pixbuf(GdkPixbuf* source, int pixels) {
  if (source == nullptr || pixels <= 0) return {};
  const int source_width = gdk_pixbuf_get_width(source);
  const int source_height = gdk_pixbuf_get_height(source);
  if (source_width <= 0 || source_height <= 0) return {};

  const double scale = std::min(
      static_cast<double>(pixels) / source_width,
      static_cast<double>(pixels) / source_height);
  const int width = std::clamp(
      static_cast<int>(std::lround(source_width * scale)), 1, pixels);
  const int height = std::clamp(
      static_cast<int>(std::lround(source_height * scale)), 1, pixels);
  PixbufPtr scaled(
      gdk_pixbuf_scale_simple(source, width, height, GDK_INTERP_BILINEAR));
  if (!scaled) return {};

  PixbufPtr result(gdk_pixbuf_new(
      GDK_COLORSPACE_RGB, TRUE, 8, pixels, pixels));
  if (!result) return {};
  gdk_pixbuf_fill(result.get(), 0x00000000);
  gdk_pixbuf_copy_area(
      scaled.get(),
      0,
      0,
      width,
      height,
      result.get(),
      (pixels - width) / 2,
      (pixels - height) / 2);
  return result;
}

std::filesystem::path canonical_path(const std::string& value) {
  std::error_code error;
  const auto path = std::filesystem::path(value);
  auto result = std::filesystem::weakly_canonical(path, error);
  return error ? path.lexically_normal() : result;
}

bool executable_matches(
    const std::filesystem::path& requested,
    const char* executable) {
  if (executable == nullptr || *executable == '\0') return false;
  const std::filesystem::path candidate(executable);
  if (candidate.is_absolute() && canonical_path(candidate.string()) == requested) {
    return true;
  }
  return candidate.filename() == requested.filename();
}

AppInfoPtr app_info_for_path(const std::string& app_path) {
  const std::filesystem::path requested = canonical_path(app_path);
  if (requested.extension() == ".desktop") {
    return AppInfoPtr(G_APP_INFO(
        g_desktop_app_info_new_from_filename(requested.c_str())));
  }

  GList* values = g_app_info_get_all();
  GAppInfo* match = nullptr;
  for (GList* cursor = values; cursor != nullptr; cursor = cursor->next) {
    auto* info = G_APP_INFO(cursor->data);
    if (executable_matches(requested, g_app_info_get_executable(info))) {
      match = G_APP_INFO(g_object_ref(info));
      break;
    }
  }
  g_list_free_full(values, g_object_unref);
  return AppInfoPtr(match);
}

PixbufPtr pixbuf_for_icon(GIcon* icon, int pixels) {
  if (icon == nullptr) return {};
  GtkIconTheme* theme = gtk_icon_theme_get_default();
  if (theme == nullptr) return {};
  IconInfoPtr info(gtk_icon_theme_lookup_by_gicon(
      theme,
      icon,
      pixels,
      static_cast<GtkIconLookupFlags>(
          GTK_ICON_LOOKUP_FORCE_SIZE | GTK_ICON_LOOKUP_USE_BUILTIN)));
  if (!info) return {};

  GError* raw_error = nullptr;
  PixbufPtr pixbuf(gtk_icon_info_load_icon(info.get(), &raw_error));
  ErrorPtr error(raw_error);
  return pixbuf;
}

PixbufPtr file_icon_pixbuf(const std::string& app_path, int pixels) {
  FilePtr file(g_file_new_for_path(app_path.c_str()));
  if (!file) return {};
  GError* raw_error = nullptr;
  FileInfoPtr info(g_file_query_info(
      file.get(),
      G_FILE_ATTRIBUTE_STANDARD_ICON,
      G_FILE_QUERY_INFO_NONE,
      nullptr,
      &raw_error));
  ErrorPtr error(raw_error);
  if (!info) return {};
  return pixbuf_for_icon(g_file_info_get_icon(info.get()), pixels);
}

}  // namespace

void require_gtk() {
  static bool initialized = false;
  if (initialized) return;
  if (gdk_display_get_default() != nullptr) {
    initialized = true;
    return;
  }
  if (!gtk_init_check(nullptr, nullptr)) {
    throw std::runtime_error(
        "nativekit Linux support requires an available desktop display");
  }
  initialized = true;
}

GdkPixbuf* pixbuf_from_data_url(const std::string& data_url) {
  if (data_url.empty() || data_url.size() > kMaximumDataUrlLength) {
    throw std::runtime_error("overlay image data exceeds the 32 MiB limit");
  }
  const std::size_t comma = data_url.find(',');
  if (comma == std::string::npos || comma + 1 >= data_url.size()) {
    throw std::runtime_error("overlay image data URL is invalid");
  }
  const std::string header = lower_ascii(data_url.substr(0, comma));
  if (header != "data:image/png;base64" &&
      header != "data:image/jpeg;base64" &&
      header != "data:image/jpg;base64") {
    throw std::runtime_error("overlay image must be a PNG or JPEG data URL");
  }
  const std::string encoded = data_url.substr(comma + 1);
  if (!valid_base64(encoded)) {
    throw std::runtime_error("overlay image base64 data is invalid");
  }

  gsize decoded_size = 0;
  std::unique_ptr<guchar, decltype(&g_free)> decoded(
      g_base64_decode(encoded.c_str(), &decoded_size), &g_free);
  if (!decoded || decoded_size == 0) {
    throw std::runtime_error("overlay image base64 data is invalid");
  }

  PixbufLoaderPtr loader(gdk_pixbuf_loader_new());
  if (!loader) throw std::runtime_error("GdkPixbuf loader is unavailable");
  PreparedImageSize prepared;
  g_signal_connect(
      loader.get(),
      "size-prepared",
      G_CALLBACK(validate_prepared_size),
      &prepared);

  GError* raw_error = nullptr;
  const bool written = gdk_pixbuf_loader_write(
      loader.get(), decoded.get(), decoded_size, &raw_error);
  ErrorPtr error(raw_error);
  if (!written) throw gtk_error("GdkPixbuf decode", error.get());

  raw_error = nullptr;
  const bool closed = gdk_pixbuf_loader_close(loader.get(), &raw_error);
  ErrorPtr close_error(raw_error);
  if (!closed) throw gtk_error("GdkPixbuf decode", close_error.get());
  if (prepared.invalid) {
    throw std::runtime_error("overlay image dimensions exceed the limit");
  }

  GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader.get());
  if (pixbuf == nullptr) {
    throw std::runtime_error("overlay image data could not be decoded");
  }
  return GDK_PIXBUF(g_object_ref(pixbuf));
}

GdkPixbuf* app_icon_pixbuf(const std::string& app_path, int pixels) {
  if (pixels <= 0 || app_path.empty()) return nullptr;
  std::error_code error;
  if (!std::filesystem::exists(app_path, error) || error) return nullptr;

  require_gtk();
  PixbufPtr source;
  if (AppInfoPtr info = app_info_for_path(app_path)) {
    source = pixbuf_for_icon(g_app_info_get_icon(info.get()), pixels);
  }
  if (!source) source = file_icon_pixbuf(app_path, pixels);
  if (!source) return nullptr;
  PixbufPtr exact = exact_square_pixbuf(source.get(), pixels);
  return exact ? exact.release() : nullptr;
}

std::optional<std::string> pixbuf_to_png_data_url(
    GdkPixbuf* pixbuf,
    int pixels) {
  PixbufPtr exact = exact_square_pixbuf(pixbuf, pixels);
  if (!exact) return std::nullopt;

  gchar* raw_png = nullptr;
  gsize png_size = 0;
  GError* raw_error = nullptr;
  const bool saved = gdk_pixbuf_save_to_buffer(
      exact.get(),
      &raw_png,
      &png_size,
      "png",
      &raw_error,
      nullptr);
  std::unique_ptr<gchar, decltype(&g_free)> png(raw_png, &g_free);
  ErrorPtr error(raw_error);
  if (!saved || !png || png_size == 0) return std::nullopt;

  std::unique_ptr<gchar, decltype(&g_free)> encoded(
      g_base64_encode(
          reinterpret_cast<const guchar*>(png.get()), png_size),
      &g_free);
  if (!encoded) return std::nullopt;
  return std::string("data:image/png;base64,") + encoded.get();
}

std::optional<std::string> icon_to_png_data_url(
    const std::string& app_path,
    int pixels) {
  PixbufPtr pixbuf(app_icon_pixbuf(app_path, pixels));
  return pixbuf_to_png_data_url(pixbuf.get(), pixels);
}

}  // namespace nativekit::platform
