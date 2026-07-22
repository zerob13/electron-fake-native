#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace nativekit {

using WindowId = std::uint64_t;

struct Point {
  double x = 0;
  double y = 0;
};

struct Rect {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;

  [[nodiscard]] bool contains(const Point& point) const {
    return point.x >= x && point.y >= y && point.x < x + width &&
           point.y < y + height;
  }
};

struct SystemWindow {
  WindowId id = 0;
  std::optional<std::string> name;
  Rect bounds;
  int level = 0;
  std::uint32_t owner_pid = 0;
  std::optional<std::string> owner_name;
  bool is_onscreen = false;
};

struct FrontmostWindow {
  std::string bundle_id;
  std::optional<std::string> icon;
  std::string name;
  std::optional<std::string> title;
};

}  // namespace nativekit
