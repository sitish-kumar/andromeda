#pragma once

#include <optional>
#include <string>

class NiriOutputBackend {
public:
  [[nodiscard]] std::optional<std::string> focusedOutputName() const;
};

namespace compositors::niri {

  [[nodiscard]] bool setOutputPower(bool on);

} // namespace compositors::niri
