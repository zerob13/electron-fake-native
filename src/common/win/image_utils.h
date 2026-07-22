#pragma once

#ifdef _WIN32

#include <optional>
#include <string>

namespace nativekit::platform {

std::optional<std::string> icon_to_png_data_url(
    const std::wstring& path,
    int pixels);

}  // namespace nativekit::platform

#endif
