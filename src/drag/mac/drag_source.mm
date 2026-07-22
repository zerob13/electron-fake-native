#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#include "drag/drag_source.h"

#include <memory>
#include <mutex>
#include <utility>

typedef void (^NativekitDragEndedHandler)(
    NSPoint point,
    NSDragOperation operation);

@interface NativekitDragSource : NSObject <NSDraggingSource>

@property(nonatomic, copy) NativekitDragEndedHandler endedHandler;

@end

@implementation NativekitDragSource

- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
  return NSDragOperationCopy;
}

- (BOOL)ignoreModifierKeysForDraggingSession:(NSDraggingSession*)session {
  return YES;
}

- (void)draggingSession:(NSDraggingSession*)session
           endedAtPoint:(NSPoint)screenPoint
              operation:(NSDragOperation)operation {
  if (self.endedHandler != nil) {
    self.endedHandler(screenPoint, operation);
  }
}

@end

namespace nativekit::platform {
namespace {

struct MacDragState {
  std::mutex mutex;
  bool active = false;
  DragEvents events;
};

Point screen_point(NSPoint point) {
  const CGRect main_bounds = CGDisplayBounds(CGMainDisplayID());
  return {point.x, CGRectGetHeight(main_bounds) - point.y};
}

class MacDragPlatform final : public DragPlatform {
 public:
  explicit MacDragPlatform(DragEvents events)
      : state_(std::make_shared<MacDragState>()) {
    state_->events = std::move(events);
  }

  ~MacDragPlatform() override { stop(); }

  bool start(const DragRequest& request) override {
    if (![NSThread isMainThread]) return false;

    NSView* view = (__bridge NSView*)(
        reinterpret_cast<void*>(request.window_handle));
    NSWindow* window = view.window;
    if (view == nil || window == nil) return false;

    NSMutableArray<NSDraggingItem*>* items =
        [[NSMutableArray alloc] initWithCapacity:request.files.size()];
    NSFileManager* file_manager = [NSFileManager defaultManager];
    const NSPoint location = NSMakePoint(
        request.position.x,
        view.isFlipped ? request.position.y
                       : NSHeight(view.bounds) - request.position.y);
    if (!NSPointInRect(location, view.bounds)) return false;
    for (const auto& file : request.files) {
      NSString* path = [NSString stringWithUTF8String:file.c_str()];
      if (path == nil || !path.isAbsolutePath ||
          ![file_manager fileExistsAtPath:path]) {
        return false;
      }
      NSURL* url = [NSURL fileURLWithPath:path];
      NSDraggingItem* item =
          [[NSDraggingItem alloc] initWithPasteboardWriter:url];
      NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:path];
      [item setDraggingFrame:NSMakeRect(location.x - 16,
                                        location.y - 16,
                                        32,
                                        32)
                   contents:icon];
      [items addObject:item];
    }

    {
      std::lock_guard lock(state_->mutex);
      if (state_->active) return false;
      state_->active = true;
    }

    const NSPoint window_location = [view convertPoint:location toView:nil];
    NSEvent* event = [NSEvent
        mouseEventWithType:NSEventTypeLeftMouseDragged
                  location:window_location
             modifierFlags:NSEvent.modifierFlags
                 timestamp:NSProcessInfo.processInfo.systemUptime
              windowNumber:window.windowNumber
                   context:nil
               eventNumber:0
                clickCount:1
                  pressure:1.0];
    if (event == nil) {
      mark_inactive();
      return false;
    }

    auto state = state_;
    NativekitDragSource* source = [[NativekitDragSource alloc] init];
    source.endedHandler = ^(NSPoint point, NSDragOperation operation) {
      DragEvents events;
      {
        std::lock_guard lock(state->mutex);
        if (!state->active) return;
        state->active = false;
        events = state->events;
      }
      if (events.ended) {
        events.ended({operation != NSDragOperationNone, screen_point(point)});
      }
    };
    source_ = source;
    NSDraggingSession* session =
        [view beginDraggingSessionWithItems:items event:event source:source];
    if (session == nil) {
      source_ = nil;
      mark_inactive();
      return false;
    }
    session.animatesToStartingPositionsOnCancelOrFail = YES;
    return true;
  }

  void stop() override {
    if ([NSThread isMainThread]) {
      clear();
    } else {
      auto* platform = this;
      dispatch_sync(dispatch_get_main_queue(), ^{
        platform->clear();
      });
    }
  }

 private:
  void clear() {
    {
      std::lock_guard lock(state_->mutex);
      state_->active = false;
      state_->events = {};
    }
    source_ = nil;
  }

  void mark_inactive() {
    std::lock_guard lock(state_->mutex);
    state_->active = false;
  }

  std::shared_ptr<MacDragState> state_;
  NativekitDragSource* source_ = nil;
};

}  // namespace

std::unique_ptr<DragPlatform> create_drag_platform(DragEvents events) {
  return std::make_unique<MacDragPlatform>(std::move(events));
}

}  // namespace nativekit::platform
