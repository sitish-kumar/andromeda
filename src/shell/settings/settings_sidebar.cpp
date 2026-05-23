#include "shell/settings/settings_sidebar.h"

#include "i18n/i18n.h"
#include "shell/settings/settings_registry.h"
#include "ui/builders.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

namespace settings {
  namespace {

    constexpr float kSidebarWidth = 180.0f;

    std::string normalizedConfigId(std::string_view text) { return StringUtils::trim(text); }

    bool isValidConfigId(std::string_view text) {
      const auto trimmed = StringUtils::trim(text);
      if (trimmed.empty()) {
        return false;
      }
      return std::all_of(trimmed.begin(), trimmed.end(),
                         [](unsigned char c) { return std::isalnum(c) != 0 || c == '_' || c == '-'; });
    }

    bool barNameExists(const std::vector<std::string>& barNames, std::string_view name) {
      return std::any_of(barNames.begin(), barNames.end(),
                         [name](const std::string& barName) { return barName == name; });
    }

    std::string nextAvailableBarName(const std::vector<std::string>& barNames) {
      for (std::size_t i = 1;; ++i) {
        const std::string candidate = i == 1 ? "bar" : std::format("bar_{}", i);
        if (!barNameExists(barNames, candidate)) {
          return candidate;
        }
      }
    }

    void applyPrimaryNavStyle(Button& button, float scale, bool selected) {
      button.setVariant(selected ? ButtonVariant::TabActive : ButtonVariant::Tab);
      button.setContentAlign(ButtonContentAlign::Start);
      button.setFontSize(Style::fontSizeBody * scale);
      button.setGlyphSize(21.0f * scale);
      button.setMinHeight(Style::controlHeight * scale);
      button.setPadding(Style::spaceSm * scale, Style::spaceMd * scale);
      button.setGap(Style::spaceSm * scale);
      button.setRadius(Style::scaledRadiusLg(scale));
      if (button.label() != nullptr) {
        button.label()->setFontWeight(FontWeight::Bold);
      }
    }

    void applySecondaryNavStyle(Button& button, float scale, bool selected) {
      button.setVariant(selected ? ButtonVariant::TabActive : ButtonVariant::Tab);
      button.setContentAlign(ButtonContentAlign::Start);
      button.setFontSize(Style::fontSizeCaption * scale);
      button.setGlyphSize(Style::fontSizeCaption * scale);
      button.setMinHeight(Style::controlHeightSm * scale);
      button.setPadding(Style::spaceXs * scale, Style::spaceMd * scale, Style::spaceXs * scale, Style::spaceLg * scale);
      button.setGap(Style::spaceXs * scale);
      button.setRadius(Style::scaledRadiusMd(scale));
    }

    std::unique_ptr<Button> makePrimaryNavButton(std::string_view glyph, std::string text, float scale, bool selected,
                                                 std::function<void()> onClick) {
      return ui::button({
          .text = std::move(text),
          .glyph = std::string(glyph),
          .onClick = std::move(onClick),
          .configure = [scale, selected](Button& button) { applyPrimaryNavStyle(button, scale, selected); },
      });
    }

    std::unique_ptr<Button> makeSecondaryNavButton(std::string_view glyph, std::string text, float scale, bool selected,
                                                   std::function<void()> onClick) {
      return ui::button({
          .text = std::move(text),
          .glyph = std::string(glyph),
          .onClick = std::move(onClick),
          .configure = [scale, selected](Button& button) { applySecondaryNavStyle(button, scale, selected); },
      });
    }

    void applySecondaryActionStyle(Button& button, float scale) {
      button.setVariant(ButtonVariant::Ghost);
      button.setContentAlign(ButtonContentAlign::Start);
      button.setFontSize(Style::fontSizeCaption * scale);
      button.setGlyphSize(Style::fontSizeCaption * scale);
      button.setMinHeight(Style::controlHeightSm * scale);
      button.setPadding(Style::spaceXs * scale, Style::spaceMd * scale, Style::spaceXs * scale, Style::spaceLg * scale);
      button.setGap(Style::spaceXs * scale);
      button.setRadius(Style::scaledRadiusMd(scale));
    }

    void applyPrimaryActionStyle(Button& button, float scale) {
      button.setVariant(ButtonVariant::Ghost);
      button.setContentAlign(ButtonContentAlign::Start);
      button.setFontSize(Style::fontSizeBody * scale);
      button.setGlyphSize(21.0f * scale);
      button.setMinHeight(Style::controlHeight * scale);
      button.setPadding(Style::spaceSm * scale, Style::spaceMd * scale);
      button.setGap(Style::spaceSm * scale);
      button.setRadius(Style::scaledRadiusLg(scale));
      if (button.label() != nullptr) {
        button.label()->setFontWeight(FontWeight::Bold);
      }
    }

    std::unique_ptr<Button> makeCreateButton(std::string text, float scale, std::function<void()> onClick) {
      return ui::button({
          .text = std::move(text),
          .fontSize = Style::fontSizeCaption * scale,
          .variant = ButtonVariant::Default,
          .onClick = std::move(onClick),
          .configure =
              [scale](Button& button) {
                button.setMinHeight(Style::controlHeightSm * scale);
                button.setPadding(Style::spaceXs * scale, Style::spaceSm * scale);
                button.setRadius(Style::scaledRadiusSm(scale));
              },
      });
    }

    std::unique_ptr<Button> makeCreateCancelButton(float scale, std::function<void()> onClick) {
      return ui::button({
          .glyph = "close",
          .glyphSize = Style::fontSizeCaption * scale,
          .variant = ButtonVariant::Ghost,
          .onClick = std::move(onClick),
          .configure =
              [scale](Button& button) {
                button.setMinWidth(Style::controlHeightSm * scale);
                button.setMinHeight(Style::controlHeightSm * scale);
                button.setPadding(Style::spaceXs * scale);
                button.setRadius(Style::scaledRadiusSm(scale));
              },
      });
    }

  } // namespace

  std::unique_ptr<Flex> buildSettingsSidebar(SettingsSidebarContext ctx) {
    const Config& cfg = ctx.config;
    std::vector<std::string> existingBarNames = ctx.availableBars;
    const std::string nextBarName = nextAvailableBarName(existingBarNames);
    const auto sectionLabel = [](std::string_view section) {
      return i18n::tr("settings.navigation.sections." + std::string(section));
    };

    auto* scroll = &ctx.contentScrollState;
    auto* selectedSection = &ctx.selectedSection;
    auto* selectedBarName = &ctx.selectedBarName;
    auto* selectedMonitorOverride = &ctx.selectedMonitorOverride;
    auto* creatingBarName = &ctx.creatingBarName;
    auto* creatingMonitorOverrideBarName = &ctx.creatingMonitorOverrideBarName;
    auto* creatingMonitorOverrideMatch = &ctx.creatingMonitorOverrideMatch;

    const auto clearTransientState = std::move(ctx.clearTransientState);
    const auto clearSearchQuery = std::move(ctx.clearSearchQuery);
    const auto requestRebuild = std::move(ctx.requestRebuild);
    const auto createBar = std::move(ctx.createBar);
    const auto createMonitorOverride = std::move(ctx.createMonitorOverride);
    const float scale = ctx.scale;
    const bool searchActive = ctx.globalSearchActive;
    const bool showActiveTab = !searchActive;

    auto sidebarScroll = ui::scrollView({
        .state = &ctx.sidebarScrollState,
        .scrollbarVisible = true,
        .viewportPaddingH = 0.0f,
        .viewportPaddingV = 0.0f,
        .fill = colorSpecFromRole(ColorRole::Surface),
        .radius = Style::scaledRadiusXl(scale),
        .minWidth = kSidebarWidth * scale,
        .fillHeight = true,
        .width = kSidebarWidth * scale,
        .height = 0.0f,
        .configure = [](ScrollView& scrollView) { scrollView.clearBorder(); },
    });

    auto* sidebar = sidebarScroll->content();
    sidebar->setDirection(FlexDirection::Vertical);
    sidebar->setAlign(FlexAlign::Stretch);
    sidebar->setGap(Style::spaceXs * scale);
    sidebar->setPadding(Style::spaceSm * scale);

    for (const auto& section : ctx.sections) {
      const bool selected = showActiveTab && section == *selectedSection;
      sidebar->addChild(makePrimaryNavButton(
          sectionGlyph(section), sectionLabel(section), scale, selected,
          [selectedSection, scroll, section, searchActive, clearTransientState, clearSearchQuery, requestRebuild]() {
            if (searchActive || *selectedSection != section) {
              scroll->offset = 0.0f;
            }
            *selectedSection = section;
            clearSearchQuery();
            clearTransientState();
            requestRebuild();
          }));
    }

    for (const auto& barName : ctx.availableBars) {
      const bool barSelected =
          showActiveTab && *selectedSection == "bar" && *selectedBarName == barName && selectedMonitorOverride->empty();
      sidebar->addChild(makePrimaryNavButton(
          sectionGlyph("bar"), i18n::tr("settings.entities.bar.label", "name", barName), scale, barSelected,
          [selectedSection, selectedBarName, selectedMonitorOverride, scroll, barName, searchActive,
           clearTransientState, clearSearchQuery, requestRebuild]() {
            if (searchActive || *selectedSection != "bar" || *selectedBarName != barName ||
                !selectedMonitorOverride->empty()) {
              scroll->offset = 0.0f;
            }
            *selectedSection = "bar";
            *selectedBarName = barName;
            selectedMonitorOverride->clear();
            clearSearchQuery();
            clearTransientState();
            requestRebuild();
          }));

      const auto* bar = settings::findBar(cfg, barName);
      if (bar == nullptr) {
        continue;
      }

      for (const auto& ovr : bar->monitorOverrides) {
        const bool ovrSelected = showActiveTab && *selectedSection == "bar" && *selectedBarName == barName &&
                                 *selectedMonitorOverride == ovr.match;
        auto match = ovr.match;
        sidebar->addChild(makeSecondaryNavButton(
            "device-desktop", i18n::tr("settings.entities.monitor-override.label", "name", ovr.match), scale,
            ovrSelected,
            [selectedSection, selectedBarName, selectedMonitorOverride, scroll, barName, match, searchActive,
             clearTransientState, clearSearchQuery, requestRebuild]() {
              if (searchActive || *selectedSection != "bar" || *selectedBarName != barName ||
                  *selectedMonitorOverride != match) {
                scroll->offset = 0.0f;
              }
              *selectedSection = "bar";
              *selectedBarName = barName;
              *selectedMonitorOverride = match;
              clearSearchQuery();
              clearTransientState();
              requestRebuild();
            }));
      }

      if (*selectedSection != "bar" || *selectedBarName != barName) {
        continue;
      }

      sidebar->addChild(ui::button({
          .text = i18n::tr("settings.entities.monitor-override.new"),
          .glyph = "add",
          .onClick =
              [creatingMonitorOverrideBarName, creatingMonitorOverrideMatch, barName, clearTransientState,
               requestRebuild]() {
                clearTransientState();
                *creatingMonitorOverrideBarName = barName;
                creatingMonitorOverrideMatch->clear();
                requestRebuild();
              },
          .configure = [scale](Button& button) { applySecondaryActionStyle(button, scale); },
      }));

      if (*creatingMonitorOverrideBarName != barName) {
        continue;
      }

      auto createPanel = ui::column({
          .align = FlexAlign::Stretch,
          .gap = Style::spaceXs * scale,
          .configure =
              [scale](Flex& panel) { panel.setPadding(0.0f, Style::spaceXs * scale, 0.0f, Style::spaceLg * scale); },
      });

      Input* inputPtr = nullptr;
      auto input = ui::input({
          .out = &inputPtr,
          .value = *creatingMonitorOverrideMatch,
          .placeholder = i18n::tr("settings.entities.monitor-override.match-placeholder"),
          .fontSize = Style::fontSizeCaption * scale,
          .controlHeight = Style::controlHeightSm * scale,
          .horizontalPadding = Style::spaceXs * scale,
          .width = 112.0f * scale,
          .height = Style::controlHeightSm * scale,
      });

      std::vector<std::string> existingMatches;
      existingMatches.reserve(bar->monitorOverrides.size());
      for (const auto& monitorOverride : bar->monitorOverrides) {
        existingMatches.push_back(monitorOverride.match);
      }

      auto doCreate = [barName, createMonitorOverride, inputPtr,
                       existingMatches = std::move(existingMatches)](std::string rawMatch) {
        const std::string match = normalizedConfigId(rawMatch);
        if (match.empty() ||
            std::find(existingMatches.begin(), existingMatches.end(), match) != existingMatches.end()) {
          inputPtr->setInvalid(true);
          return;
        }
        inputPtr->setInvalid(false);
        createMonitorOverride(barName, match);
      };

      inputPtr->setOnChange([creatingMonitorOverrideMatch, inputPtr](const std::string& value) {
        *creatingMonitorOverrideMatch = value;
        inputPtr->setInvalid(false);
      });
      inputPtr->setOnSubmit([doCreate](const std::string& text) mutable { doCreate(text); });

      createPanel->addChild(std::move(input));
      createPanel->addChild(ui::row(
          {
              .align = FlexAlign::Center,
              .gap = Style::spaceXs * scale,
          },
          makeCreateButton(i18n::tr("settings.entities.monitor-override.create"), scale,
                           [doCreate, inputPtr]() mutable { doCreate(inputPtr->value()); }),
          makeCreateCancelButton(scale,
                                 [creatingMonitorOverrideBarName, creatingMonitorOverrideMatch, requestRebuild]() {
                                   creatingMonitorOverrideBarName->clear();
                                   creatingMonitorOverrideMatch->clear();
                                   requestRebuild();
                                 })));
      sidebar->addChild(std::move(createPanel));
    }

    sidebar->addChild(ui::button({
        .text = i18n::tr("settings.entities.bar.new"),
        .glyph = "add",
        .onClick =
            [creatingBarName, nextBarName, clearTransientState, requestRebuild]() {
              clearTransientState();
              *creatingBarName = nextBarName;
              requestRebuild();
            },
        .configure = [scale](Button& button) { applyPrimaryActionStyle(button, scale); },
    }));

    if (!creatingBarName->empty()) {
      auto createPanel = ui::column({
          .align = FlexAlign::Stretch,
          .gap = Style::spaceXs * scale,
          .configure = [scale](Flex& panel) { panel.setPadding(0.0f, Style::spaceXs * scale); },
      });

      Input* inputPtr = nullptr;
      auto input = ui::input({
          .out = &inputPtr,
          .value = *creatingBarName,
          .placeholder = i18n::tr("settings.entities.bar.id-placeholder"),
          .fontSize = Style::fontSizeCaption * scale,
          .controlHeight = Style::controlHeightSm * scale,
          .horizontalPadding = Style::spaceXs * scale,
          .width = 120.0f * scale,
          .height = Style::controlHeightSm * scale,
      });

      auto doCreate = [existingBarNames, createBar, inputPtr](std::string rawName) {
        const std::string name = normalizedConfigId(rawName);
        if (!isValidConfigId(name) || barNameExists(existingBarNames, name)) {
          inputPtr->setInvalid(true);
          return;
        }
        inputPtr->setInvalid(false);
        createBar(name);
      };

      inputPtr->setOnChange([creatingBarName, inputPtr](const std::string& value) {
        *creatingBarName = value;
        inputPtr->setInvalid(false);
      });
      inputPtr->setOnSubmit([doCreate](const std::string& text) mutable { doCreate(text); });

      createPanel->addChild(std::move(input));
      createPanel->addChild(ui::row(
          {
              .align = FlexAlign::Center,
              .gap = Style::spaceXs * scale,
          },
          makeCreateButton(i18n::tr("settings.entities.bar.create"), scale,
                           [doCreate, inputPtr]() mutable { doCreate(inputPtr->value()); }),
          makeCreateCancelButton(scale, [creatingBarName, requestRebuild]() {
            creatingBarName->clear();
            requestRebuild();
          })));
      sidebar->addChild(std::move(createPanel));
    }

    return sidebarScroll;
  }

} // namespace settings
