#include "apps/icon.h"

#include <windows.h>

#include <limits>
#include <string>

#include "common/win/image_utils.h"

namespace nativekit::platform {
namespace {

std::wstring utf8_to_utf16(const std::string& value) {
  if (value.empty() || value.find('\0') != std::string::npos ||
      value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return {};
  }
  const int size = MultiByteToWideChar(
      CP_UTF8,
      MB_ERR_INVALID_CHARS,
      value.data(),
      static_cast<int>(value.size()),
      nullptr,
      0);
  if (size <= 0) return {};
  std::wstring converted(static_cast<std::size_t>(size), L'\0');
  if (MultiByteToWideChar(
          CP_UTF8,
          MB_ERR_INVALID_CHARS,
          value.data(),
          static_cast<int>(value.size()),
          converted.data(),
          size) != size) {
    return {};
  }
  return converted;
}

}  // namespace

std::optional<std::string> app_icon(
    const std::string& app_path,
    int pixels) {
  return icon_to_png_data_url(utf8_to_utf16(app_path), pixels);
}

}  // namespace nativekit::platform
