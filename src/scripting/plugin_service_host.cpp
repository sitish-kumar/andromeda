#include "scripting/plugin_service_host.h"

#include "core/log.h"
#include "scripting/plugin_ipc.h"
#include "scripting/plugin_manifest.h"
#include "scripting/plugin_registry.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace scripting {

  namespace {
    constexpr Logger kLog("plugin-service");

    std::string readFile(const std::filesystem::path& path) {
      std::ifstream file(path);
      if (!file) {
        return {};
      }
      std::stringstream ss;
      ss << file.rdbuf();
      return ss.str();
    }
  } // namespace

  PluginServiceHost::PluginServiceHost(ScriptApiContext& scriptApi, HttpClient* httpClient, ClipboardService* clipboard)
      : m_scriptApi(scriptApi), m_httpClient(httpClient), m_clipboard(clipboard) {}

  PluginServiceHost::~PluginServiceHost() {
    for (auto& service : m_services) {
      PluginIpcRouter::instance().unregisterEndpoint(service.get());
      if (service->alive) {
        *service->alive = false;
      }
      if (service->runtime != nullptr) {
        if (service->subscription != 0) {
          service->runtime->unsubscribe(service->subscription);
        }
        service->runtime->stop();
      }
    }
  }

  PluginServiceHost::Service::DispatchResult
  PluginServiceHost::Service::dispatchIpc(std::string_view event, std::string_view payload) {
    if (runtime == nullptr) {
      return DispatchResult::MissingHost;
    }
    if (!runtime->enqueueCallStrings("onIpc", std::string(event), std::string(payload), {})) {
      return DispatchResult::Failed;
    }
    return DispatchResult::Handled;
  }

  std::optional<ScriptWidgetSettings>
  PluginServiceHost::seedFor(const std::string& entryId, const PluginSettingsMap& pluginSettings) const {
    auto entry = PluginRegistry::instance().resolve(entryId);
    if (!entry.has_value()) {
      return std::nullopt;
    }
    auto seeded = seedEntrySettings(*entry->entry, {});
    static const ScriptWidgetSettings kEmpty;
    const auto it = pluginSettings.find(entry->manifest->id);
    mergePluginSettings(*entry->manifest, it != pluginSettings.end() ? it->second : kEmpty, seeded);
    return seeded;
  }

  void PluginServiceHost::subscribeAndArm(Service& service) {
    Service* svc = &service;
    std::weak_ptr<bool> alive = service.alive;
    service.subscription = service.runtime->subscribe([this, svc, alive](const ScriptWidgetResult& result) {
      auto token = alive.lock();
      if (token == nullptr || !*token) {
        return;
      }
      if (result.patch.updateIntervalMs.has_value()) {
        const int next = std::max(16, *result.patch.updateIntervalMs);
        if (next != svc->updateIntervalMs) {
          svc->updateIntervalMs = next;
          armTimer(*svc);
        }
      }
    });
    armTimer(service);
  }

  void PluginServiceHost::start(const PluginSettingsMap& pluginSettings) {
    PluginRegistry::instance().ensureScanned();
    for (const auto& entry : PluginRegistry::instance().entriesOfKind(PluginEntryKind::Service)) {
      const std::filesystem::path source = entry.sourcePath;
      std::string code = readFile(source);
      if (code.empty()) {
        kLog.warn("service '{}': empty or unreadable source {}", entry.fullId(), source.string());
        continue;
      }

      auto service = std::make_unique<Service>();
      service->entryId = entry.fullId();
      auto seeded = seedFor(service->entryId, pluginSettings);
      service->lastSeededSettings = seeded.value_or(ScriptWidgetSettings{});
      service->runtime = std::make_shared<ScriptRuntime>(
          entry.fullId(), service->lastSeededSettings, m_scriptApi, source.parent_path(), m_httpClient, m_clipboard
      );
      subscribeAndArm(*service);
      service->runtime->start(source.string(), std::move(code), {});
      PluginIpcRouter::instance().registerEndpoint(service.get());
      kLog.info("started service '{}'", entry.fullId());
      m_services.push_back(std::move(service));
    }
  }

  void PluginServiceHost::refresh(const PluginSettingsMap& pluginSettings) {
    for (auto& service : m_services) {
      auto seeded = seedFor(service->entryId, pluginSettings);
      if (!seeded.has_value() || settingsEqual(*seeded, service->lastSeededSettings)) {
        continue; // entry gone or settings unchanged — leave the runtime running
      }

      const std::filesystem::path source = PluginRegistry::instance().resolve(service->entryId)->sourcePath;
      std::string code = readFile(source);
      if (code.empty()) {
        kLog.warn("service '{}': empty or unreadable source on refresh {}", service->entryId, source.string());
        continue;
      }

      service->updateTimer.stop();
      if (service->subscription != 0) {
        service->runtime->unsubscribe(service->subscription);
        service->subscription = 0;
      }
      service->runtime->stop();

      service->lastSeededSettings = *seeded;
      service->runtime = std::make_shared<ScriptRuntime>(
          service->entryId, service->lastSeededSettings, m_scriptApi, source.parent_path(), m_httpClient, m_clipboard
      );
      subscribeAndArm(*service);
      service->runtime->start(source.string(), std::move(code), {});
      kLog.info("restarted service '{}' after settings change", service->entryId);
    }
  }

  void PluginServiceHost::armTimer(Service& service) {
    service.updateTimer.stop();
    Service* svc = &service;
    std::weak_ptr<bool> alive = service.alive;
    service.updateTimer.startRepeating(std::chrono::milliseconds(service.updateIntervalMs), [svc, alive] {
      auto token = alive.lock();
      if (token != nullptr && *token && svc->runtime != nullptr) {
        (void)svc->runtime->enqueueUpdate({});
      }
    });
  }

} // namespace scripting
