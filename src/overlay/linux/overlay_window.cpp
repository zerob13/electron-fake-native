#include "overlay/overlay_manager.h"

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/linux/image_utils.h"

namespace nativekit::platform {
namespace {

constexpr int kMinimumPanelSize = 64;
constexpr int kStackGap = 12;
constexpr int kControlMargin = 6;
constexpr int kIconMargin = 8;
constexpr int kIconSize = 28;

void trace_native(const char* step) {
  if (std::getenv("NATIVEKIT_NATIVE_TRACE") == nullptr) return;
  std::fprintf(stderr, "NATIVEKIT_NATIVE_TRACE %s\n", step);
  std::fflush(stderr);
}

template <typename Type>
struct GObjectDeleter {
  void operator()(Type* value) const {
    if (value != nullptr) g_object_unref(value);
  }
};

using PixbufPtr = std::unique_ptr<GdkPixbuf, GObjectDeleter<GdkPixbuf>>;

struct PanelState {
  OverlayPlatformEvents* events = nullptr;
  std::string id;
  std::string host_id;
  GtkWidget* window = nullptr;
  GtkWidget* content = nullptr;
  GtkWidget* image = nullptr;
  GtkWidget* icon = nullptr;
  GtkWidget* hide_button = nullptr;
  GtkWidget* relocate_button = nullptr;
  GdkRectangle work_area{};
  int width = 1;
  int height = 1;
  int drag_start_x = 0;
  int drag_start_y = 0;
  int manual_x = 0;
  int manual_y = 0;
  bool dragging = false;
  bool manually_positioned = false;
};

struct RenderItem {
  std::string id;
  std::string host_id;
  std::string title;
  std::string hide_tooltip;
  std::string relocate_tooltip;
  std::uintptr_t host_window = 0;
  PixbufPtr image;
  PixbufPtr icon;
  GdkRectangle work_area{};
  int x = 0;
  int y = 0;
  int width = 1;
  int height = 1;
  bool visible = false;
  bool preserve_position = false;
};

struct HostLayout {
  GdkRectangle work_area{};
  double cursor = 0;
};

GdkRectangle work_area_for_host(const OverlayHost& host) {
  GdkDisplay* display = gdk_display_get_default();
  if (display == nullptr || !GDK_IS_X11_DISPLAY(display)) {
    throw std::runtime_error(
        "nativekit overlays require Electron to use X11/XWayland; "
        "start Electron with --ozone-platform=x11");
  }

  GdkMonitor* monitor = nullptr;
  GdkWindow* foreign = gdk_x11_window_foreign_new_for_display(
      display, static_cast<Window>(host.window_handle));
  if (foreign != nullptr) {
    monitor = gdk_display_get_monitor_at_window(display, foreign);
    g_object_unref(foreign);
  }
  if (monitor == nullptr) {
    monitor = gdk_display_get_monitor_at_point(
        display,
        static_cast<int>(std::lround(host.bounds.x + host.bounds.width / 2)),
        static_cast<int>(std::lround(host.bounds.y + host.bounds.height / 2)));
  }
  if (monitor == nullptr) monitor = gdk_display_get_primary_monitor(display);

  GdkRectangle result{};
  if (monitor != nullptr) gdk_monitor_get_workarea(monitor, &result);
  if (result.width <= 0 || result.height <= 0) {
    GdkScreen* screen = gdk_display_get_default_screen(display);
    result = {
        0,
        0,
        screen == nullptr ? 1 : gdk_screen_get_width(screen),
        screen == nullptr ? 1 : gdk_screen_get_height(screen),
    };
  }
  return result;
}

std::pair<int, int> fitted_size(
    GdkPixbuf* image,
    const OverlayHost& host,
    double max_size,
    const GdkRectangle& work_area) {
  const int source_width = gdk_pixbuf_get_width(image);
  const int source_height = gdk_pixbuf_get_height(image);
  const double width_limit = std::min({
      max_size,
      std::max(host.bounds.width, static_cast<double>(kMinimumPanelSize)),
      static_cast<double>(work_area.width),
  });
  const double height_limit = std::min({
      max_size,
      std::max(host.bounds.height, static_cast<double>(kMinimumPanelSize)),
      static_cast<double>(work_area.height),
  });
  const double scale = std::min({
      1.0,
      width_limit / std::max(source_width, 1),
      height_limit / std::max(source_height, 1),
      max_size / std::max(source_width, source_height),
  });
  const int width = std::clamp(
      std::max(
          kMinimumPanelSize,
          static_cast<int>(std::floor(source_width * scale))),
      1,
      std::max(work_area.width, 1));
  const int height = std::clamp(
      std::max(
          kMinimumPanelSize,
          static_cast<int>(std::floor(source_height * scale))),
      1,
      std::max(work_area.height, 1));
  return {width, height};
}

PixbufPtr scale_pixbuf(GdkPixbuf* source, int width, int height) {
  const int source_width = gdk_pixbuf_get_width(source);
  const int source_height = gdk_pixbuf_get_height(source);
  const double scale = std::min(
      static_cast<double>(width) / source_width,
      static_cast<double>(height) / source_height);
  const int content_width = std::clamp(
      static_cast<int>(std::lround(source_width * scale)), 1, width);
  const int content_height = std::clamp(
      static_cast<int>(std::lround(source_height * scale)), 1, height);
  PixbufPtr result(gdk_pixbuf_new(
      GDK_COLORSPACE_RGB, TRUE, 8, width, height));
  if (!result) return {};
  gdk_pixbuf_fill(result.get(), 0x00000000);
  const int x = (width - content_width) / 2;
  const int y = (height - content_height) / 2;
  gdk_pixbuf_composite(
      source,
      result.get(),
      x,
      y,
      content_width,
      content_height,
      x,
      y,
      scale,
      scale,
      GDK_INTERP_BILINEAR,
      255);
  return result;
}

std::pair<int, int> presentation_origin(
    const OverlayHost& host,
    int width,
    int height,
    double cursor,
    const GdkRectangle& work_area) {
  const int offset = static_cast<int>(std::lround(host.anchor.offset));
  const int stack_offset = static_cast<int>(std::lround(cursor));
  switch (host.anchor.edge) {
    case AnchorEdge::kLeading:
      return {
          work_area.x + offset,
          work_area.y + offset + stack_offset,
      };
    case AnchorEdge::kTrailing:
      return {
          work_area.x + work_area.width - offset - width,
          work_area.y + offset + stack_offset,
      };
    case AnchorEdge::kTop:
      return {
          work_area.x + offset + stack_offset,
          work_area.y + offset,
      };
    case AnchorEdge::kBottom:
      return {
          work_area.x + offset + stack_offset,
          work_area.y + work_area.height - offset - height,
      };
  }
  return {work_area.x, work_area.y};
}

bool presentation_fits(
    const OverlayHost& host,
    int width,
    int height,
    double cursor,
    const GdkRectangle& work_area) {
  const double offset = host.anchor.offset;
  if (host.anchor.edge == AnchorEdge::kLeading ||
      host.anchor.edge == AnchorEdge::kTrailing) {
    return offset + width <= work_area.width &&
           offset + cursor + height <= work_area.height;
  }
  return offset + height <= work_area.height &&
         offset + cursor + width <= work_area.width;
}

std::pair<int, int> clamped_origin(
    int x,
    int y,
    int width,
    int height,
    const GdkRectangle& work_area) {
  const int maximum_x =
      std::max(work_area.x, work_area.x + work_area.width - width);
  const int maximum_y =
      std::max(work_area.y, work_area.y + work_area.height - height);
  return {
      std::clamp(x, work_area.x, maximum_x),
      std::clamp(y, work_area.y, maximum_y),
  };
}

gboolean handle_button_press(
    GtkWidget*,
    GdkEventButton* event,
    gpointer user_data) {
  auto& state = *static_cast<PanelState*>(user_data);
  if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
  if (event->type == GDK_2BUTTON_PRESS) {
    if (state.events != nullptr && state.events->activate) {
      state.events->activate();
    }
    return TRUE;
  }
  if (event->type != GDK_BUTTON_PRESS) return FALSE;

  gtk_window_get_position(
      GTK_WINDOW(state.window), &state.drag_start_x, &state.drag_start_y);
  state.dragging = true;
  gtk_window_begin_move_drag(
      GTK_WINDOW(state.window),
      static_cast<int>(event->button),
      static_cast<int>(std::lround(event->x_root)),
      static_cast<int>(std::lround(event->y_root)),
      event->time);
  return TRUE;
}

gboolean handle_configure(
    GtkWidget*,
    GdkEventConfigure* event,
    gpointer user_data) {
  auto& state = *static_cast<PanelState*>(user_data);
  if (!state.dragging ||
      (event->x == state.drag_start_x && event->y == state.drag_start_y)) {
    return FALSE;
  }
  state.manually_positioned = true;
  state.manual_x = event->x;
  state.manual_y = event->y;
  return FALSE;
}

gboolean handle_button_release(
    GtkWidget*,
    GdkEventButton* event,
    gpointer user_data) {
  auto& state = *static_cast<PanelState*>(user_data);
  if (event->button != GDK_BUTTON_PRIMARY || !state.dragging) return FALSE;
  state.dragging = false;
  int x = 0;
  int y = 0;
  gtk_window_get_position(GTK_WINDOW(state.window), &x, &y);
  if (x != state.drag_start_x || y != state.drag_start_y) {
    state.manually_positioned = true;
    std::tie(state.manual_x, state.manual_y) = clamped_origin(
        x, y, state.width, state.height, state.work_area);
    gtk_window_move(
        GTK_WINDOW(state.window), state.manual_x, state.manual_y);
    if (state.events != nullptr && state.events->refresh) {
      state.events->refresh();
    }
  }
  return TRUE;
}

void handle_hide(GtkButton*, gpointer user_data) {
  auto& state = *static_cast<PanelState*>(user_data);
  if (state.events != nullptr && state.events->visibility_request) {
    state.events->visibility_request(false);
  }
}

void handle_relocate(GtkButton*, gpointer user_data) {
  auto& state = *static_cast<PanelState*>(user_data);
  state.manually_positioned = false;
  state.dragging = false;
  if (state.events != nullptr && state.events->relocate) {
    state.events->relocate(state.host_id);
  }
}

GtkWidget* icon_button(const char* icon_name) {
  GtkWidget* button = gtk_button_new();
  gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NORMAL);
  gtk_widget_set_can_focus(button, FALSE);
  GtkWidget* icon = gtk_image_new_from_icon_name(
      icon_name, GTK_ICON_SIZE_SMALL_TOOLBAR);
  gtk_container_add(GTK_CONTAINER(button), icon);
  return button;
}

std::unique_ptr<PanelState> create_panel(
    const RenderItem& item,
    OverlayPlatformEvents& events) {
  auto state = std::make_unique<PanelState>();
  state->events = &events;
  state->id = item.id;
  state->host_id = item.host_id;
  state->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  if (state->window == nullptr) {
    throw std::runtime_error("GTK could not create an overlay window");
  }

  gtk_widget_set_name(state->window, "nativekit-overlay");
  gtk_window_set_title(GTK_WINDOW(state->window), item.title.c_str());
  gtk_window_set_decorated(GTK_WINDOW(state->window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(state->window), FALSE);
  gtk_window_set_accept_focus(GTK_WINDOW(state->window), FALSE);
  gtk_window_set_focus_on_map(GTK_WINDOW(state->window), FALSE);
  gtk_window_set_skip_taskbar_hint(GTK_WINDOW(state->window), TRUE);
  gtk_window_set_skip_pager_hint(GTK_WINDOW(state->window), TRUE);
  gtk_window_set_keep_above(GTK_WINDOW(state->window), TRUE);
  gtk_window_set_type_hint(
      GTK_WINDOW(state->window), GDK_WINDOW_TYPE_HINT_UTILITY);
  gtk_window_stick(GTK_WINDOW(state->window));
  gtk_widget_set_app_paintable(state->window, TRUE);
  gtk_widget_add_events(
      state->window,
      GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
          GDK_STRUCTURE_MASK);

  GtkCssProvider* provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(
      provider,
      "#nativekit-overlay { background-color: transparent; "
      "background-image: none; }",
      -1,
      nullptr);
  gtk_style_context_add_provider(
      gtk_widget_get_style_context(state->window),
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);

  state->content = gtk_overlay_new();
  state->image = gtk_image_new();
  state->icon = gtk_image_new();
  state->hide_button = icon_button("view-conceal-symbolic");
  state->relocate_button = icon_button("view-refresh-symbolic");

  gtk_container_add(GTK_CONTAINER(state->window), state->content);
  gtk_container_add(GTK_CONTAINER(state->content), state->image);

  GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_pack_start(
      GTK_BOX(controls), state->relocate_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(controls), state->hide_button, FALSE, FALSE, 0);
  gtk_widget_set_halign(controls, GTK_ALIGN_END);
  gtk_widget_set_valign(controls, GTK_ALIGN_START);
  gtk_widget_set_margin_top(controls, kControlMargin);
  gtk_widget_set_margin_end(controls, kControlMargin);
  gtk_overlay_add_overlay(GTK_OVERLAY(state->content), controls);

  gtk_widget_set_halign(state->icon, GTK_ALIGN_START);
  gtk_widget_set_valign(state->icon, GTK_ALIGN_END);
  gtk_widget_set_margin_start(state->icon, kIconMargin);
  gtk_widget_set_margin_bottom(state->icon, kIconMargin);
  gtk_overlay_add_overlay(GTK_OVERLAY(state->content), state->icon);

  g_signal_connect(
      state->window,
      "button-press-event",
      G_CALLBACK(handle_button_press),
      state.get());
  g_signal_connect(
      state->window,
      "button-release-event",
      G_CALLBACK(handle_button_release),
      state.get());
  g_signal_connect(
      state->window,
      "configure-event",
      G_CALLBACK(handle_configure),
      state.get());
  g_signal_connect(
      state->hide_button, "clicked", G_CALLBACK(handle_hide), state.get());
  g_signal_connect(
      state->relocate_button,
      "clicked",
      G_CALLBACK(handle_relocate),
      state.get());
  return state;
}

void set_transient_for_host(PanelState& state, std::uintptr_t host_window) {
  gtk_widget_realize(state.window);
  GdkWindow* window = gtk_widget_get_window(state.window);
  GdkDisplay* display = gtk_widget_get_display(state.window);
  if (window == nullptr || display == nullptr || host_window == 0) return;
  GdkWindow* host = gdk_x11_window_foreign_new_for_display(
      display, static_cast<Window>(host_window));
  if (host == nullptr) return;
  gdk_window_set_transient_for(window, host);
  g_object_unref(host);
}

class LinuxOverlayPlatform final : public OverlayPlatform {
 public:
  explicit LinuxOverlayPlatform(OverlayPlatformEvents events)
      : events_(std::move(events)) {
    require_gtk();
    GdkDisplay* display = gdk_display_get_default();
    if (display == nullptr || !GDK_IS_X11_DISPLAY(display)) {
      throw std::runtime_error(
          "nativekit overlays require Electron to use X11/XWayland; "
          "start Electron with --ozone-platform=x11");
    }
  }

  ~LinuxOverlayPlatform() override { stop(); }

  void update(const OverlaySnapshot& snapshot) override {
    trace_native("linux:update:start");
    std::unordered_map<std::string, const OverlayHost*> hosts;
    hosts.reserve(snapshot.hosts.size());
    for (const auto& host : snapshot.hosts) hosts.emplace(host.id, &host);

    std::unordered_map<std::string, HostLayout> layouts;
    std::unordered_set<std::string> active_ids;
    std::vector<RenderItem> items;
    items.reserve(snapshot.presentations.size());
    for (const auto& presentation : snapshot.presentations) {
      const auto host = hosts.find(presentation.host_id);
      if (host == hosts.end()) continue;
      active_ids.insert(presentation.id);

      auto layout = layouts.find(presentation.host_id);
      if (layout == layouts.end()) {
        layout = layouts
                     .emplace(
                         presentation.host_id,
                         HostLayout{work_area_for_host(*host->second), 0})
                     .first;
      }

      PixbufPtr source(pixbuf_from_data_url(presentation.image_data));
      const auto [width, height] = fitted_size(
          source.get(),
          *host->second,
          snapshot.max_size,
          layout->second.work_area);
      RenderItem item;
      item.id = presentation.id;
      item.host_id = presentation.host_id;
      item.title = host->second->title;
      item.hide_tooltip = snapshot.options.hide_tooltip;
      item.relocate_tooltip = snapshot.options.relocate_tooltip;
      item.host_window = host->second->window_handle;
      item.image = scale_pixbuf(source.get(), width, height);
      if (!item.image) {
        throw std::runtime_error("GTK could not scale the overlay image");
      }
      if (presentation.app_icon_path) {
        item.icon.reset(
            app_icon_pixbuf(*presentation.app_icon_path, kIconSize));
      }
      item.work_area = layout->second.work_area;
      item.width = width;
      item.height = height;

      const bool eligible = presentation.visible && snapshot.visible;
      const bool stack_slot_fits = presentation_fits(
          *host->second,
          width,
          height,
          layout->second.cursor,
          layout->second.work_area);
      const auto existing = panels_.find(presentation.id);
      item.preserve_position =
          existing != panels_.end() &&
          existing->second->manually_positioned;
      if (item.preserve_position) {
        std::tie(item.x, item.y) = clamped_origin(
            existing->second->manual_x,
            existing->second->manual_y,
            width,
            height,
            item.work_area);
        item.visible = eligible;
      } else {
        std::tie(item.x, item.y) = presentation_origin(
            *host->second,
            width,
            height,
            layout->second.cursor,
            layout->second.work_area);
        item.visible = eligible && stack_slot_fits;
      }
      if (eligible && (stack_slot_fits || item.preserve_position)) {
        layout->second.cursor +=
            (host->second->anchor.edge == AnchorEdge::kLeading ||
             host->second->anchor.edge == AnchorEdge::kTrailing)
                ? height + kStackGap
                : width + kStackGap;
      }
      items.push_back(std::move(item));
    }

    for (auto& item : items) apply_item(item);
    for (auto iterator = panels_.begin(); iterator != panels_.end();) {
      if (active_ids.find(iterator->first) != active_ids.end()) {
        ++iterator;
        continue;
      }
      gtk_widget_destroy(iterator->second->window);
      iterator = panels_.erase(iterator);
    }

    for (auto item = items.rbegin(); item != items.rend(); ++item) {
      if (!item->visible) continue;
      const auto panel = panels_.find(item->id);
      if (panel == panels_.end()) continue;
      GdkWindow* window = gtk_widget_get_window(panel->second->window);
      if (window != nullptr) gdk_window_raise(window);
    }
    trace_native("linux:update:ready");
  }

  void stop() override {
    for (auto& [id, panel] : panels_) {
      if (panel->window != nullptr) gtk_widget_destroy(panel->window);
    }
    panels_.clear();
  }

 private:
  void apply_item(const RenderItem& item) {
    auto existing = panels_.find(item.id);
    if (existing == panels_.end()) {
      auto panel = create_panel(item, events_);
      existing = panels_.emplace(item.id, std::move(panel)).first;
    }
    PanelState& state = *existing->second;
    state.host_id = item.host_id;
    state.work_area = item.work_area;
    state.width = item.width;
    state.height = item.height;
    if (item.preserve_position) {
      state.manual_x = item.x;
      state.manual_y = item.y;
    }

    gtk_window_set_title(GTK_WINDOW(state.window), item.title.c_str());
    gtk_widget_set_tooltip_text(
        state.hide_button, item.hide_tooltip.c_str());
    gtk_widget_set_tooltip_text(
        state.relocate_button, item.relocate_tooltip.c_str());
    gtk_image_set_from_pixbuf(GTK_IMAGE(state.image), item.image.get());
    gtk_image_set_from_pixbuf(GTK_IMAGE(state.icon), item.icon.get());
    gtk_window_resize(
        GTK_WINDOW(state.window), item.width, item.height);
    gtk_widget_show_all(state.content);
    if (!item.icon) gtk_widget_hide(state.icon);
    set_transient_for_host(state, item.host_window);
    gtk_window_move(GTK_WINDOW(state.window), item.x, item.y);

    if (item.visible) {
      gtk_widget_show(state.window);
      gtk_window_move(GTK_WINDOW(state.window), item.x, item.y);
    } else {
      gtk_widget_hide(state.window);
    }
  }

  OverlayPlatformEvents events_;
  std::unordered_map<std::string, std::unique_ptr<PanelState>> panels_;
};

}  // namespace

std::unique_ptr<OverlayPlatform> create_overlay_platform(
    OverlayPlatformEvents events) {
  return std::make_unique<LinuxOverlayPlatform>(std::move(events));
}

}  // namespace nativekit::platform
