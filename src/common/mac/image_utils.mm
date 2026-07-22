#import "common/mac/image_utils.h"

namespace nativekit::platform {

std::optional<std::string> image_to_png_data_url(
    NSImage* image,
    double point_size) {
  if (image == nil || point_size <= 0) return std::nullopt;

  const NSInteger pixels = static_cast<NSInteger>(point_size);
  NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:nullptr
                    pixelsWide:pixels
                    pixelsHigh:pixels
                 bitsPerSample:8
               samplesPerPixel:4
                      hasAlpha:YES
                      isPlanar:NO
                colorSpaceName:NSCalibratedRGBColorSpace
                   bytesPerRow:0
                  bitsPerPixel:0];
  if (bitmap == nil) return std::nullopt;

  [NSGraphicsContext saveGraphicsState];
  NSGraphicsContext.currentContext =
      [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
  [image drawInRect:NSMakeRect(0, 0, pixels, pixels)
           fromRect:NSZeroRect
          operation:NSCompositingOperationSourceOver
           fraction:1.0
     respectFlipped:YES
              hints:@{NSImageHintInterpolation : @(NSImageInterpolationHigh)}];
  [NSGraphicsContext restoreGraphicsState];

  NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG
                                     properties:@{}];
  if (png == nil) return std::nullopt;
  NSString* encoded = [png base64EncodedStringWithOptions:0];
  return "data:image/png;base64," + std::string(encoded.UTF8String);
}

NSImage* image_from_data_url(const std::string& data_url) {
  const std::size_t separator = data_url.find(',');
  if (separator == std::string::npos) return nil;
  NSString* encoded = [NSString
      stringWithUTF8String:data_url.substr(separator + 1).c_str()];
  NSData* data = [[NSData alloc] initWithBase64EncodedString:encoded options:0];
  return data == nil ? nil : [[NSImage alloc] initWithData:data];
}

}  // namespace nativekit::platform
