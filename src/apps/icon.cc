#include "apps/icon.h"

#include <stdexcept>
#include <string>

#include "common/napi_helpers.h"

namespace nativekit {
namespace {

Napi::Value icon(const Napi::CallbackInfo& info) {
  try {
    if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString()) {
      throw std::invalid_argument("appPath and size must be strings");
    }
    const std::string app_path = info[0].As<Napi::String>().Utf8Value();
    if (app_path.empty() || app_path.find('\0') != std::string::npos) {
      throw std::invalid_argument("appPath must be a valid path");
    }
    const std::string size = info[1].As<Napi::String>().Utf8Value();
    if (size != "small" && size != "medium") {
      throw std::invalid_argument("size must be small or medium");
    }
    return optional_string_to_js(
        info.Env(), platform::app_icon(app_path, size == "small" ? 16 : 32));
  } catch (const std::invalid_argument& error) {
    throw_js_type_error(info.Env(), error);
  } catch (const std::exception& error) {
    throw_js_error(info.Env(), error);
  }
  return info.Env().Undefined();
}

}  // namespace

void register_apps(Napi::Env env, Napi::Object& exports) {
  exports.Set("appsIcon", Napi::Function::New(env, icon));
}

}  // namespace nativekit
