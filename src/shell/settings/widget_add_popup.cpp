#include "shell/settings/widget_add_popup.h"

#include "config/config_service.h"
#include "core/deferred_call.h"
#include "i18n/i18n.h"
#include "render/render_context.h"
#include "render/scene/input_area.h"
#include "render/scene/node.h"
#include "shell/settings/widget_settings_registry.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/label.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "wayland/wayland_connection.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cmath>
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

    PopupSurfaceConfig makePopupConfig(std::int32_t anchorX, std::int32_t anchorY, std::int32_t anchorWidth,
                                       std::int32_t anchorHeight, std::uint32_t width, std::uint32_t height,
                                       std::uint32_t serial, std::int32_t offsetX = 0, std::int32_t offsetY = 0,
                                       bool grab = true) {
      PopupSurfaceConfig cfg{
          .anchorX = anchorX,
          .anchorY = anchorY,
          .anchorWidth = std::max(1, anchorWidth),
          .anchorHeight = std::max(1, anchorHeight),
          .width = std::max<std::uint32_t>(1, width),
          .height = std::max<std::uint32_t>(1, height),
          .anchor = XDG_POSITIONER_ANCHOR_BOTTOM,
          .gravity = XDG_POSITIONER_GRAVITY_BOTTOM,
          .constraintAdjustment =
              XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
              XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y,
          .offsetX = offsetX,
          .offsetY = offsetY,
          .serial = serial,
          .grab = grab,
      };

      cfg.anchorX += cfg.anchorWidth / 2;
      cfg.anchorY += cfg.anchorHeight / 2;
      cfg.anchorWidth = 1;
      cfg.anchorHeight = 1;
      return cfg;
    }

  } // namespace

  WidgetAddPopup::~WidgetAddPopup() { destroyPopup(); }

  void WidgetAddPopup::initialize(WaylandConnection& wayland, ConfigService& config, RenderContext& renderContext) {
    initializeBase(wayland, config, renderContext);
  }

  void WidgetAddPopup::setOnSelect(SelectCallback callback) { m_onSelect = std::move(callback); }

  void WidgetAddPopup::setOnDismissed(std::function<void()> callback) { m_onDismissed = std::move(callback); }

  void WidgetAddPopup::open(xdg_surface* parentXdgSurface, wl_output* output, std::uint32_t serial,
                            Button* anchorButton, wl_surface* parentWlSurface, const std::vector<std::string>& lanePath,
                            const Config& config, float scale) {
    if (parentXdgSurface == nullptr || parentWlSurface == nullptr || anchorButton == nullptr) {
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

    if (isOpen()) {
      close();
    }

    m_scale = std::max(0.1f, scale);
    m_options = std::move(options);
    m_lanePath = lanePath;
    m_searchPicker = nullptr;
    m_root = nullptr;

    const float panelWidth = 520.0f * m_scale;
    const float panelHeight = 420.0f * m_scale;

    float anchorAbsX = 0.0f;
    float anchorAbsY = 0.0f;
    Node::absolutePosition(anchorButton, anchorAbsX, anchorAbsY);

    const auto cfg = makePopupConfig(static_cast<std::int32_t>(anchorAbsX), static_cast<std::int32_t>(anchorAbsY),
                                     std::max(1, static_cast<std::int32_t>(anchorButton->width())),
                                     std::max(1, static_cast<std::int32_t>(anchorButton->height())),
                                     static_cast<std::uint32_t>(std::max(1.0f, panelWidth)),
                                     static_cast<std::uint32_t>(std::max(1.0f, panelHeight)), serial, 0,
                                     static_cast<std::int32_t>(Style::spaceXs * m_scale), true);

    (void)openPopupAsChild(cfg, parentXdgSurface, parentWlSurface, output);
  }

  void WidgetAddPopup::close() { destroyPopup(); }

  bool WidgetAddPopup::isOpen() const noexcept { return DialogPopupHost::isOpen(); }

  bool WidgetAddPopup::onPointerEvent(const PointerEvent& event) { return DialogPopupHost::onPointerEvent(event); }

  void WidgetAddPopup::onKeyboardEvent(const KeyboardEvent& event) { DialogPopupHost::onKeyboardEvent(event); }

  wl_surface* WidgetAddPopup::wlSurface() const noexcept { return DialogPopupHost::wlSurface(); }

  void WidgetAddPopup::requestLayout() { DialogPopupHost::requestLayout(); }

  void WidgetAddPopup::requestRedraw() { DialogPopupHost::requestRedraw(); }

  void WidgetAddPopup::populateContent(Node* contentParent, std::uint32_t /*width*/, std::uint32_t /*height*/) {
    const float panelPadding = Style::spaceSm * m_scale;
    const float panelGap = Style::spaceSm * m_scale;

    auto root = std::make_unique<Flex>();
    root->setDirection(FlexDirection::Vertical);
    root->setAlign(FlexAlign::Stretch);
    root->setGap(panelGap);
    root->setPadding(panelPadding);
    root->setFill(colorSpecFromRole(ColorRole::Surface));
    root->setBorder(colorSpecFromRole(ColorRole::Outline, 0.35f), Style::borderWidth);
    root->setRadius(Style::radiusLg);
    m_root = root.get();

    auto header = std::make_unique<Flex>();
    header->setDirection(FlexDirection::Horizontal);
    header->setAlign(FlexAlign::Center);
    header->setGap(Style::spaceSm * m_scale);
    header->addChild(makeLabel(i18n::tr("settings.entities.widget.inspector.add-title", "lane",
                                        laneLabel(m_lanePath.empty() ? "" : m_lanePath.back())),
                               Style::fontSizeBody * m_scale, colorSpecFromRole(ColorRole::OnSurface), true));

    auto spacer = std::make_unique<Flex>();
    spacer->setFlexGrow(1.0f);
    header->addChild(std::move(spacer));

    auto closeBtn = std::make_unique<Button>();
    closeBtn->setGlyph("close");
    closeBtn->setVariant(ButtonVariant::Ghost);
    closeBtn->setGlyphSize(Style::fontSizeBody * m_scale);
    closeBtn->setMinWidth(Style::controlHeightSm * m_scale);
    closeBtn->setMinHeight(Style::controlHeightSm * m_scale);
    closeBtn->setPadding(Style::spaceXs * m_scale);
    closeBtn->setRadius(Style::radiusSm * m_scale);
    closeBtn->setOnClick([this]() { DeferredCall::callLater([this]() { close(); }); });
    header->addChild(std::move(closeBtn));
    root->addChild(std::move(header));

    auto picker = std::make_unique<SearchPicker>();
    picker->setPlaceholder(i18n::tr("settings.entities.widget.picker.placeholder"));
    picker->setEmptyText(i18n::tr("settings.entities.widget.picker.empty"));
    picker->setOptions(m_options);
    picker->setOnActivated([this](const SearchPickerOption& option) {
      if (option.value.empty()) {
        return;
      }
      if (m_onSelect) {
        m_onSelect(m_lanePath, option.value);
      }
      DeferredCall::callLater([this]() { close(); });
    });
    picker->setOnCancel([this]() { DeferredCall::callLater([this]() { close(); }); });
    m_searchPicker = picker.get();
    root->addChild(std::move(picker));

    contentParent->addChild(std::move(root));
  }

  void WidgetAddPopup::layoutSheet(float contentWidth, float contentHeight) {
    if (m_root == nullptr || m_searchPicker == nullptr || renderContext() == nullptr) {
      return;
    }

    const float panelPadding = Style::spaceSm * m_scale;
    m_root->setSize(contentWidth, contentHeight);
    const float pickerWidth = std::max(1.0f, contentWidth - panelPadding * 2.0f);
    const float pickerHeight = std::max(1.0f, contentHeight - panelPadding * 2.0f - Style::controlHeightSm * m_scale);
    m_searchPicker->setSize(pickerWidth, pickerHeight);
    m_root->layout(*renderContext());
  }

  void WidgetAddPopup::cancelToFacade() {}

  InputArea* WidgetAddPopup::initialFocusArea() {
    return m_searchPicker != nullptr ? m_searchPicker->filterInputArea() : nullptr;
  }

  void WidgetAddPopup::onSheetClose() {
    m_options.clear();
    m_lanePath.clear();
    m_root = nullptr;
    m_searchPicker = nullptr;
    if (m_onDismissed) {
      m_onDismissed();
    }
  }

} // namespace settings
