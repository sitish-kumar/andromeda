#include "shell/desktop/widgets/desktop_sysmon_widget.h"

#include "render/core/renderer.h"
#include "render/scene/node.h"
#include "system/format_units.h"
#include "system/system_monitor_service.h"
#include "ui/builders.h"
#include "ui/controls/graph.h"
#include "ui/style.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>
#include <vector>

namespace {

  constexpr float kBaseWidth = 180.0f;
  constexpr float kBaseHeight = 80.0f;
  constexpr float kGraphLineWidth = 0.75f;
  bool needsCpuTemp(DesktopSysmonStat stat) { return stat == DesktopSysmonStat::CpuTemp; }
  bool needsGpuTemp(DesktopSysmonStat stat) { return stat == DesktopSysmonStat::GpuTemp; }
  bool needsGpuUsage(DesktopSysmonStat stat) { return stat == DesktopSysmonStat::GpuUsage; }
  bool needsGpuVram(DesktopSysmonStat stat) { return stat == DesktopSysmonStat::GpuVram; }

} // namespace

DesktopSysmonWidget::DesktopSysmonWidget(SystemMonitorService* monitor, Options options)
    : m_monitor(monitor), m_stat(options.stat), m_stat2(options.stat2), m_lineColor(options.lineColor),
      m_lineColor2(options.lineColor2), m_networkInterface(std::move(options.networkInterface)),
      m_showLabel(options.showLabel), m_shadow(options.shadow) {
  if (m_monitor != nullptr) {
    if (needsCpuTemp(m_stat))
      m_monitor->retainCpuTemp();
    if (needsGpuTemp(m_stat))
      m_monitor->retainGpuTemp();
    if (needsGpuUsage(m_stat))
      m_monitor->retainGpuUsage();
    if (needsGpuVram(m_stat))
      m_monitor->retainGpuVram();
    if (m_stat2.has_value() && needsCpuTemp(*m_stat2))
      m_monitor->retainCpuTemp();
    if (m_stat2.has_value() && needsGpuTemp(*m_stat2))
      m_monitor->retainGpuTemp();
    if (m_stat2.has_value() && needsGpuUsage(*m_stat2))
      m_monitor->retainGpuUsage();
    if (m_stat2.has_value() && needsGpuVram(*m_stat2))
      m_monitor->retainGpuVram();
  }
}

DesktopSysmonWidget::~DesktopSysmonWidget() {
  if (m_monitor != nullptr) {
    if (needsCpuTemp(m_stat))
      m_monitor->releaseCpuTemp();
    if (needsGpuTemp(m_stat))
      m_monitor->releaseGpuTemp();
    if (needsGpuUsage(m_stat))
      m_monitor->releaseGpuUsage();
    if (needsGpuVram(m_stat))
      m_monitor->releaseGpuVram();
    if (m_stat2.has_value() && needsCpuTemp(*m_stat2))
      m_monitor->releaseCpuTemp();
    if (m_stat2.has_value() && needsGpuTemp(*m_stat2))
      m_monitor->releaseGpuTemp();
    if (m_stat2.has_value() && needsGpuUsage(*m_stat2))
      m_monitor->releaseGpuUsage();
    if (m_stat2.has_value() && needsGpuVram(*m_stat2))
      m_monitor->releaseGpuVram();
  }
}

void DesktopSysmonWidget::create() {
  auto rootNode = std::make_unique<Node>();

  auto glyph = ui::glyph({
      .out = &m_glyph,
      .glyph = glyphName(m_stat),
  });
  rootNode->addChild(std::move(glyph));

  auto graph = std::make_unique<Graph>();
  graph->setLineWidth(kGraphLineWidth);
  graph->setFillOpacity(0.2f);
  m_graph = static_cast<Graph*>(rootNode->addChild(std::move(graph)));

  if (m_stat2.has_value()) {
    auto glyph2 = ui::glyph({
        .out = &m_glyph2,
        .glyph = glyphName(*m_stat2),
    });
    rootNode->addChild(std::move(glyph2));
  }

  if (m_showLabel) {
    const Color shadow{0.0f, 0.0f, 0.0f, 0.5f};
    auto label = ui::label({
        .out = &m_label,
        .fontWeight = FontWeight::Medium,
    });
    if (m_shadow) {
      label->setShadow(shadow, 0.0f, 1.0f);
    }
    rootNode->addChild(std::move(label));

    if (m_stat2.has_value()) {
      auto label2 = ui::label({
          .out = &m_label2,
          .fontWeight = FontWeight::Medium,
      });
      if (m_shadow) {
        label2->setShadow(shadow, 0.0f, 1.0f);
      }
      rootNode->addChild(std::move(label2));
    }
  }

  setRoot(std::move(rootNode));
}

void DesktopSysmonWidget::onFrameTick(float deltaMs, Renderer& renderer) {
  (void)deltaMs;
  if (!m_redrawLimiter.shouldStep([this]() { requestRedraw(); })) {
    return;
  }
  if (m_monitor != nullptr) {
    if (m_monitor->isRunning()) {
      const auto latestSampleAt = m_monitor->latest().sampledAt;
      if (latestSampleAt != std::chrono::steady_clock::time_point{} && latestSampleAt != m_lastSampleAt) {
        updateGraph(renderer);
        syncLabel();
      }
    } else {
      clearGraph();
      syncLabel();
    }
  }

  m_scrollProgress = scrollProgressForSample(m_lastSampleAt);
  if (m_graph != nullptr) {
    m_graph->setScroll(m_scrollProgress);
  }
  requestRedraw();
}

bool DesktopSysmonWidget::applySetting(
    const std::string& key, const WidgetSettingValue& value,
    const std::unordered_map<std::string, WidgetSettingValue>& allSettings, Renderer& renderer
) {
  if (key == "color") {
    if (const auto* v = std::get_if<std::string>(&value)) {
      m_lineColor = colorSpecFromConfigString(*v, key);
      layout(renderer);
      return true;
    }
    return false;
  }
  if (key == "color2") {
    if (const auto* v = std::get_if<std::string>(&value)) {
      m_lineColor2 = colorSpecFromConfigString(*v, key);
      layout(renderer);
      return true;
    }
    return false;
  }
  if (key == "shadow") {
    if (const auto* v = std::get_if<bool>(&value)) {
      m_shadow = *v;
      const Color shadow{0.0f, 0.0f, 0.0f, 0.5f};
      for (Glyph* glyph : {m_glyph, m_glyph2}) {
        if (glyph != nullptr) {
          if (m_shadow)
            glyph->setShadow(shadow, 0.0f, 1.0f);
          else
            glyph->clearShadow();
        }
      }
      for (Label* label : {m_label, m_label2}) {
        if (label != nullptr) {
          if (m_shadow)
            label->setShadow(shadow, 0.0f, 1.0f);
          else
            label->clearShadow();
        }
      }
      return true;
    }
    return false;
  }
  return DesktopWidget::applySetting(key, value, allSettings, renderer);
}

void DesktopSysmonWidget::onFontFamilyChanged(const std::string& family, Renderer& /*renderer*/) {
  for (Label* label : {m_label, m_label2}) {
    if (label != nullptr) {
      label->setFontFamily(family);
    }
  }
}

void DesktopSysmonWidget::doLayout(Renderer& renderer) {
  if (root() == nullptr || m_glyph == nullptr) {
    return;
  }

  const float scale = m_contentScale;
  const float fontSize = Style::fontSizeBody * scale;
  const float glyphSize = Style::baseGlyphSize * scale;
  const float groupGap = Style::spaceXs * scale;
  const float legendGap = Style::spaceMd * scale;
  const Color shadow{0.0f, 0.0f, 0.0f, 0.5f};

  m_graph->setColor(m_lineColor);
  if (m_stat2.has_value()) {
    m_graph->setColor2(m_lineColor2);
  }
  m_graph->setLineWidth(kGraphLineWidth * scale);

  // Measure a legend group (icon + optional value), coloring both to the stat's line color.
  auto measureGroup = [&](Glyph* glyph, Label* label, const ColorSpec& color, float& width, float& height) {
    glyph->setGlyphSize(glyphSize);
    glyph->setColor(color);
    if (m_shadow) {
      glyph->setShadow(shadow, 0.0f, 1.0f);
    }
    glyph->measure(renderer);
    width = glyph->width();
    height = glyph->height();
    if (label != nullptr) {
      label->setFontSize(fontSize);
      label->setColor(color);
      label->measure(renderer);
      width += groupGap + label->width();
      height = std::max(height, label->height());
    }
  };

  float w1 = 0.0f, h1 = 0.0f;
  measureGroup(m_glyph, m_label, m_lineColor, w1, h1);

  float w2 = 0.0f, h2 = 0.0f;
  if (m_glyph2 != nullptr) {
    measureGroup(m_glyph2, m_label2, m_lineColor2, w2, h2);
  }

  const float totalW = kBaseWidth * scale;
  const float chartH = kBaseHeight * scale;

  const float headerW = (m_glyph2 != nullptr) ? (w1 + legendGap + w2) : w1;
  const float headerH = std::max(h1, h2);
  const float contentW = std::max(totalW, headerW);

  m_graph->setPosition(0.0f, 0.0f);
  m_graph->setSize(contentW, chartH);
  m_graph->sync(renderer);

  const float headerY = chartH + Style::spaceSm * scale;
  float x = std::round((contentW - headerW) * 0.5f);

  auto placeGroup = [&](Glyph* glyph, Label* label) {
    glyph->setPosition(x, headerY + std::round((headerH - glyph->height()) * 0.5f));
    x += glyph->width();
    if (label != nullptr) {
      x += groupGap;
      label->setPosition(x, headerY + std::round((headerH - label->height()) * 0.5f));
      x += label->width();
    }
  };

  placeGroup(m_glyph, m_label);
  if (m_glyph2 != nullptr) {
    x += legendGap;
    placeGroup(m_glyph2, m_label2);
  }

  root()->setSize(contentW, headerY + headerH);
}

void DesktopSysmonWidget::doUpdate(Renderer& renderer) {
  (void)renderer;
  if (m_monitor == nullptr) {
    return;
  }

  if (m_monitor->isRunning()) {
    updateGraph(renderer);
  } else {
    clearGraph();
  }
  syncLabel();
}

void DesktopSysmonWidget::syncLabel() {
  if (m_label == nullptr) {
    return;
  }

  std::string text = formatValueFor(m_stat);
  if (text != m_lastRawValue) {
    m_lastRawValue = text;
    m_label->setText(text);
    requestRedraw();
  }

  if (m_label2 != nullptr && m_stat2.has_value()) {
    std::string text2 = formatValueFor(*m_stat2);
    if (text2 != m_lastRawValue2) {
      m_lastRawValue2 = text2;
      m_label2->setText(text2);
      requestRedraw();
    }
  }
}

double DesktopSysmonWidget::normalizedFromStats(
    DesktopSysmonStat stat, const SystemStats& stats, double& tempMin, double& tempMax,
    std::string_view networkInterface
) {
  switch (stat) {
  case DesktopSysmonStat::CpuUsage:
    return stats.cpuUsagePercent / 100.0;

  case DesktopSysmonStat::CpuTemp:
    if (stats.cpuTempC.has_value()) {
      const double temp = *stats.cpuTempC;
      tempMin = std::min(tempMin, temp);
      tempMax = std::max(tempMax, temp);
      const double range = tempMax - tempMin;
      if (range <= 0.0)
        return 0.5;
      return std::clamp((temp - tempMin) / range, 0.0, 1.0);
    }
    return 0.0;

  case DesktopSysmonStat::GpuTemp:
    if (stats.gpuTempC.has_value()) {
      const double temp = *stats.gpuTempC;
      tempMin = std::min(tempMin, temp);
      tempMax = std::max(tempMax, temp);
      const double range = tempMax - tempMin;
      if (range <= 0.0)
        return 0.5;
      return std::clamp((temp - tempMin) / range, 0.0, 1.0);
    }
    return 0.0;

  case DesktopSysmonStat::GpuUsage:
    if (stats.gpuUsagePercent.has_value()) {
      return *stats.gpuUsagePercent / 100.0;
    }
    return 0.0;

  case DesktopSysmonStat::GpuVram:
    if (stats.gpuVramUsedBytes.has_value() && stats.gpuVramTotalBytes.has_value() && *stats.gpuVramTotalBytes > 0) {
      return static_cast<double>(*stats.gpuVramUsedBytes) / static_cast<double>(*stats.gpuVramTotalBytes);
    }
    return 0.0;

  case DesktopSysmonStat::RamPct:
    return stats.ramUsagePercent / 100.0;

  case DesktopSysmonStat::SwapPct:
    if (stats.swapTotalMb > 0) {
      return static_cast<double>(stats.swapUsedMb) / static_cast<double>(stats.swapTotalMb);
    }
    return 0.0;

  case DesktopSysmonStat::NetRx: {
    const double value = networkInterface.empty() ? stats.netRxBytesPerSec : [&stats, networkInterface]() {
      if (const auto it = stats.netThroughputByInterface.find(std::string(networkInterface));
          it != stats.netThroughputByInterface.end()) {
        return it->second.rxBytesPerSec;
      }
      return 0.0;
    }();
    tempMax = std::max(tempMax, value);
    return tempMax > 0.0 ? std::clamp(value / tempMax, 0.0, 1.0) : 0.0;
  }

  case DesktopSysmonStat::NetTx: {
    const double value = networkInterface.empty() ? stats.netTxBytesPerSec : [&stats, networkInterface]() {
      if (const auto it = stats.netThroughputByInterface.find(std::string(networkInterface));
          it != stats.netThroughputByInterface.end()) {
        return it->second.txBytesPerSec;
      }
      return 0.0;
    }();
    tempMax = std::max(tempMax, value);
    return tempMax > 0.0 ? std::clamp(value / tempMax, 0.0, 1.0) : 0.0;
  }
  }

  return 0.0;
}

std::string DesktopSysmonWidget::formatValueFor(DesktopSysmonStat stat) const {
  if (m_monitor == nullptr || !m_monitor->isRunning()) {
    return "--";
  }

  const auto stats = m_monitor->latest();

  switch (stat) {
  case DesktopSysmonStat::CpuUsage:
    return std::format("{:.0f}%", stats.cpuUsagePercent);

  case DesktopSysmonStat::CpuTemp:
    if (stats.cpuTempC.has_value()) {
      return std::format("{:.0f}°C", *stats.cpuTempC);
    }
    return "--";

  case DesktopSysmonStat::GpuTemp:
    if (stats.gpuTempC.has_value()) {
      return std::format("{:.0f}°C", *stats.gpuTempC);
    }
    return "--";

  case DesktopSysmonStat::GpuUsage:
    if (stats.gpuUsagePercent.has_value()) {
      return std::format("{:.0f}%", *stats.gpuUsagePercent);
    }
    return "--";

  case DesktopSysmonStat::GpuVram:
    if (stats.gpuVramUsedBytes.has_value() && stats.gpuVramTotalBytes.has_value() && *stats.gpuVramTotalBytes > 0) {
      return std::format(
          "{:.0f}%",
          100.0 * static_cast<double>(*stats.gpuVramUsedBytes) / static_cast<double>(*stats.gpuVramTotalBytes)
      );
    }
    return "--";

  case DesktopSysmonStat::RamPct:
    return std::format("{:.0f}%", stats.ramUsagePercent);

  case DesktopSysmonStat::SwapPct:
    if (stats.swapTotalMb > 0) {
      return std::format(
          "{:.0f}%", 100.0 * static_cast<double>(stats.swapUsedMb) / static_cast<double>(stats.swapTotalMb)
      );
    }
    return "--";

  case DesktopSysmonStat::NetRx:
    return FormatUnits::formatDecimalBytesPerSecond(m_monitor->netRxBytesPerSec(m_networkInterface));

  case DesktopSysmonStat::NetTx:
    return FormatUnits::formatDecimalBytesPerSecond(m_monitor->netTxBytesPerSec(m_networkInterface));
  }

  return "--";
}

void DesktopSysmonWidget::clearGraph() {
  if (m_graph == nullptr || !m_graphInitialized) {
    return;
  }

  m_graph->setValues({});
  m_graph->setValues2({});
  m_graphInitialized = false;
  m_lastSampleAt = {};
  m_scrollProgress = 1.0f;
  requestRedraw();
}

void DesktopSysmonWidget::updateGraph(Renderer& renderer) {
  if (m_graph == nullptr || m_monitor == nullptr || !m_monitor->isRunning()) {
    return;
  }

  const auto hist = m_monitor->history();
  if (hist.size() < 4) {
    return;
  }

  const auto latestSampleAt = hist.back().sampledAt;
  const bool newData = latestSampleAt != m_lastSampleAt;
  if (!newData && m_graphInitialized) {
    return;
  }

  const auto n = hist.size();
  std::vector<float> data1(n);
  for (std::size_t i = 0; i < n; ++i) {
    data1[i] = static_cast<float>(
        std::clamp(normalizedFromStats(m_stat, hist[i], m_tempMin1, m_tempMax1, m_networkInterface), 0.0, 1.0)
    );
  }
  m_graph->setValues(std::move(data1));

  if (m_stat2.has_value()) {
    std::vector<float> data2(n);
    for (std::size_t i = 0; i < n; ++i) {
      data2[i] = static_cast<float>(
          std::clamp(normalizedFromStats(*m_stat2, hist[i], m_tempMin2, m_tempMax2, m_networkInterface), 0.0, 1.0)
      );
    }
    m_graph->setValues2(std::move(data2));
  }

  m_graph->sync(renderer);
  m_graphInitialized = true;
  m_lastSampleAt = latestSampleAt;
  m_scrollProgress = scrollProgressForSample(m_lastSampleAt);
  m_graph->setScroll(m_scrollProgress);
  requestRedraw();
}

float DesktopSysmonWidget::scrollProgressForSample(std::chrono::steady_clock::time_point sampledAt) const {
  if (sampledAt == std::chrono::steady_clock::time_point{}) {
    return 1.0f;
  }

  const auto sampleInterval = m_monitor != nullptr ? m_monitor->historySampleInterval()
                                                   : std::chrono::steady_clock::duration{std::chrono::seconds(1)};
  if (sampleInterval.count() <= 0) {
    return 1.0f;
  }

  const auto elapsed = std::chrono::steady_clock::now() - sampledAt;
  const auto clamped = std::clamp(elapsed, std::chrono::steady_clock::duration::zero(), sampleInterval);
  return std::chrono::duration<float>(clamped).count() / std::chrono::duration<float>(sampleInterval).count();
}

const char* DesktopSysmonWidget::glyphName(DesktopSysmonStat stat) {
  switch (stat) {
  case DesktopSysmonStat::CpuUsage:
    return "cpu-usage";
  case DesktopSysmonStat::CpuTemp:
    return "cpu-temperature";
  case DesktopSysmonStat::GpuTemp:
    return "temperature";
  case DesktopSysmonStat::GpuUsage:
    return "gpu-usage";
  case DesktopSysmonStat::GpuVram:
    return "memory";
  case DesktopSysmonStat::RamPct:
    return "memory";
  case DesktopSysmonStat::SwapPct:
    return "storage";
  case DesktopSysmonStat::NetRx:
    return "download";
  case DesktopSysmonStat::NetTx:
    return "upload";
  }
  return "cpu-usage";
}
