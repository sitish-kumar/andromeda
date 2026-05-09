#pragma once

#include <optional>
#include <string>
#include <vector>

struct KeyboardLayoutState {
  std::vector<std::string> names;
  int currentIndex = -1;
};

class KeyboardLayoutBackend {
public:
  virtual ~KeyboardLayoutBackend() = default;

  [[nodiscard]] virtual bool isAvailable() const noexcept = 0;
  [[nodiscard]] virtual bool cycleLayout() const = 0;
  [[nodiscard]] virtual std::optional<KeyboardLayoutState> layoutState() const = 0;
  [[nodiscard]] virtual std::optional<std::string> currentLayoutName() const = 0;
};
