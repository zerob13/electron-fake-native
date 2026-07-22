#pragma once

#include <napi.h>

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

#include "common/types.h"

namespace nativekit {

inline Napi::Value optional_string_to_js(
    Napi::Env env,
    const std::optional<std::string>& value) {
  return value ? Napi::String::New(env, *value) : env.Null();
}

inline Napi::Object rect_to_js(Napi::Env env, const Rect& rect) {
  Napi::Object result = Napi::Object::New(env);
  result.Set("x", rect.x);
  result.Set("y", rect.y);
  result.Set("width", rect.width);
  result.Set("height", rect.height);
  return result;
}

inline Napi::Object system_window_to_js(
    Napi::Env env,
    const SystemWindow& window) {
  Napi::Object result = Napi::Object::New(env);
  result.Set("id", static_cast<double>(window.id));
  result.Set("name", optional_string_to_js(env, window.name));
  result.Set("bounds", rect_to_js(env, window.bounds));
  result.Set("level", window.level);
  result.Set("ownerPid", window.owner_pid);
  result.Set("ownerName", optional_string_to_js(env, window.owner_name));
  result.Set("isOnscreen", window.is_onscreen);
  return result;
}

inline Napi::Object frontmost_window_to_js(
    Napi::Env env,
    const FrontmostWindow& window) {
  Napi::Object result = Napi::Object::New(env);
  result.Set("bundleId", window.bundle_id);
  result.Set("icon", optional_string_to_js(env, window.icon));
  result.Set("name", window.name);
  result.Set("title", optional_string_to_js(env, window.title));
  return result;
}

inline WindowId window_id_arg(
    const Napi::CallbackInfo& info,
    std::size_t index,
    const char* name,
    bool allow_zero = false) {
  if (info.Length() <= index || !info[index].IsNumber()) {
    throw std::invalid_argument(std::string(name) + " must be a number");
  }
  const double value = info[index].As<Napi::Number>().DoubleValue();
  constexpr double kMaxSafeInteger = 9007199254740991.0;
  if (!std::isfinite(value) || value < (allow_zero ? 0 : 1) ||
      value > kMaxSafeInteger ||
      value != std::floor(value)) {
    throw std::invalid_argument(
        std::string(name) + " must be a valid window id");
  }
  return static_cast<WindowId>(value);
}

inline void throw_js_error(Napi::Env env, const std::exception& error) {
  Napi::Error::New(env, error.what()).ThrowAsJavaScriptException();
}

inline void throw_js_type_error(
    Napi::Env env,
    const std::invalid_argument& error) {
  Napi::TypeError::New(env, error.what()).ThrowAsJavaScriptException();
}

}  // namespace nativekit
