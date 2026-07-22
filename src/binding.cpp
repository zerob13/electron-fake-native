#include <napi.h>

#include "overlay/overlay_manager.h"
#include "windows/window_query.h"

namespace nativekit {

Napi::Object init(Napi::Env env, Napi::Object exports) {
  register_window_query(env, exports);
  register_overlay(env, exports);
  napi_add_env_cleanup_hook(
      env,
      [](void*) { cleanup_overlay(); },
      nullptr);
  return exports;
}

}  // namespace nativekit

Napi::Object init_nativekit(Napi::Env env, Napi::Object exports) {
  return nativekit::init(env, exports);
}

NODE_API_MODULE(nativekit, init_nativekit)
