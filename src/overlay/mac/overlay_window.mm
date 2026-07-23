#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include "overlay/overlay_manager.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "common/mac/image_utils.h"

namespace {

NSRect clamped_frame(NSRect requested, NSScreen* screen) {
  if (screen == nil) return requested;
  const NSRect available = screen.visibleFrame;
  requested.size.width = std::min(requested.size.width, available.size.width);
  requested.size.height = std::min(requested.size.height, available.size.height);
  requested.origin.x = std::clamp(
      requested.origin.x,
      NSMinX(available),
      NSMaxX(available) - requested.size.width);
  requested.origin.y = std::clamp(
      requested.origin.y,
      NSMinY(available),
      NSMaxY(available) - requested.size.height);
  return requested;
}

NSScreen* screen_for_frame(NSRect frame) {
  NSScreen* result = nil;
  CGFloat largest_area = 0;
  for (NSScreen* screen in NSScreen.screens) {
    const NSRect intersection = NSIntersectionRect(frame, screen.frame);
    const CGFloat area = intersection.size.width * intersection.size.height;
    if (area > largest_area) {
      largest_area = area;
      result = screen;
    }
  }
  return result;
}

NSString* ns_string(const std::string& value) {
  return [NSString stringWithUTF8String:value.c_str()] ?: @"";
}

NSImage* control_image(nativekit::OverlayControlIcon icon) {
  switch (icon) {
    case nativekit::OverlayControlIcon::kClose:
      return [NSImage imageWithSystemSymbolName:@"xmark"
                      accessibilityDescription:@"Close"];
    case nativekit::OverlayControlIcon::kPanelRightOpen:
      return [NSImage imageWithSystemSymbolName:@"sidebar.right"
                      accessibilityDescription:@"Open right panel"];
  }
  return nil;
}

bool controls_equal(
    const std::vector<nativekit::OverlayControl>& first,
    const std::vector<nativekit::OverlayControl>& second) {
  if (first.size() != second.size()) return false;
  for (std::size_t index = 0; index < first.size(); ++index) {
    if (first[index].id != second[index].id ||
        first[index].icon != second[index].icon ||
        first[index].tooltip != second[index].tooltip) {
      return false;
    }
  }
  return true;
}

}  // namespace

@interface NativekitOverlayPanel : NSPanel
@end

@implementation NativekitOverlayPanel
- (BOOL)canBecomeKeyWindow {
  return NO;
}
- (BOOL)canBecomeMainWindow {
  return NO;
}
@end

@interface NativekitOverlayView : NSView <NSWindowDelegate>
- (instancetype)initWithActivate:(std::function<void()>)activate
                          control:
                              (std::function<void(const std::string&)>)control
                            moved:(std::function<void(NSRect)>)moved;
- (void)applyFrameFromLayout:(NSRect)frame;
- (void)setContentImage:(NSImage*)image
               iconPath:(NSString*)iconPath;
- (void)setControls:
    (const std::vector<nativekit::OverlayControl>&)controls;
@end

@implementation NativekitOverlayView {
  NSImageView* image_view_;
  NSImageView* icon_view_;
  NSMutableArray<NSButton*>* control_buttons_;
  std::vector<nativekit::OverlayControl> controls_;
  std::function<void()> activate_;
  std::function<void(const std::string&)> control_;
  std::function<void(NSRect)> moved_;
  BOOL applying_layout_frame_;
  BOOL has_pending_layout_origin_;
  NSPoint pending_layout_origin_;
}

- (instancetype)initWithActivate:(std::function<void()>)activate
                          control:
                              (std::function<void(const std::string&)>)control
                            moved:(std::function<void(NSRect)>)moved {
  self = [super initWithFrame:NSZeroRect];
  if (self == nil) return nil;

  activate_ = std::move(activate);
  control_ = std::move(control);
  moved_ = std::move(moved);
  self.wantsLayer = YES;
  self.layer.backgroundColor = NSColor.clearColor.CGColor;

  image_view_ = [[NSImageView alloc] initWithFrame:NSZeroRect];
  image_view_.imageScaling = NSImageScaleProportionallyUpOrDown;
  image_view_.imageAlignment = NSImageAlignCenter;
  image_view_.accessibilityElement = YES;
  [self addSubview:image_view_];

  icon_view_ = [[NSImageView alloc] initWithFrame:NSZeroRect];
  icon_view_.imageScaling = NSImageScaleProportionallyUpOrDown;
  icon_view_.wantsLayer = YES;
  icon_view_.layer.cornerRadius = 6;
  icon_view_.layer.masksToBounds = YES;
  [self addSubview:icon_view_];

  control_buttons_ = [[NSMutableArray alloc] init];

  return self;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}

- (NSView*)hitTest:(NSPoint)point {
  NSView* hit = [super hitTest:point];
  if (hit == nil) return nil;
  for (NSButton* button in control_buttons_) {
    if (hit == button || [hit isDescendantOf:button]) return hit;
  }
  return self;
}

- (void)mouseDown:(NSEvent*)event {
  if (event.clickCount == 2) {
    if (activate_) activate_();
    return;
  }
  NativekitOverlayPanel* panel = (NativekitOverlayPanel*)self.window;
  // AppKit returns before the Window Server finishes moving the panel.
  [panel performWindowDragWithEvent:event];
}

- (void)applyFrameFromLayout:(NSRect)frame {
  pending_layout_origin_ = frame.origin;
  has_pending_layout_origin_ = YES;
  applying_layout_frame_ = YES;
  [self.window setFrame:frame display:YES animate:NO];
  applying_layout_frame_ = NO;
}

- (void)windowDidMove:(NSNotification*)notification {
  NSWindow* window = notification.object;
  if (window != self.window) return;

  const NSRect frame = window.frame;
  // Snapshot layout must not be mistaken for user-owned placement.
  if (applying_layout_frame_ ||
      (has_pending_layout_origin_ &&
       NSEqualPoints(pending_layout_origin_, frame.origin))) {
    has_pending_layout_origin_ = NO;
    return;
  }
  has_pending_layout_origin_ = NO;
  if (moved_) {
    NSScreen* screen = window.screen ?: screen_for_frame(frame);
    moved_(clamped_frame(frame, screen));
  }
}

- (void)layout {
  [super layout];
  image_view_.frame = self.bounds;
  constexpr CGFloat button_size = 26;
  constexpr CGFloat margin = 8;
  constexpr CGFloat gap = 6;
  CGFloat x = NSMaxX(self.bounds) - margin - button_size;
  for (NSInteger index = control_buttons_.count - 1; index >= 0; --index) {
    control_buttons_[index].frame = NSMakeRect(
        x,
        NSMaxY(self.bounds) - margin - button_size,
        button_size,
        button_size);
    x -= button_size + gap;
  }
  icon_view_.frame = NSMakeRect(margin, margin, 28, 28);
}

- (void)setContentImage:(NSImage*)image
               iconPath:(NSString*)iconPath {
  image_view_.image = image;
  image_view_.accessibilityLabel = @"Overlay image";
  icon_view_.image = iconPath.length == 0
                         ? nil
                         : [NSWorkspace.sharedWorkspace iconForFile:iconPath];
  icon_view_.hidden = icon_view_.image == nil;
}

- (void)setControls:
    (const std::vector<nativekit::OverlayControl>&)controls {
  if (controls_equal(controls_, controls)) return;
  for (NSButton* button in control_buttons_) [button removeFromSuperview];
  [control_buttons_ removeAllObjects];
  controls_ = controls;

  for (std::size_t index = 0; index < controls_.size(); ++index) {
    const auto& control = controls_[index];
    NSButton* button = [NSButton
        buttonWithImage:control_image(control.icon)
                 target:self
                 action:@selector(controlOverlay:)];
    button.tag = static_cast<NSInteger>(index);
    button.bezelStyle = NSBezelStyleCircular;
    button.bordered = YES;
    button.focusRingType = NSFocusRingTypeNone;
    button.contentTintColor = NSColor.labelColor;
    button.toolTip = ns_string(control.tooltip);
    button.accessibilityLabel = control.tooltip.empty()
        ? ns_string(control.id)
        : ns_string(control.tooltip);
    [control_buttons_ addObject:button];
    [self addSubview:button];
  }
  [self setNeedsLayout:YES];
}

- (void)controlOverlay:(NSButton*)sender {
  const NSInteger index = sender.tag;
  if (index < 0 ||
      static_cast<std::size_t>(index) >= controls_.size()) {
    return;
  }
  if (control_) control_(controls_[index].id);
}

@end

namespace nativekit::platform {
namespace {

NSSize fitted_size(NSImage* image, double max_size, NSScreen* screen) {
  NSSize size = image.size;
  if (size.width <= 0 || size.height <= 0) {
    size = NSMakeSize(64, 64);
  }
  const NSSize available =
      screen == nil ? NSMakeSize(max_size, max_size) : screen.visibleFrame.size;
  const double width_limit =
      std::min(max_size, std::max<double>(available.width, 1));
  const double height_limit =
      std::min(max_size, std::max<double>(available.height, 1));
  const double scale = std::min({
      1.0,
      width_limit / std::max<double>(size.width, 1),
      height_limit / std::max<double>(size.height, 1),
      max_size / std::max<double>(std::max(size.width, size.height), 1),
  });
  return NSMakeSize(
      std::min<CGFloat>(
          available.width,
          std::max<CGFloat>(64, std::floor(size.width * scale))),
      std::min<CGFloat>(
          available.height,
          std::max<CGFloat>(64, std::floor(size.height * scale))));
}

NSScreen* screen_for_host(const OverlayHost& host) {
  NSView* view = (__bridge NSView*)(reinterpret_cast<void*>(host.window_handle));
  return view.window.screen ?: NSScreen.mainScreen ?: NSScreen.screens.firstObject;
}

NSRect frame_for_presentation(
    const OverlayHost& host,
    NSSize size,
    double cursor) {
  NSScreen* screen = screen_for_host(host);
  if (screen == nil) return NSMakeRect(0, 0, size.width, size.height);
  const NSRect frame = screen.visibleFrame;
  const CGFloat offset = host.anchor.offset;
  switch (host.anchor.edge) {
    case AnchorEdge::kLeading:
      return clamped_frame(NSMakeRect(
          NSMinX(frame) + offset,
          NSMaxY(frame) - offset - cursor - size.height,
          size.width,
          size.height), screen);
    case AnchorEdge::kTrailing:
      return clamped_frame(NSMakeRect(
          NSMaxX(frame) - offset - size.width,
          NSMaxY(frame) - offset - cursor - size.height,
          size.width,
          size.height), screen);
    case AnchorEdge::kTop:
      return clamped_frame(NSMakeRect(
          NSMinX(frame) + offset + cursor,
          NSMaxY(frame) - offset - size.height,
          size.width,
          size.height), screen);
    case AnchorEdge::kBottom:
      return clamped_frame(NSMakeRect(
          NSMinX(frame) + offset + cursor,
          NSMinY(frame) + offset,
          size.width,
          size.height), screen);
  }
  return NSMakeRect(0, 0, size.width, size.height);
}

bool presentation_fits(
    const OverlayHost& host,
    NSSize size,
    double cursor) {
  NSScreen* screen = screen_for_host(host);
  if (screen == nil) return false;
  const NSSize available = screen.visibleFrame.size;
  const double offset = host.anchor.offset;
  if (host.anchor.edge == AnchorEdge::kLeading ||
      host.anchor.edge == AnchorEdge::kTrailing) {
    return offset + size.width <= available.width &&
           offset + cursor + size.height <= available.height;
  }
  return offset + size.height <= available.height &&
         offset + cursor + size.width <= available.width;
}

class MacOverlayPlatform final : public OverlayPlatform {
 public:
  explicit MacOverlayPlatform(OverlayPlatformEvents events)
      : events_(std::move(events)),
        panels_([[NSMutableDictionary alloc] init]) {}

  ~MacOverlayPlatform() override { stop(); }

  void update(const OverlaySnapshot& snapshot) override {
    require_main_thread();

    std::unordered_map<std::string, OverlayHost> hosts;
    for (const auto& host : snapshot.hosts) hosts.emplace(host.id, host);

    std::unordered_set<std::string> active_ids;
    std::unordered_set<std::string> visible_ids;
    std::unordered_map<std::string, double> cursors;
    for (const auto& presentation : snapshot.presentations) {
      active_ids.insert(presentation.id);
      const auto host = hosts.find(presentation.host_id);
      if (host == hosts.end()) continue;

      NSString* identifier = ns_string(presentation.id);
      NativekitOverlayPanel* panel = panels_[identifier];
      const bool created = panel == nil;
      if (created) {
        panel = [[NativekitOverlayPanel alloc]
            initWithContentRect:NSMakeRect(0, 0, 64, 64)
                      styleMask:NSWindowStyleMaskBorderless |
                                NSWindowStyleMaskNonactivatingPanel
                        backing:NSBackingStoreBuffered
                          defer:NO];
        panel.level = NSFloatingWindowLevel;
        panel.opaque = NO;
        panel.backgroundColor = NSColor.clearColor;
        panel.hasShadow = YES;
        panel.hidesOnDeactivate = NO;
        panel.releasedWhenClosed = NO;
        panel.becomesKeyOnlyIfNeeded = YES;
        panel.movable = YES;
        panel.collectionBehavior =
            NSWindowCollectionBehaviorCanJoinAllSpaces |
            NSWindowCollectionBehaviorStationary |
            NSWindowCollectionBehaviorIgnoresCycle |
            NSWindowCollectionBehaviorFullScreenAuxiliary;
        const std::string presentation_id = presentation.id;
        auto* content_view = [[NativekitOverlayView alloc]
            initWithActivate:events_.activate
                      control:events_.control
                        moved:[this, presentation_id](NSRect frame) {
                          manual_frames_.insert_or_assign(
                              presentation_id, frame);
                        }];
        panel.contentView = content_view;
        panel.delegate = content_view;
        panels_[identifier] = panel;
      }

      NSImage* image = image_from_data_url(presentation.image_data);
      if (image == nil) {
        throw std::runtime_error("overlay image data could not be decoded");
      }
      auto* view = (NativekitOverlayView*)panel.contentView;
      NSString* icon_path = presentation.app_icon_path
                                ? ns_string(*presentation.app_icon_path)
                                : nil;
      [view setContentImage:image
                   iconPath:icon_path];
      [view setControls:snapshot.options.controls];
      panel.title = ns_string(host->second.title);

      const NSSize size = fitted_size(
          image, snapshot.max_size, screen_for_host(host->second));
      double& cursor = cursors[presentation.host_id];
      const bool eligible = presentation.visible && snapshot.visible;
      const bool stack_slot_fits =
          cursor == 0 || presentation_fits(host->second, size, cursor);
      auto manual_frame = manual_frames_.find(presentation.id);
      if (eligible && manual_frame != manual_frames_.end()) {
        NSRect frame = manual_frame->second;
        frame.size = size;
        NSScreen* screen = panel.screen ?: screen_for_frame(frame);
        if (screen == nil) screen = screen_for_host(host->second);
        frame = clamped_frame(frame, screen);
        manual_frame->second = frame;
        [view applyFrameFromLayout:frame];
        visible_ids.insert(presentation.id);
      } else if (eligible && stack_slot_fits) {
        const NSRect frame = frame_for_presentation(host->second, size, cursor);
        [view applyFrameFromLayout:frame];
        visible_ids.insert(presentation.id);
        // Visible panels are ordered after layout so the active session wins.
      } else {
        [panel orderOut:nil];
      }
      if (eligible &&
          (stack_slot_fits || manual_frame != manual_frames_.end())) {
        cursor += (host->second.anchor.edge == AnchorEdge::kLeading ||
                   host->second.anchor.edge == AnchorEdge::kTrailing)
                      ? size.height + 12
                      : size.width + 12;
      }
    }

    for (auto iterator = snapshot.presentations.rbegin();
         iterator != snapshot.presentations.rend(); ++iterator) {
      if (visible_ids.find(iterator->id) == visible_ids.end()) continue;
      NativekitOverlayPanel* panel = panels_[ns_string(iterator->id)];
      if (panel != nil) [panel orderFrontRegardless];
    }

    for (NSString* identifier in panels_.allKeys.copy) {
      if (active_ids.find(identifier.UTF8String ?: "") != active_ids.end()) {
        continue;
      }
      NativekitOverlayPanel* panel = panels_[identifier];
      [panel orderOut:nil];
      [panel close];
      [panels_ removeObjectForKey:identifier];
      manual_frames_.erase(identifier.UTF8String ?: "");
    }
  }

  void stop() override {
    if (panels_ == nil) return;
    if (![NSThread isMainThread]) {
      auto* platform = this;
      dispatch_sync(dispatch_get_main_queue(), ^{
        platform->close_all();
      });
    } else {
      close_all();
    }
  }

 private:
  static void require_main_thread() {
    if (![NSThread isMainThread]) {
      throw std::runtime_error("overlay methods must run on the main thread");
    }
  }

  void close_all() {
    for (NativekitOverlayPanel* panel in panels_.allValues.copy) {
      [panel orderOut:nil];
      [panel close];
    }
    [panels_ removeAllObjects];
    manual_frames_.clear();
  }

  OverlayPlatformEvents events_;
  NSMutableDictionary<NSString*, NativekitOverlayPanel*>* panels_;
  std::unordered_map<std::string, NSRect> manual_frames_;
};

}  // namespace

std::unique_ptr<OverlayPlatform> create_overlay_platform(
    OverlayPlatformEvents events) {
  return std::make_unique<MacOverlayPlatform>(std::move(events));
}

}  // namespace nativekit::platform
