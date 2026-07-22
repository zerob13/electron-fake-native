#pragma once

#include <napi.h>

#include <optional>
#include <string>

namespace nativekit {

namespace platform {

std::optional<std::string> app_icon(
    const std::string& app_path,
    int pixels);

}  // namespace platform

void register_apps(Napi::Env env, Napi::Object& exports);

}  // namespace nativekit
