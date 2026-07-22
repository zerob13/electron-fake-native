#include <napi.h>

#include "apps/icon.h"
#include "overlay/overlay_manager.h"
#include "windows/window_query.h"

#if defined(__linux__)
#include "common/linux/image_utils.h"
#endif

namespace nativekit {

Napi::Object init(Napi::Env env, Napi::Object exports) {
#if defined(__linux__)
  // Electron loads the addon before creating BrowserWindows. Preparing GTK at
  // this point avoids initializing it from a nested Chromium UI callback.
  platform::prepare_gtk();
#endif
  register_window_query(env, exports);
  register_overlay(env, exports);
  register_apps(env, exports);
  napi_add_env_cleanup_hook(
      env,
      [](void*) noexcept {
        cleanup_overlay();
      },
      nullptr);
  return exports;
}

}  // namespace nativekit

Napi::Object init_nativekit(Napi::Env env, Napi::Object exports) {
  return nativekit::init(env, exports);
}

NODE_API_MODULE(nativekit, init_nativekit)
