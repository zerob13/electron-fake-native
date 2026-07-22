#pragma once

#ifdef __OBJC__
#import <AppKit/AppKit.h>

#include <optional>
#include <string>

namespace nativekit::platform {

std::optional<std::string> image_to_png_data_url(
    NSImage* image,
    double point_size);
NSImage* image_from_data_url(const std::string& data_url);

}  // namespace nativekit::platform
#endif
