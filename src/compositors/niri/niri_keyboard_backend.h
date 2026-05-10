#pragma once

#include "compositors/keyboard_backend.h"

#include <optional>
#include <string>

namespace compositors::niri {
  class NiriRuntime;
} // namespace compositors::niri

class NiriKeyboardBackend {
public:
  explicit NiriKeyboardBackend(compositors::niri::NiriRuntime& runtime);

  [[nodiscard]] bool isAvailable() const noexcept;
  [[nodiscard]] bool cycleLayout() const;
  [[nodiscard]] std::optional<KeyboardLayoutState> layoutState() const;
  [[nodiscard]] std::optional<std::string> currentLayoutName() const;

private:
  compositors::niri::NiriRuntime& m_runtime;
};
