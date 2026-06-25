#include "shell/settings/settings_sidebar.h"

#include "core/key_symbols.h"
#include "i18n/i18n.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "render/scene/node.h"
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

    constexpr float kSidebarWidth = 200.0f;
    constexpr float kSidebarPadding = 6.0f;
    constexpr float kSidebarGap = 2.0f;
    constexpr float kPrimaryNavGlyphSize = 18.0f;
    constexpr float kPrimaryNavGap = 6.0f;
    constexpr float kPrimaryNavPaddingH = 10.0f;

    class SettingsSidebarNav : public Flex {
    public:
      explicit SettingsSidebarNav(std::function<void(const Node*)> scrollNodeIntoView)
          : m_scrollNodeIntoView(std::move(scrollNodeIntoView)) {
        setDirection(FlexDirection::Vertical);
        setAlign(FlexAlign::Stretch);

        auto area = std::make_unique<InputArea>();
        area->setFocusable(true);
        area->setHitTestVisible(false);
        area->setTabFocusKey("settings.sidebar");
        area->setOnFocusGain([this]() { onFocusGain(); });
        area->setOnFocusLoss([this]() { onFocusLoss(); });
        area->setOnKeyDown([this](const InputArea::KeyData& key) {
          if (key.pressed) {
            handleKey(key.sym);
          }
        });
        m_focusArea = static_cast<InputArea*>(addChild(std::move(area)));
        m_focusArea->setParticipatesInLayout(false);
        m_focusArea->setZIndex(2);
      }

      void registerNavItem(Button* button, std::function<void()> activate) {
        if (button == nullptr) {
          return;
        }
        m_items.push_back(NavItem{button, std::move(activate)});
      }

      void doLayout(Renderer& renderer) override {
        Flex::doLayout(renderer);
        if (m_focusArea != nullptr) {
          m_focusArea->setPosition(0.0f, 0.0f);
          m_focusArea->setFrameSize(width(), height());
        }
      }

    private:
      struct NavItem {
        Button* button = nullptr;
        std::function<void()> activate;
      };

      void onFocusGain() {
        syncKeyboardIndexToSelection();
        applyKeyboardHints();
        scrollKeyboardItemIntoView();
      }

      void onFocusLoss() { clearKeyboardHints(); }

      void syncKeyboardIndexToSelection() {
        for (std::size_t i = 0; i < m_items.size(); ++i) {
          if (m_items[i].button != nullptr && m_items[i].button->variant() == ButtonVariant::TabActive) {
            setKeyboardIndex(i, false);
            return;
          }
        }
        setKeyboardIndex(0, false);
      }

      void setKeyboardIndex(std::size_t index, bool scrollIntoView) {
        if (m_items.empty()) {
          return;
        }
        index = std::min(index, m_items.size() - 1);
        if (m_keyboardIndex != index) {
          if (m_keyboardIndex < m_items.size() && m_items[m_keyboardIndex].button != nullptr) {
            m_items[m_keyboardIndex].button->setKeyboardFocusHint(false);
          }
          m_keyboardIndex = index;
        }
        applyKeyboardHints();
        if (scrollIntoView) {
          scrollKeyboardItemIntoView();
        }
      }

      void applyKeyboardHints() {
        const bool focused = m_focusArea != nullptr && m_focusArea->focused();
        for (std::size_t i = 0; i < m_items.size(); ++i) {
          if (m_items[i].button != nullptr) {
            m_items[i].button->setKeyboardFocusHint(focused && i == m_keyboardIndex);
          }
        }
      }

      void clearKeyboardHints() {
        for (const NavItem& item : m_items) {
          if (item.button != nullptr) {
            item.button->setKeyboardFocusHint(false);
          }
        }
      }

      void scrollKeyboardItemIntoView() {
        if (m_keyboardIndex >= m_items.size() || m_items[m_keyboardIndex].button == nullptr) {
          return;
        }
        if (m_scrollNodeIntoView) {
          m_scrollNodeIntoView(m_items[m_keyboardIndex].button);
        }
      }

      void handleKey(std::uint32_t sym) {
        if (m_items.empty()) {
          return;
        }
        if (KeySymbol::isUp(sym)) {
          if (m_keyboardIndex > 0) {
            setKeyboardIndex(m_keyboardIndex - 1, true);
          }
          return;
        }
        if (KeySymbol::isDown(sym)) {
          if (m_keyboardIndex + 1 < m_items.size()) {
            setKeyboardIndex(m_keyboardIndex + 1, true);
          }
          return;
        }
        if (KeySymbol::isHome(sym)) {
          setKeyboardIndex(0, true);
          return;
        }
        if (KeySymbol::isEnd(sym)) {
          setKeyboardIndex(m_items.size() - 1, true);
          return;
        }
        if (KeySymbol::isEnterOrSpace(sym)) {
          if (m_keyboardIndex < m_items.size() && m_items[m_keyboardIndex].activate) {
            m_items[m_keyboardIndex].activate();
          }
        }
      }

      InputArea* m_focusArea = nullptr;
      std::vector<NavItem> m_items;
      std::size_t m_keyboardIndex = 0;
      std::function<void(const Node*)> m_scrollNodeIntoView;
    };

    void addNavButton(SettingsSidebarNav& nav, std::unique_ptr<Button> button, std::function<void()> onClick) {
      Button* raw = button.get();
      nav.registerNavItem(raw, onClick);
      nav.addChild(std::move(button));
    }

    std::string normalizedConfigId(std::string_view text) { return StringUtils::trim(text); }

    bool isValidConfigId(std::string_view text) {
      const auto trimmed = StringUtils::trim(text);
      if (trimmed.empty()) {
        return false;
      }
      return std::ranges::all_of(trimmed, [](unsigned char c) { return std::isalnum(c) != 0 || c == '_' || c == '-'; });
    }

    bool barNameExists(const std::vector<std::string>& barNames, std::string_view name) {
      return std::ranges::contains(barNames, name);
    }

    std::string nextAvailableBarName(const std::vector<std::string>& barNames) {
      for (std::size_t i = 1;; ++i) {
        const std::string candidate = i == 1 ? "bar" : std::format("bar_{}", i);
        if (!barNameExists(barNames, candidate)) {
          return candidate;
        }
      }
    }

    void makeButtonLabelBold(Button& button) {
      if (button.label() != nullptr) {
        button.label()->setFontWeight(FontWeight::Bold);
      }
    }

    // Primary sidebar nav style: top-level section rows with a bolder label.
    std::unique_ptr<Button> makePrimaryNavButton(
        std::string_view glyph, std::string text, float scale, bool selected, std::function<void()> onClick
    ) {
      return ui::button({
          .text = std::move(text),
          .glyph = std::string(glyph),
          .fontSize = Style::fontSizeCaption * scale,
          .glyphSize = kPrimaryNavGlyphSize * scale,
          .contentAlign = ButtonContentAlign::Start,
          .variant = selected ? ButtonVariant::TabActive : ButtonVariant::Tab,
          .minHeight = Style::controlHeightSm * scale,
          .paddingV = Style::spaceXs * scale,
          .paddingH = kPrimaryNavPaddingH * scale,
          .gap = kPrimaryNavGap * scale,
          .radius = Style::scaledRadiusMd(scale),
          .onClick = std::move(onClick),
          .configure = [](Button& button) {
            makeButtonLabelBold(button);
            button.setTabStop(false);
          },
      });
    }

    // Secondary sidebar nav style: indented compact rows for bars and monitors.
    std::unique_ptr<Button> makeSecondaryNavButton(
        std::string_view glyph, std::string text, float scale, bool selected, std::function<void()> onClick
    ) {
      return ui::button({
          .text = std::move(text),
          .glyph = std::string(glyph),
          .fontSize = Style::fontSizeCaption * scale,
          .glyphSize = Style::fontSizeCaption * scale,
          .contentAlign = ButtonContentAlign::Start,
          .variant = selected ? ButtonVariant::TabActive : ButtonVariant::Tab,
          .minHeight = Style::controlHeightSm * scale,
          .paddingTop = Style::spaceXs * scale,
          .paddingRight = Style::spaceMd * scale,
          .paddingBottom = Style::spaceXs * scale,
          .paddingLeft = Style::spaceLg * scale,
          .gap = Style::spaceXs * scale,
          .radius = Style::scaledRadiusMd(scale),
          .onClick = std::move(onClick),
          .configure = [](Button& button) { button.setTabStop(false); },
      });
    }

    std::unique_ptr<Button> makeCreateButton(std::string text, float scale, std::function<void()> onClick) {
      return ui::button({
          .text = std::move(text),
          .fontSize = Style::fontSizeCaption * scale,
          .variant = ButtonVariant::Default,
          .minHeight = Style::controlHeightSm * scale,
          .paddingV = Style::spaceXs * scale,
          .paddingH = Style::spaceSm * scale,
          .radius = Style::scaledRadiusSm(scale),
          .onClick = std::move(onClick),
      });
    }

    std::unique_ptr<Button> makeCreateCancelButton(float scale, std::function<void()> onClick) {
      return ui::button({
          .glyph = "close",
          .glyphSize = Style::fontSizeCaption * scale,
          .variant = ButtonVariant::Ghost,
          .minWidth = Style::controlHeightSm * scale,
          .minHeight = Style::controlHeightSm * scale,
          .padding = Style::spaceXs * scale,
          .radius = Style::scaledRadiusSm(scale),
          .onClick = std::move(onClick),
      });
    }

  } // namespace

  std::unique_ptr<Flex> buildSettingsSidebar(SettingsSidebarContext ctx) {
    const Config& cfg = ctx.config;
    std::vector<std::string> existingBarNames = ctx.availableBars;
    const std::string nextBarName = nextAvailableBarName(existingBarNames);

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

    auto sidebarNav = std::make_unique<SettingsSidebarNav>(std::move(ctx.scrollSidebarNodeIntoView));
    sidebarNav->setGap(kSidebarGap * scale);
    sidebarNav->setPadding(kSidebarPadding * scale);
    SettingsSidebarNav* nav = sidebarNav.get();

    for (const auto& section : ctx.sections) {
      const std::string sectionId(settingsSectionId(section));
      const bool selected = showActiveTab && sectionId == *selectedSection;
      const auto onClick = [selectedSection, scroll, sectionId, searchActive, clearTransientState, clearSearchQuery,
                            requestRebuild]() {
        if (searchActive || *selectedSection != sectionId) {
          scroll->offset = 0.0f;
        }
        *selectedSection = sectionId;
        clearSearchQuery();
        clearTransientState();
        requestRebuild();
      };
      addNavButton(
          *nav,
          makePrimaryNavButton(
              sectionGlyph(section), i18n::tr(settingsSectionLabelKey(section)), scale, selected, onClick
          ),
          onClick
      );
    }

    for (const auto& barName : ctx.availableBars) {
      const bool barSelected =
          showActiveTab && *selectedSection == "bar" && *selectedBarName == barName && selectedMonitorOverride->empty();
      const auto onBarClick = [selectedSection, selectedBarName, selectedMonitorOverride, scroll, barName, searchActive,
                               clearTransientState, clearSearchQuery, requestRebuild]() {
        if (searchActive
            || *selectedSection != "bar"
            || *selectedBarName != barName
            || !selectedMonitorOverride->empty()) {
          scroll->offset = 0.0f;
        }
        *selectedSection = "bar";
        *selectedBarName = barName;
        selectedMonitorOverride->clear();
        clearSearchQuery();
        clearTransientState();
        requestRebuild();
      };
      addNavButton(
          *nav,
          makePrimaryNavButton(
              sectionGlyph(SettingsSection::Bar), i18n::tr("settings.entities.bar.label", "name", barName), scale,
              barSelected, onBarClick
          ),
          onBarClick
      );

      const auto* bar = settings::findBar(cfg, barName);
      if (bar == nullptr) {
        continue;
      }

      for (const auto& ovr : bar->monitorOverrides) {
        const bool ovrSelected = showActiveTab
            && *selectedSection == "bar"
            && *selectedBarName == barName
            && *selectedMonitorOverride == ovr.match;
        auto match = ovr.match;
        const auto onMonitorClick = [selectedSection, selectedBarName, selectedMonitorOverride, scroll, barName, match,
                                     searchActive, clearTransientState, clearSearchQuery, requestRebuild]() {
          if (searchActive
              || *selectedSection != "bar"
              || *selectedBarName != barName
              || *selectedMonitorOverride != match) {
            scroll->offset = 0.0f;
          }
          *selectedSection = "bar";
          *selectedBarName = barName;
          *selectedMonitorOverride = match;
          clearSearchQuery();
          clearTransientState();
          requestRebuild();
        };
        addNavButton(
            *nav,
            makeSecondaryNavButton(
                "device-desktop", i18n::tr("settings.entities.monitor-override.label", "name", ovr.match), scale,
                ovrSelected, onMonitorClick
            ),
            onMonitorClick
        );
      }

      if (*selectedSection != "bar" || *selectedBarName != barName) {
        continue;
      }

      // Secondary sidebar action style: same compact indentation as monitor rows.
      const auto onNewMonitorClick = [creatingMonitorOverrideBarName, creatingMonitorOverrideMatch, barName,
                                      clearTransientState, requestRebuild]() {
        clearTransientState();
        *creatingMonitorOverrideBarName = barName;
        creatingMonitorOverrideMatch->clear();
        requestRebuild();
      };
      addNavButton(
          *nav,
          ui::button({
              .text = i18n::tr("settings.entities.monitor-override.new"),
              .glyph = "add",
              .fontSize = Style::fontSizeCaption * scale,
              .glyphSize = Style::fontSizeCaption * scale,
              .contentAlign = ButtonContentAlign::Start,
              .variant = ButtonVariant::Ghost,
              .minHeight = Style::controlHeightSm * scale,
              .paddingTop = Style::spaceXs * scale,
              .paddingRight = Style::spaceMd * scale,
              .paddingBottom = Style::spaceXs * scale,
              .paddingLeft = Style::spaceLg * scale,
              .gap = Style::spaceXs * scale,
              .radius = Style::scaledRadiusMd(scale),
              .onClick = onNewMonitorClick,
              .configure = [](Button& button) { button.setTabStop(false); },
          }),
          onNewMonitorClick
      );

      if (*creatingMonitorOverrideBarName != barName) {
        continue;
      }

      auto createPanel = ui::column({
          .align = FlexAlign::Stretch,
          .gap = Style::spaceXs * scale,
          .configure = [scale](Flex& panel) {
            panel.setPadding(0.0f, Style::spaceXs * scale, 0.0f, Style::spaceLg * scale);
          },
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
        if (match.empty() || std::ranges::contains(existingMatches, match)) {
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
      createPanel->addChild(
          ui::row(
              {
                  .align = FlexAlign::Center,
                  .gap = Style::spaceXs * scale,
              },
              makeCreateButton(
                  i18n::tr("settings.entities.monitor-override.create"), scale,
                  [doCreate, inputPtr]() mutable { doCreate(inputPtr->value()); }
              ),
              makeCreateCancelButton(
                  scale, [creatingMonitorOverrideBarName, creatingMonitorOverrideMatch, requestRebuild]() {
                    creatingMonitorOverrideBarName->clear();
                    creatingMonitorOverrideMatch->clear();
                    requestRebuild();
                  }
              )
          )
      );
      sidebarNav->addChild(std::move(createPanel));
    }

    // Primary sidebar action style: same scale as top-level section rows.
    const auto onNewBarClick = [creatingBarName, nextBarName, clearTransientState, requestRebuild]() {
      clearTransientState();
      *creatingBarName = nextBarName;
      requestRebuild();
    };
    addNavButton(
        *nav,
        ui::button({
            .text = i18n::tr("settings.entities.bar.new"),
            .glyph = "add",
            .fontSize = Style::fontSizeCaption * scale,
            .glyphSize = kPrimaryNavGlyphSize * scale,
            .contentAlign = ButtonContentAlign::Start,
            .variant = ButtonVariant::Ghost,
            .minHeight = Style::controlHeightSm * scale,
            .paddingV = Style::spaceXs * scale,
            .paddingH = kPrimaryNavPaddingH * scale,
            .gap = kPrimaryNavGap * scale,
            .radius = Style::scaledRadiusMd(scale),
            .onClick = onNewBarClick,
            .configure =
                [](Button& button) {
                  makeButtonLabelBold(button);
                  button.setTabStop(false);
                },
        }),
        onNewBarClick
    );

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
      createPanel->addChild(
          ui::row(
              {
                  .align = FlexAlign::Center,
                  .gap = Style::spaceXs * scale,
              },
              makeCreateButton(
                  i18n::tr("settings.entities.bar.create"), scale,
                  [doCreate, inputPtr]() mutable { doCreate(inputPtr->value()); }
              ),
              makeCreateCancelButton(scale, [creatingBarName, requestRebuild]() {
                creatingBarName->clear();
                requestRebuild();
              })
          )
      );
      sidebarNav->addChild(std::move(createPanel));
    }

    auto* sidebar = sidebarScroll->content();
    sidebar->setDirection(FlexDirection::Vertical);
    sidebar->setAlign(FlexAlign::Stretch);
    sidebar->addChild(std::move(sidebarNav));

    return sidebarScroll;
  }

} // namespace settings
