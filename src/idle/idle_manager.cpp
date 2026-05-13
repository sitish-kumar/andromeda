#include "idle/idle_manager.h"

#include "config/config_types.h"
#include "core/deferred_call.h"
#include "core/log.h"
#include "ext-idle-notify-v1-client-protocol.h"
#include "wayland/wayland_connection.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace {

  constexpr Logger kLog("idle");

  const ext_idle_notification_v1_listener kIdleNotificationListener = {
      .idled = &IdleManager::handleIdled,
      .resumed = &IdleManager::handleResumed,
  };

} // namespace

IdleManager::IdleManager() = default;

IdleManager::~IdleManager() { clearBehaviors(); }

bool IdleManager::initialize(WaylandConnection& wayland, GraceBeginCallback onBegin, GraceEndCallback onEnd) {
  m_wayland = &wayland;
  m_onGraceBegin = std::move(onBegin);
  m_onGraceEnd = std::move(onEnd);
  m_notifier = m_wayland->idleNotifier();
  if (m_notifier == nullptr) {
    kLog.info("idle notify protocol unavailable");
  }
  return true;
}

void IdleManager::setActionRunner(ActionRunner runner) { m_actionRunner = std::move(runner); }

void IdleManager::reload(const IdleConfig& config) {
  clearBehaviors();
  m_idleConfig = config;

  if (m_notifier == nullptr) {
    return;
  }
  if (m_wayland == nullptr || m_wayland->seat() == nullptr) {
    kLog.warn("cannot register idle behaviors without a Wayland seat");
    return;
  }

  for (const auto& behavior : config.behaviors) {
    createBehavior(behavior);
  }
}

void IdleManager::clearBehaviors() {
  cancelActiveGrace(false);
  for (auto& behavior : m_behaviors) {
    if (behavior->notification != nullptr) {
      ext_idle_notification_v1_destroy(behavior->notification);
      behavior->notification = nullptr;
    }
  }
  m_behaviors.clear();
}

void IdleManager::createBehavior(const IdleBehaviorConfig& config) {
  if (!config.enabled) {
    return;
  }
  if (config.timeoutSeconds < 0) {
    kLog.warn("idle behavior '{}' ignored: timeout must be >= 0 seconds", config.name);
    return;
  }
  if (config.timeoutSeconds == 0) {
    kLog.debug("idle behavior '{}' disabled by zero timeout", config.name);
    return;
  }
  const ResolvedIdleBehavior resolved = resolveIdleBehaviorActions(config);
  if (resolved.idleAction.kind == IdleActionKind::None) {
    kLog.warn("idle behavior '{}' ignored: needs an action", config.name);
    return;
  }

  auto behavior = std::make_unique<BehaviorState>();
  behavior->owner = this;
  behavior->config = config;
  const auto timeoutMs = static_cast<std::uint32_t>(config.timeoutSeconds) * 1000u;
  behavior->notification = ext_idle_notifier_v1_get_idle_notification(m_notifier, timeoutMs, m_wayland->seat());
  if (behavior->notification == nullptr) {
    kLog.warn("failed to register idle behavior '{}'", config.name);
    return;
  }

  ext_idle_notification_v1_add_listener(behavior->notification, &kIdleNotificationListener, behavior.get());
  kLog.info("registered idle behavior '{}' timeout={}s", config.name, config.timeoutSeconds);
  m_behaviors.push_back(std::move(behavior));
}

void IdleManager::runBehavior(BehaviorState& behavior) {
  const ResolvedIdleBehavior resolved = resolveIdleBehaviorActions(behavior.config);
  if (!runAction(behavior.config, resolved.idleAction)) {
    kLog.warn("idle behavior '{}' action failed", behavior.config.name);
  }
}

void IdleManager::runResumeBehavior(BehaviorState& behavior) {
  const ResolvedIdleBehavior resolved = resolveIdleBehaviorActions(behavior.config);
  if (resolved.resumeAction.kind == IdleActionKind::None) {
    return;
  }
  if (!runAction(behavior.config, resolved.resumeAction)) {
    kLog.warn("idle behavior '{}' resume action failed", behavior.config.name);
  }
}

bool IdleManager::runAction(const IdleBehaviorConfig& behavior, const IdleActionRequest& action) const {
  if (action.kind == IdleActionKind::None) {
    return true;
  }
  if (!m_actionRunner) {
    return false;
  }
  return m_actionRunner(behavior, action);
}

void IdleManager::cancelActiveGrace(bool userCancelled) {
  if (m_graceBehavior == nullptr) {
    return;
  }
  m_graceBehavior->phase = BehaviorPhase::Waiting;
  m_graceBehavior = nullptr;
  if (m_onGraceEnd) {
    m_onGraceEnd(userCancelled);
  }
}

void IdleManager::graceFadeComplete() {
  if (m_graceBehavior == nullptr) {
    return;
  }
  BehaviorState* behavior = m_graceBehavior;
  m_graceBehavior = nullptr;
  if (m_onGraceEnd) {
    m_onGraceEnd(false);
  }
  behavior->phase = BehaviorPhase::Idled;
  kLog.info("idle behavior '{}' triggered after pre-action fade", behavior->config.name);
  runBehavior(*behavior);
}

void IdleManager::handleIdled(void* data, ext_idle_notification_v1* /*notification*/) {
  auto* behavior = static_cast<BehaviorState*>(data);
  if (behavior == nullptr || behavior->owner == nullptr) {
    return;
  }
  if (behavior->phase != BehaviorPhase::Waiting) {
    return;
  }

  IdleManager& self = *behavior->owner;

  if (self.m_graceBehavior != nullptr && self.m_graceBehavior != behavior) {
    BehaviorState* pending = self.m_graceBehavior;
    self.m_graceBehavior = nullptr;
    if (self.m_onGraceEnd) {
      self.m_onGraceEnd(false);
    }
    pending->phase = BehaviorPhase::Idled;
    kLog.info("idle behavior '{}' triggered (superseded pending pre-action fade)", pending->config.name);
    self.runBehavior(*pending);
  }

  const float fadeSec = self.m_idleConfig.preActionFadeSeconds;
  if (fadeSec > 0.0005f) {
    assert(self.m_onGraceBegin);
    self.m_graceBehavior = behavior;
    const int fadeMs = static_cast<int>(std::lround(static_cast<double>(fadeSec) * 1000.0));
    const auto fade = std::chrono::milliseconds(std::clamp(fadeMs, 1, 600000));
    kLog.info("idle behavior '{}' pre-action fade {}ms", behavior->config.name, fade.count());
    behavior->phase = BehaviorPhase::Fading;
    BehaviorState* b = behavior;
    self.m_onGraceBegin(behavior->config.name, fade, [ptr = &self, b]() {
      DeferredCall::callLater([ptr, b]() {
        if (ptr->m_graceBehavior != b) {
          return;
        }
        ptr->graceFadeComplete();
      });
    });
    return;
  }

  behavior->phase = BehaviorPhase::Idled;
  kLog.info("idle behavior '{}' triggered", behavior->config.name);
  self.runBehavior(*behavior);
}

void IdleManager::handleResumed(void* data, ext_idle_notification_v1* /*notification*/) {
  auto* behavior = static_cast<BehaviorState*>(data);
  if (behavior == nullptr || behavior->owner == nullptr) {
    return;
  }

  IdleManager& self = *behavior->owner;
  if (behavior == self.m_graceBehavior && behavior->phase == BehaviorPhase::Fading) {
    self.m_graceBehavior = nullptr;
    behavior->phase = BehaviorPhase::Waiting;
    kLog.info("idle behavior '{}' pre-action fade cancelled (user active)", behavior->config.name);
    if (self.m_onGraceEnd) {
      self.m_onGraceEnd(true);
    }
    return;
  }

  if (behavior->phase != BehaviorPhase::Idled) {
    return;
  }

  behavior->phase = BehaviorPhase::Waiting;
  kLog.info("idle behavior '{}' resumed", behavior->config.name);
  self.runResumeBehavior(*behavior);
}
