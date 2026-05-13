#pragma once

#include "config/config_service.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class WaylandConnection;
struct ext_idle_notification_v1;
struct ext_idle_notifier_v1;

class IdleManager {
public:
  /// `command` is the resolved idle or resume shell / `noctalia:` token for this behavior.
  using CommandRunner = std::function<bool(const IdleBehaviorConfig& behavior, const std::string& command)>;
  /// Starts the pre-action fade overlay; call `onFadeComplete` once every output has finished fading.
  using GraceBeginCallback = std::function<void(const std::string& behaviorName, std::chrono::milliseconds fadeDuration,
                                                std::function<void()> onFadeComplete)>;
  /// `userCancelled` is true when input resumed during the fade before the idle command ran.
  using GraceEndCallback = std::function<void(bool userCancelled)>;

  IdleManager();
  ~IdleManager();

  IdleManager(const IdleManager&) = delete;
  IdleManager& operator=(const IdleManager&) = delete;

  bool initialize(WaylandConnection& wayland, GraceBeginCallback onBegin, GraceEndCallback onEnd);
  void setCommandRunner(CommandRunner runner);
  void reload(const IdleConfig& config);
  static void handleIdled(void* data, ext_idle_notification_v1* notification);
  static void handleResumed(void* data, ext_idle_notification_v1* notification);

private:
  struct BehaviorState {
    IdleManager* owner = nullptr;
    IdleBehaviorConfig config;
    ext_idle_notification_v1* notification = nullptr;
    bool idled = false;
  };

  void clearBehaviors();
  void createBehavior(const IdleBehaviorConfig& config);
  void runBehavior(BehaviorState& behavior);
  void runResumeBehavior(BehaviorState& behavior);
  bool runCommand(const IdleBehaviorConfig& behavior, const std::string& command) const;
  void cancelActiveGrace(bool userCancelled);
  void graceFadeComplete();

  WaylandConnection* m_wayland = nullptr;
  ext_idle_notifier_v1* m_notifier = nullptr;
  CommandRunner m_commandRunner;
  GraceBeginCallback m_onGraceBegin;
  GraceEndCallback m_onGraceEnd;
  IdleConfig m_idleConfig;
  BehaviorState* m_graceBehavior = nullptr;
  std::vector<std::unique_ptr<BehaviorState>> m_behaviors;
};
