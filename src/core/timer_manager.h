#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

// SteadyMonotonic: steady_clock (pauses in S3 on Linux).
// IncludesSystemSleep: CLOCK_BOOTTIME on Linux only; FreeBSD remaps to steady (same file).
enum class TimerDeadline : std::uint8_t {
  SteadyMonotonic,
  IncludesSystemSleep,
};

class TimerManager {
public:
  using TimerId = std::uint64_t;

  static TimerManager& instance();

  TimerId start(TimerId existingId, std::chrono::milliseconds delay, std::function<void()> callback,
                bool repeating = false, TimerDeadline deadline = TimerDeadline::SteadyMonotonic);
  bool cancel(TimerId id);
  [[nodiscard]] bool active(TimerId id) const noexcept;
  [[nodiscard]] int pollTimeoutMs() const;
  void tick();

private:
  TimerManager() = default;
};

class Timer {
public:
  Timer() = default;
  ~Timer();

  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;

  Timer(Timer&& other) noexcept;
  Timer& operator=(Timer&& other) noexcept;

  void start(std::chrono::milliseconds delay, std::function<void()> callback,
             TimerDeadline deadline = TimerDeadline::SteadyMonotonic);
  void startRepeating(std::chrono::milliseconds interval, std::function<void()> callback,
                      TimerDeadline deadline = TimerDeadline::SteadyMonotonic);
  void stop();
  [[nodiscard]] bool active() const noexcept { return TimerManager::instance().active(m_id); }

private:
  TimerManager::TimerId m_id = 0;
};
