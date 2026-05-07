#include "shell/settings/widget_add_popup.h"

#include "config/config_service.h"
#include "i18n/i18n.h"
#include "render/scene/node.h"
#include "shell/settings/widget_settings_registry.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/label.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "wayland/wayland_connection.h"
#include "wayland/wayland_seat.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace settings {
  namespace {

    constexpr std::string_view kCreateInstancePrefix = "create-instance:";

    std::string laneLabel(std::string_view lane) {
      if (lane == "start") {
        return i18n::tr("settings.entities.widget.lanes.start");
      }
      if (lane == "center") {
        return i18n::tr("settings.entities.widget.lanes.center");
      }
      if (lane == "end") {
        return i18n::tr("settings.entities.widget.lanes.end");
      }
      return std::string(lane);
    }

    std::unique_ptr<Label> makeLabel(std::string_view text, float fontSize, const ColorSpec& color, bool bold = false) {
      auto label = std::make_unique<Label>();
      label->setText(text);
      label->setFontSize(fontSize);
      label->setColor(color);
      label->setBold(bold);
      return label;
    }

  } // namespace

  WidgetAddPopup::WidgetAddPopup(WaylandConnection& wayland, RenderContext& renderContext)
      : m_wayland(wayland), m_renderContext(renderContext),
        m_popup(std::make_unique<PopupWindow>(wayland, renderContext)) {
    m_popup->setOnDismissed([this]() {
      m_lanePath.clear();
      m_searchPicker = nullptr;
      m_focusSearchOnOpen = false;
      if (m_onDismissed) {
        m_onDismissed();
      }
    });
  }

  WidgetAddPopup::~WidgetAddPopup() = default;

  void WidgetAddPopup::setOnSelect(SelectCallback callback) { m_onSelect = std::move(callback); }

  void WidgetAddPopup::setOnDismissed(std::function<void()> callback) { m_onDismissed = std::move(callback); }

  void WidgetAddPopup::open(xdg_surface* parentXdgSurface, wl_output* output, std::uint32_t serial,
                            Button* anchorButton, const std::vector<std::string>& lanePath, const Config& config,
                            float scale, PopupWindow::AnchorMode anchorMode) {
    if (m_popup == nullptr || parentXdgSurface == nullptr || anchorButton == nullptr) {
      return;
    }

    const auto pickerEntries = widgetPickerEntries(config);
    std::vector<SearchPickerOption> options;
    options.reserve(pickerEntries.size() * 2);
    for (const auto& entry : pickerEntries) {
      options.push_back(SearchPickerOption{.value = entry.value,
                                           .label = entry.label,
                                           .description = entry.description,
                                           .category = entry.category,
                                           .enabled = true});
      if (entry.kind != WidgetReferenceKind::BuiltIn) {
        continue;
      }
      for (const auto& spec : widgetTypeSpecs()) {
        if (spec.type != entry.value || !spec.supportsMultipleInstances) {
          continue;
        }
        options.push_back(SearchPickerOption{
            .value = std::string(kCreateInstancePrefix) + entry.value,
            .label = i18n::tr("settings.entities.widget.picker.create-label", "label", entry.label),
            .description = i18n::tr("settings.entities.widget.picker.create-description", "type", entry.value),
            .category = i18n::tr("settings.entities.widget.kinds.new-instance"),
            .enabled = true,
        });
        break;
      }
    }

    if (options.empty()) {
      return;
    }

    m_lanePath = lanePath;
    m_searchPicker = nullptr;

    const float panelPadding = Style::spaceSm * scale;
    const float panelGap = Style::spaceSm * scale;
    const float panelWidth = 520.0f * scale;
    const float panelHeight = 420.0f * scale;

    m_popup->setContentBuilder([this, options, panelPadding, panelGap, scale](float width, float height) {
      auto root = std::make_unique<Flex>();
      root->setDirection(FlexDirection::Vertical);
      root->setAlign(FlexAlign::Stretch);
      root->setGap(panelGap);
      root->setPadding(panelPadding);
      root->setFill(colorSpecFromRole(ColorRole::Surface));
      root->setBorder(colorSpecFromRole(ColorRole::Outline, 0.35f), Style::borderWidth);
      root->setRadius(Style::radiusLg);
      root->setSize(width, height);

      auto header = std::make_unique<Flex>();
      header->setDirection(FlexDirection::Horizontal);
      header->setAlign(FlexAlign::Center);
      header->setGap(Style::spaceSm * scale);
      header->addChild(makeLabel(i18n::tr("settings.entities.widget.inspector.add-title", "lane",
                                          laneLabel(m_lanePath.empty() ? "" : m_lanePath.back())),
                                 Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), true));

      auto spacer = std::make_unique<Flex>();
      spacer->setFlexGrow(1.0f);
      header->addChild(std::move(spacer));

      auto closeBtn = std::make_unique<Button>();
      closeBtn->setGlyph("close");
      closeBtn->setVariant(ButtonVariant::Ghost);
      closeBtn->setGlyphSize(Style::fontSizeBody * scale);
      closeBtn->setMinWidth(Style::controlHeightSm * scale);
      closeBtn->setMinHeight(Style::controlHeightSm * scale);
      closeBtn->setPadding(Style::spaceXs * scale);
      closeBtn->setRadius(Style::radiusSm * scale);
      closeBtn->setOnClick([this]() {
        if (m_popup != nullptr) {
          m_popup->close();
        }
      });
      header->addChild(std::move(closeBtn));
      root->addChild(std::move(header));

      auto picker = std::make_unique<SearchPicker>();
      picker->setPlaceholder(i18n::tr("settings.entities.widget.picker.placeholder"));
      picker->setEmptyText(i18n::tr("settings.entities.widget.picker.empty"));
      picker->setOptions(options);
      picker->setSize(std::max(1.0f, width - panelPadding * 2.0f),
                      std::max(1.0f, height - panelPadding * 2.0f - Style::controlHeightSm * scale));
      picker->setOnActivated([this](const SearchPickerOption& option) {
        if (option.value.empty()) {
          return;
        }
        if (m_onSelect) {
          m_onSelect(m_lanePath, option.value);
        }
        if (m_popup != nullptr) {
          m_popup->close();
        }
      });
      picker->setOnCancel([this]() {
        if (m_popup != nullptr) {
          m_popup->close();
        }
      });
      m_searchPicker = picker.get();
      root->addChild(std::move(picker));
      return root;
    });

    m_popup->setSceneReadyCallback([this](InputDispatcher& dispatcher) {
      if (!m_focusSearchOnOpen || m_searchPicker == nullptr) {
        return;
      }
      auto* area = m_searchPicker->filterInputArea();
      if (area != nullptr) {
        dispatcher.setFocus(area);
        m_focusSearchOnOpen = false;
      }
    });

    float anchorAbsX = 0.0f;
    float anchorAbsY = 0.0f;
    Node::absolutePosition(anchorButton, anchorAbsX, anchorAbsY);

    auto cfg = PopupWindow::makeConfig(static_cast<std::int32_t>(anchorAbsX), static_cast<std::int32_t>(anchorAbsY),
                                       std::max(1, static_cast<std::int32_t>(anchorButton->width())),
                                       std::max(1, static_cast<std::int32_t>(anchorButton->height())),
                                       static_cast<std::uint32_t>(std::max(1.0f, panelWidth)),
                                       static_cast<std::uint32_t>(std::max(1.0f, panelHeight)), serial, anchorMode, 0,
                                       static_cast<std::int32_t>(Style::spaceXs * scale), true);

    m_focusSearchOnOpen = true;
    m_popup->openAsChild(cfg, parentXdgSurface, output);
  }

  void WidgetAddPopup::close() {
    if (m_popup != nullptr) {
      m_popup->close();
    }
  }

  bool WidgetAddPopup::isOpen() const noexcept { return m_popup != nullptr && m_popup->isOpen(); }

  bool WidgetAddPopup::onPointerEvent(const PointerEvent& event) {
    return m_popup != nullptr && m_popup->onPointerEvent(event);
  }

  void WidgetAddPopup::onKeyboardEvent(const KeyboardEvent& event) {
    if (m_popup != nullptr) {
      m_popup->onKeyboardEvent(event);
    }
  }

  void WidgetAddPopup::requestLayout() {
    if (m_popup != nullptr) {
      m_popup->requestLayout();
    }
  }

  void WidgetAddPopup::requestRedraw() {
    if (m_popup != nullptr) {
      m_popup->requestRedraw();
    }
  }

} // namespace settings
