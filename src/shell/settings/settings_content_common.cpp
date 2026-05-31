#include "shell/settings/settings_content_common.h"

#include "config/config_types.h"
#include "i18n/i18n.h"
#include "shell/settings/settings_content.h"
#include "ui/builders.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <cmath>
#include <format>
#include <unordered_set>
#include <utility>

namespace settings {
  std::unique_ptr<Label>
  makeLabel(std::string_view text, float fontSize, const ColorSpec& color, FontWeight fontWeight) {
    return ui::label({
        .text = std::string(text),
        .fontSize = fontSize,
        .color = color,
        .fontWeight = fontWeight,
    });
  }

  std::unique_ptr<Label> makeSettingSubtitleLabel(std::string_view text, float scale) {
    return ui::label({
        .text = std::string(text),
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .maxLines = kSettingDescriptionMaxLines,
    });
  }

  std::optional<std::size_t> optionIndex(const std::vector<SelectOption>& options, std::string_view value) {
    for (std::size_t i = 0; i < options.size(); ++i) {
      if (options[i].value == value) {
        return i;
      }
    }
    return std::nullopt;
  }

  std::string optionLabel(const std::vector<SelectOption>& options, std::string_view value) {
    for (const auto& opt : options) {
      if (opt.value == value) {
        return opt.label;
      }
    }
    return std::string(value);
  }

  std::vector<std::string> optionLabels(const std::vector<SelectOption>& options) {
    std::vector<std::string> labels;
    labels.reserve(options.size());
    for (const auto& opt : options) {
      labels.push_back(opt.label);
    }
    return labels;
  }

  std::vector<ColorSwatchPreview> optionSwatchPreviews(const std::vector<SelectOption>& options) {
    std::vector<ColorSwatchPreview> previews;
    previews.reserve(options.size());
    for (const auto& opt : options) {
      previews.push_back(opt.preview);
    }
    return previews;
  }

  bool isBlankInput(std::string_view text) { return StringUtils::trim(text).empty(); }

  std::string formatSliderValue(double value, bool integerValue) {
    if (integerValue) {
      return std::format("{}", static_cast<int>(std::llround(value)));
    }
    return StringUtils::formatFixedDotDecimal(value, 2);
  }

  std::optional<double> parseDoubleInput(std::string_view text) { return StringUtils::parseDotDecimal<double>(text); }

  std::vector<SelectOption> sessionActionKindOptions() {
    return {
        {"lock", i18n::tr("settings.session-actions.kind.lock"), {}},
        {"logout", i18n::tr("settings.session-actions.kind.logout"), {}},
        {"suspend", i18n::tr("settings.session-actions.kind.suspend"), {}},
        {"lock_and_suspend", i18n::tr("settings.session-actions.kind.lock-and-suspend"), {}},
        {"reboot", i18n::tr("settings.session-actions.kind.reboot"), {}},
        {"shutdown", i18n::tr("settings.session-actions.kind.shutdown"), {}},
        {"command", i18n::tr("settings.session-actions.kind.command"), {}},
    };
  }

  std::string
  sessionActionRowSummary(const std::vector<SelectOption>& kindOptions, const SessionPanelActionConfig& row) {
    if (row.label.has_value() && !row.label->empty()) {
      return *row.label;
    }
    return optionLabel(kindOptions, row.action);
  }

  std::string sessionActionDisplayTitle(const SessionPanelActionConfig& row) {
    return sessionActionRowSummary(sessionActionKindOptions(), row);
  }

  std::string sanitizedIdleBehaviorName(std::string_view text) {
    std::string out = StringUtils::trim(text);
    for (char& ch : out) {
      if (ch == '.' || ch == '[' || ch == ']') {
        ch = '-';
      }
    }
    return out;
  }

  std::string uniqueIdleBehaviorName(
      std::string base, const std::vector<IdleBehaviorConfig>& rows, std::optional<std::size_t> ignoreIndex
  ) {
    base = sanitizedIdleBehaviorName(base);
    if (base.empty()) {
      base = "idle-behavior";
    }

    std::unordered_set<std::string> names;
    for (std::size_t i = 0; i < rows.size(); ++i) {
      if (ignoreIndex.has_value() && i == *ignoreIndex) {
        continue;
      }
      if (!rows[i].name.empty()) {
        names.insert(rows[i].name);
      }
    }

    if (!names.contains(base)) {
      return base;
    }
    for (int suffix = 2; suffix < 10000; ++suffix) {
      std::string candidate = std::format("{}-{}", base, suffix);
      if (!names.contains(candidate)) {
        return candidate;
      }
    }
    return base;
  }

  void normalizeIdleBehaviorNames(std::vector<IdleBehaviorConfig>& rows) {
    std::vector<IdleBehaviorConfig> normalized;
    normalized.reserve(rows.size());
    for (auto& row : rows) {
      row.name = uniqueIdleBehaviorName(row.name, normalized);
      normalized.push_back(row);
    }
    rows = std::move(normalized);
  }

  std::string idleBehaviorRowSummary(const IdleBehaviorConfig& row) {
    IdleBehaviorConfig norm = row;
    normalizeIdleBehaviorAction(norm);

    const auto displayName = [&]() -> std::string {
      if (norm.action == "lock") {
        return i18n::tr("settings.idle.behavior.presets.lock");
      }
      if (norm.action == "screen_off") {
        return i18n::tr("settings.idle.behavior.presets.monitor-off");
      }
      if (norm.action == "suspend") {
        return i18n::tr("settings.idle.behavior.presets.suspend");
      }
      if (norm.action == "lock_and_suspend") {
        return i18n::tr("settings.idle.behavior.presets.lock-and-suspend");
      }
      if (row.name.empty()) {
        return i18n::tr("settings.idle.behavior.unnamed");
      }
      return row.name;
    };

    const std::string name = displayName();
    if (name.empty()) {
      return i18n::tr("settings.idle.behavior.unnamed");
    }
    if (row.timeoutSeconds <= 0) {
      return i18n::tr("settings.idle.behavior.summary-disabled-timeout", "name", name);
    }
    return i18n::tr("settings.idle.behavior.summary", "name", name, "seconds", std::to_string(row.timeoutSeconds));
  }

} // namespace settings
