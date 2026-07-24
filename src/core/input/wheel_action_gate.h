#pragma once

#include <chrono>
#include <optional>

// Collapses a rapid burst of wheel/axis steps into a single discrete action.
// Events closer than kBurstGap are treated as one flick (first step wins).
// Deliberate notches spaced farther apart each get their own action.
class WheelActionGate {
public:
  // Mechanical notches are typically ~80–120ms apart; free-spin / hi-res bursts
  // land much tighter. Stay under that so normal scrolling stays responsive.
  static constexpr auto kBurstGap = std::chrono::milliseconds(50);

  [[nodiscard]] bool tryConsume(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
    const bool sameBurst = m_lastEvent.has_value() && (now - *m_lastEvent) < kBurstGap;
    m_lastEvent = now;
    if (sameBurst) {
      return false;
    }
    return true;
  }

  void reset() noexcept { m_lastEvent.reset(); }

private:
  std::optional<std::chrono::steady_clock::time_point> m_lastEvent;
};
