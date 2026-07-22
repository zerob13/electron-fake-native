#pragma once

#include <napi.h>

#include <optional>
#include <vector>

#include "common/types.h"

namespace nativekit {

namespace platform {

std::optional<FrontmostWindow> frontmost_window();
std::vector<SystemWindow> list_windows(WindowId relative_to);
std::optional<SystemWindow> find_window(WindowId id);
std::optional<SystemWindow> window_at_point(Point point, WindowId below_id);

}  // namespace platform

void register_window_query(Napi::Env env, Napi::Object& exports);

}  // namespace nativekit
