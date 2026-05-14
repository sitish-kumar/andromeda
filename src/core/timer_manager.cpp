#include "core/timer_manager.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(__linux__)
#include <time.h>
#endif

namespace {

  // Linux: CLOCK_BOOTTIME advances across S3. FreeBSD: not wired here; callers remap to steady.
  std::int64_t boottimeNowNanoseconds() {
#if defined(__linux__)
    timespec ts{};
    if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0) {
      return static_cast<std::int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
    }
#endif
    return 0;
  }

  struct TimerEntry {
    TimerManager::TimerId id = 0;
    TimerDeadline deadlineKind = TimerDeadline::SteadyMonotonic;
    std::chrono::steady_clock::time_point steadyDue{};
    std::int64_t boottimeDueNs = 0;
    std::chrono::milliseconds interval{0};
    std::function<void()> callback;
    bool repeating = false;
  };

  [[nodiscard]] bool entryIsDue(const TimerEntry& entry, std::chrono::steady_clock::time_point steadyNow) {
    if (entry.deadlineKind == TimerDeadline::SteadyMonotonic) {
      return entry.steadyDue <= steadyNow;
    }
    return entry.boottimeDueNs <= boottimeNowNanoseconds();
  }

  [[nodiscard]] std::int64_t entryRemainingMs(const TimerEntry& entry,
                                              std::chrono::steady_clock::time_point steadyNow) {
    if (entry.deadlineKind == TimerDeadline::SteadyMonotonic) {
      if (entry.steadyDue <= steadyNow) {
        return 0;
      }
      return std::chrono::ceil<std::chrono::milliseconds>(entry.steadyDue - steadyNow).count();
    }
    const std::int64_t btNow = boottimeNowNanoseconds();
    if (entry.boottimeDueNs <= btNow) {
      return 0;
    }
    return (entry.boottimeDueNs - btNow + 999999LL) / 1000000LL;
  }

  std::unordered_map<TimerManager::TimerId, TimerEntry>& timerEntries() {
    static std::unordered_map<TimerManager::TimerId, TimerEntry> entries;
    return entries;
  }

  TimerManager::TimerId& nextTimerId() {
    static TimerManager::TimerId nextId = 1;
    return nextId;
  }

  std::unordered_set<TimerManager::TimerId>& canceledTimerIds() {
    static std::unordered_set<TimerManager::TimerId> ids;
    return ids;
  }

  std::unordered_set<TimerManager::TimerId>& inFlightTimerIds() {
    static std::unordered_set<TimerManager::TimerId> ids;
    return ids;
  }

} // namespace

TimerManager& TimerManager::instance() {
  static TimerManager manager;
  return manager;
}

TimerManager::TimerId TimerManager::start(TimerId existingId, std::chrono::milliseconds delay,
                                          std::function<void()> callback, bool repeating, TimerDeadline deadline) {
  if (existingId != 0) {
    cancel(existingId);
  }

  if (!callback) {
    return 0;
  }

  TimerDeadline effectiveDeadline = deadline;
#if !defined(__linux__)
#if !defined(__FreeBSD__)
#error "noctalia-shell: unsupported platform (expected Linux or FreeBSD)"
#endif
  // FreeBSD: IncludesSystemSleep not implemented (no CLOCK_BOOTTIME path); keep steady deadlines.
  if (deadline == TimerDeadline::IncludesSystemSleep) {
    effectiveDeadline = TimerDeadline::SteadyMonotonic;
  }
#endif

  const auto nonNegative = std::max(delay, std::chrono::milliseconds(0));

  TimerEntry entry;
  entry.id = 0; // filled below
  entry.deadlineKind = effectiveDeadline;
  entry.interval = nonNegative;
  entry.callback = std::move(callback);
  entry.repeating = repeating;

  if (effectiveDeadline == TimerDeadline::SteadyMonotonic) {
    entry.steadyDue = std::chrono::steady_clock::now() + nonNegative;
  } else {
    const auto delayNs = std::chrono::duration_cast<std::chrono::nanoseconds>(nonNegative).count();
    entry.boottimeDueNs = boottimeNowNanoseconds() + delayNs;
  }

  const TimerId id = nextTimerId()++;
  entry.id = id;
  timerEntries()[id] = std::move(entry);
  return id;
}

bool TimerManager::cancel(TimerId id) {
  if (id == 0) {
    return false;
  }

  if (timerEntries().erase(id) > 0) {
    return true;
  }

  if (inFlightTimerIds().contains(id)) {
    canceledTimerIds().insert(id);
    return true;
  }

  return false;
}

bool TimerManager::active(TimerId id) const noexcept {
  if (id == 0 || canceledTimerIds().contains(id)) {
    return false;
  }
  return timerEntries().contains(id) || inFlightTimerIds().contains(id);
}

int TimerManager::pollTimeoutMs() const {
  if (timerEntries().empty()) {
    return -1;
  }

  const auto steadyNow = std::chrono::steady_clock::now();
  std::int64_t bestMs = std::numeric_limits<std::int64_t>::max();
  for (const auto& [id, entry] : timerEntries()) {
    (void)id;
    bestMs = std::min(bestMs, entryRemainingMs(entry, steadyNow));
  }

  if (bestMs <= 0) {
    return 0;
  }

  return static_cast<int>(std::min<std::int64_t>(bestMs, std::numeric_limits<int>::max()));
}

void TimerManager::tick() {
  if (timerEntries().empty()) {
    return;
  }

  const auto steadyNow = std::chrono::steady_clock::now();
  std::vector<TimerEntry> dueEntries;
  dueEntries.reserve(timerEntries().size());

  for (auto it = timerEntries().begin(); it != timerEntries().end();) {
    if (!entryIsDue(it->second, steadyNow)) {
      ++it;
      continue;
    }

    inFlightTimerIds().insert(it->second.id);
    dueEntries.push_back(std::move(it->second));
    it = timerEntries().erase(it);
  }

  for (auto& entry : dueEntries) {
    if (canceledTimerIds().contains(entry.id)) {
      canceledTimerIds().erase(entry.id);
      inFlightTimerIds().erase(entry.id);
      continue;
    }

    if (entry.callback) {
      entry.callback();
    }

    if (entry.repeating && entry.id != 0 && !canceledTimerIds().contains(entry.id)) {
      TimerEntry next = entry;
      if (next.deadlineKind == TimerDeadline::SteadyMonotonic) {
        next.steadyDue = std::chrono::steady_clock::now() + next.interval;
      } else {
        const auto intervalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(next.interval).count();
        next.boottimeDueNs = boottimeNowNanoseconds() + intervalNs;
      }
      timerEntries()[entry.id] = std::move(next);
    }

    canceledTimerIds().erase(entry.id);
    inFlightTimerIds().erase(entry.id);
  }
}

Timer::~Timer() { stop(); }

Timer::Timer(Timer&& other) noexcept : m_id(other.m_id) { other.m_id = 0; }

Timer& Timer::operator=(Timer&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  stop();
  m_id = other.m_id;
  other.m_id = 0;
  return *this;
}

void Timer::start(std::chrono::milliseconds delay, std::function<void()> callback, TimerDeadline deadline) {
  m_id = TimerManager::instance().start(m_id, delay, std::move(callback), false, deadline);
}

void Timer::startRepeating(std::chrono::milliseconds interval, std::function<void()> callback, TimerDeadline deadline) {
  m_id = TimerManager::instance().start(m_id, interval, std::move(callback), true, deadline);
}

void Timer::stop() {
  if (m_id == 0) {
    return;
  }
  TimerManager::instance().cancel(m_id);
  m_id = 0;
}
