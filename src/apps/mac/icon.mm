#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include "apps/icon.h"

#include "common/mac/image_utils.h"

namespace nativekit::platform {

std::optional<std::string> app_icon(
    const std::string& app_path,
    int pixels) {
  NSString* path = [NSString stringWithUTF8String:app_path.c_str()];
  if (path == nil ||
      ![[NSFileManager defaultManager] fileExistsAtPath:path]) {
    return std::nullopt;
  }
  NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:path];
  return image_to_png_data_url(icon, pixels);
}

}  // namespace nativekit::platform
