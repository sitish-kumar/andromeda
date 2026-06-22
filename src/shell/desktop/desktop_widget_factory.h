#pragma once

#include "config/config_service.h"
#include "shell/desktop/desktop_widget.h"
#include "shell/desktop/desktop_widget_services.h"

#include <memory>
#include <string>
#include <unordered_map>

class ClipboardService;
class FileWatcher;
class HttpClient;
class MprisService;
class SystemMonitorService;
class PipeWireSpectrum;
class WeatherService;
namespace scripting {
  class ScriptApiContext;
}

class DesktopWidgetFactory {
public:
  explicit DesktopWidgetFactory(DesktopWidgetRuntimeServices services);

  [[nodiscard]] std::unique_ptr<DesktopWidget> create(
      const std::string& type, const std::unordered_map<std::string, WidgetSettingValue>& settings,
      float contentScale = 1.0f
  ) const;

private:
  PipeWireSpectrum* m_pipewireSpectrum = nullptr;
  const WeatherService* m_weather = nullptr;
  MprisService* m_mpris = nullptr;
  HttpClient* m_httpClient = nullptr;
  SystemMonitorService* m_sysmon = nullptr;
  DesktopWidgetScriptDeps m_scriptDeps;
};
