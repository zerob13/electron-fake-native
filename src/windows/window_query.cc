#include "windows/window_query.h"

#include <exception>

#include "common/napi_helpers.h"

namespace nativekit {
namespace {

Napi::Value frontmost(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  try {
    const auto window = platform::frontmost_window();
    return window ? frontmost_window_to_js(env, *window) : env.Null();
  } catch (const std::invalid_argument& error) {
    throw_js_type_error(env, error);
    return env.Undefined();
  } catch (const std::exception& error) {
    throw_js_error(env, error);
    return env.Undefined();
  }
}

Napi::Value list(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  try {
    const WindowId relative_to = window_id_arg(info, 0, "relativeTo", true);
    const auto windows = platform::list_windows(relative_to);
    Napi::Array result = Napi::Array::New(env, windows.size());
    for (std::size_t index = 0; index < windows.size(); ++index) {
      result.Set(index, system_window_to_js(env, windows[index]));
    }
    return result;
  } catch (const std::invalid_argument& error) {
    throw_js_type_error(env, error);
    return env.Undefined();
  } catch (const std::exception& error) {
    throw_js_error(env, error);
    return env.Undefined();
  }
}

Napi::Value find(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  try {
    const auto window = platform::find_window(window_id_arg(info, 0, "id"));
    return window ? system_window_to_js(env, *window) : env.Null();
  } catch (const std::invalid_argument& error) {
    throw_js_type_error(env, error);
    return env.Undefined();
  } catch (const std::exception& error) {
    throw_js_error(env, error);
    return env.Undefined();
  }
}

Napi::Value at_point(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  try {
    if (info.Length() < 1 || !info[0].IsObject()) {
      throw std::invalid_argument("point must be an object");
    }
    const Napi::Object point_value = info[0].As<Napi::Object>();
    if (!point_value.Get("x").IsNumber() ||
        !point_value.Get("y").IsNumber()) {
      throw std::invalid_argument("point.x and point.y must be numbers");
    }
    const Point point{
        point_value.Get("x").As<Napi::Number>().DoubleValue(),
        point_value.Get("y").As<Napi::Number>().DoubleValue(),
    };
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
      throw std::invalid_argument("point.x and point.y must be finite");
    }
    const WindowId below_id = window_id_arg(info, 1, "belowId", true);
    const auto window = platform::window_at_point(point, below_id);
    return window ? system_window_to_js(env, *window) : env.Null();
  } catch (const std::invalid_argument& error) {
    throw_js_type_error(env, error);
    return env.Undefined();
  } catch (const std::exception& error) {
    throw_js_error(env, error);
    return env.Undefined();
  }
}

}  // namespace

void register_window_query(Napi::Env env, Napi::Object& exports) {
  exports.Set("windowsFrontmost", Napi::Function::New(env, frontmost));
  exports.Set("windowsList", Napi::Function::New(env, list));
  exports.Set("windowsFind", Napi::Function::New(env, find));
  exports.Set("windowsAtPoint", Napi::Function::New(env, at_point));
}

}  // namespace nativekit
