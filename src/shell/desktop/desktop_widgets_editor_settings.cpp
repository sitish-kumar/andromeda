#include "cursor-shape-v1-client-protocol.h"
#include "i18n/i18n.h"
#include "render/render_context.h"
#include "render/scene/input_area.h"
#include "shell/desktop/desktop_widgets_editor.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/scroll_view.h"
#include "ui/controls/segmented.h"
#include "ui/controls/select.h"
#include "ui/controls/separator.h"
#include "ui/controls/slider.h"
#include "ui/controls/toggle.h"
#include "ui/dialogs/color_picker_dialog.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "wayland/layer_surface.h"

#include <array>
#include <linux/input-event-codes.h>

namespace {

  constexpr float kInspectorWidth = 340.0f;
  constexpr float kSettingRowHeight = 34.0f;
  constexpr float kLabelWidth = 100.0f;
  constexpr float kDefaultAspectRatio = 240.0f / 96.0f;

  using Settings = std::unordered_map<std::string, WidgetSettingValue>;

  std::string getStr(const Settings& s, const std::string& key, const std::string& fallback = {}) {
    const auto it = s.find(key);
    if (it == s.end()) {
      return fallback;
    }
    if (const auto* v = std::get_if<std::string>(&it->second)) {
      return *v;
    }
    return fallback;
  }

  float getFloat(const Settings& s, const std::string& key, float fallback) {
    const auto it = s.find(key);
    if (it == s.end()) {
      return fallback;
    }
    if (const auto* v = std::get_if<double>(&it->second)) {
      return static_cast<float>(*v);
    }
    if (const auto* v = std::get_if<std::int64_t>(&it->second)) {
      return static_cast<float>(*v);
    }
    return fallback;
  }

  bool getBool(const Settings& s, const std::string& key, bool fallback) {
    const auto it = s.find(key);
    if (it == s.end()) {
      return fallback;
    }
    if (const auto* v = std::get_if<bool>(&it->second)) {
      return *v;
    }
    return fallback;
  }

  ColorSpec getColorSpec(const Settings& s, const std::string& key, const ColorSpec& fallback) {
    const auto it = s.find(key);
    if (it == s.end()) {
      return fallback;
    }
    if (const auto* v = std::get_if<std::string>(&it->second)) {
      return colorSpecFromConfigString(*v);
    }
    return fallback;
  }

  std::unique_ptr<Flex> makeRow(std::string_view labelText, std::unique_ptr<Node> control) {
    auto row = std::make_unique<Flex>();
    row->setDirection(FlexDirection::Horizontal);
    row->setAlign(FlexAlign::Center);
    row->setJustify(FlexJustify::SpaceBetween);
    row->setGap(Style::spaceSm);
    row->setMinHeight(kSettingRowHeight);
    row->setFillWidth(true);

    auto labelBox = std::make_unique<Flex>();
    labelBox->setDirection(FlexDirection::Horizontal);
    labelBox->setAlign(FlexAlign::Center);
    labelBox->setMinWidth(kLabelWidth);
    auto label = std::make_unique<Label>();
    label->setText(std::string(labelText));
    label->setFontSize(Style::fontSizeCaption);
    label->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
    labelBox->addChild(std::move(label));
    row->addChild(std::move(labelBox));

    row->addChild(std::move(control));
    return row;
  }

  std::unique_ptr<Flex> makeToggleRow(std::string_view labelText, const std::string& key, bool fallback,
                                      const Settings& s, DesktopWidgetsEditor* editor) {
    auto toggle = std::make_unique<Toggle>();
    toggle->setChecked(getBool(s, key, fallback));
    toggle->setOnChange([editor, key](bool checked) { editor->applySettingChange(key, checked); });
    return makeRow(labelText, std::move(toggle));
  }

  std::unique_ptr<Flex> makeSliderRow(std::string_view labelText, const std::string& key, float fallback, float minVal,
                                      float maxVal, float step, const Settings& s, DesktopWidgetsEditor* editor) {
    auto slider = std::make_unique<Slider>();
    slider->setRange(minVal, maxVal);
    slider->setStep(step);
    slider->setValue(getFloat(s, key, fallback));
    slider->setFlexGrow(1.0f);
    slider->setOnValueChanged([editor, key](float val) { editor->applySettingChange(key, static_cast<double>(val)); });
    return makeRow(labelText, std::move(slider));
  }

  std::unique_ptr<Flex> makeColorButton(std::string_view labelText, const std::string& key, const ColorSpec& fallback,
                                        const Settings& s, DesktopWidgetsEditor* editor) {
    auto colorSpec = getColorSpec(s, key, fallback);
    auto color = resolveColorSpec(colorSpec);

    auto btn = std::make_unique<Button>();
    btn->setText(formatRgbHex(color));
    btn->setVariant(ButtonVariant::Outline);
    btn->setFlexGrow(1.0f);
    btn->setOnClick([editor, key, color]() { editor->openColorPicker(key, color); });
    return makeRow(labelText, std::move(btn));
  }

  void addClockSettings(Flex& content, const Settings& s, DesktopWidgetsEditor* editor) {
    auto input = std::make_unique<Input>();
    input->setValue(getStr(s, "format", "{:%H:%M}"));
    input->setPlaceholder("{:%H:%M}");
    input->setControlHeight(Style::controlHeightSm);
    input->setFlexGrow(1.0f);
    input->setOnChange([editor](const std::string& val) { editor->applySettingChange("format", val); });
    content.addChild(makeRow(i18n::tr("desktop-widgets.editor.settings.format"), std::move(input)));
    content.addChild(makeColorButton(i18n::tr("desktop-widgets.editor.settings.color"), "color",
                                     colorSpecFromRole(ColorRole::OnSurface), s, editor));
    content.addChild(makeToggleRow(i18n::tr("desktop-widgets.editor.settings.shadow"), "shadow", true, s, editor));
  }

  void addAudioVisualizerSettings(Flex& content, const Settings& s, DesktopWidgetsEditor* editor) {
    content.addChild(makeSliderRow(i18n::tr("desktop-widgets.editor.settings.aspect-ratio"), "aspect_ratio",
                                   kDefaultAspectRatio, 0.5f, 6.0f, 0.1f, s, editor));
    content.addChild(makeSliderRow(i18n::tr("desktop-widgets.editor.settings.bands"), "bands", 32.0f, 4.0f, 128.0f,
                                   4.0f, s, editor));
    content.addChild(makeToggleRow(i18n::tr("desktop-widgets.editor.settings.mirrored"), "mirrored", true, s, editor));
    content.addChild(makeColorButton(i18n::tr("desktop-widgets.editor.settings.low-color"), "low_color",
                                     colorSpecFromRole(ColorRole::Primary), s, editor));
    content.addChild(makeColorButton(i18n::tr("desktop-widgets.editor.settings.high-color"), "high_color",
                                     colorSpecFromRole(ColorRole::Primary), s, editor));
  }

  void addStickerSettings(Flex& content, const Settings& s, DesktopWidgetsEditor* editor) {
    auto changeBtn = std::make_unique<Button>();
    changeBtn->setText(i18n::tr("desktop-widgets.editor.settings.change-image"));
    changeBtn->setVariant(ButtonVariant::Outline);
    changeBtn->setFlexGrow(1.0f);
    changeBtn->setOnClick([editor]() {
      FileDialogOptions options;
      options.mode = FileDialogMode::Open;
      options.title = i18n::tr("desktop-widgets.editor.dialogs.select-sticker-image");
      options.extensions = {".png", ".jpg", ".jpeg", ".webp", ".svg", ".gif"};
      (void)FileDialog::open(std::move(options), [editor](std::optional<std::filesystem::path> result) {
        if (result) {
          editor->applySettingChange("image_path", result->string());
        }
      });
    });
    content.addChild(makeRow(i18n::tr("desktop-widgets.editor.settings.image-path"), std::move(changeBtn)));
    content.addChild(makeSliderRow(i18n::tr("desktop-widgets.editor.settings.opacity"), "opacity", 1.0f, 0.0f, 1.0f,
                                   0.01f, s, editor));
  }

  void addWeatherSettings(Flex& content, const Settings& s, DesktopWidgetsEditor* editor) {
    content.addChild(makeColorButton(i18n::tr("desktop-widgets.editor.settings.color"), "color",
                                     colorSpecFromRole(ColorRole::OnSurface), s, editor));
    content.addChild(makeToggleRow(i18n::tr("desktop-widgets.editor.settings.shadow"), "shadow", true, s, editor));
  }

  void addMediaPlayerSettings(Flex& content, const Settings& s, DesktopWidgetsEditor* editor) {
    auto segmented = std::make_unique<Segmented>();
    segmented->addOption(i18n::tr("desktop-widgets.editor.settings.horizontal"));
    segmented->addOption(i18n::tr("desktop-widgets.editor.settings.vertical"));
    segmented->setSelectedIndex(getStr(s, "layout", "horizontal") == "vertical" ? 1 : 0);
    segmented->setFlexGrow(1.0f);
    segmented->setOnChange([editor](std::size_t index) {
      editor->applySettingChange("layout", std::string(index == 1 ? "vertical" : "horizontal"));
    });
    content.addChild(makeRow(i18n::tr("desktop-widgets.editor.settings.layout"), std::move(segmented)));
    content.addChild(makeColorButton(i18n::tr("desktop-widgets.editor.settings.color"), "color",
                                     colorSpecFromRole(ColorRole::OnSurface), s, editor));
    content.addChild(makeToggleRow(i18n::tr("desktop-widgets.editor.settings.shadow"), "shadow", true, s, editor));
  }

  void addSysmonSettings(Flex& content, const Settings& s, DesktopWidgetsEditor* editor) {
    const std::vector<std::string> statOptions = {
        i18n::tr("desktop-widgets.editor.settings.stat-cpu-usage"),
        i18n::tr("desktop-widgets.editor.settings.stat-cpu-temp"),
        i18n::tr("desktop-widgets.editor.settings.stat-gpu-temp"),
        i18n::tr("desktop-widgets.editor.settings.stat-ram-pct"),
        i18n::tr("desktop-widgets.editor.settings.stat-swap-pct"),
        i18n::tr("desktop-widgets.editor.settings.stat-net-rx"),
        i18n::tr("desktop-widgets.editor.settings.stat-net-tx"),
    };
    const std::array<const char*, 7> statKeys = {"cpu_usage", "cpu_temp", "gpu_temp", "ram_pct",
                                                 "swap_pct",  "net_rx",   "net_tx"};

    auto statToIndex = [&](const std::string& val) -> std::size_t {
      for (std::size_t i = 0; i < statKeys.size(); ++i) {
        if (val == statKeys[i]) {
          return i;
        }
      }
      return 0;
    };

    auto statSelect = std::make_unique<Select>();
    statSelect->setOptions(statOptions);
    statSelect->setSelectedIndex(statToIndex(getStr(s, "stat", "cpu_usage")));
    statSelect->setControlHeight(Style::controlHeightSm);
    statSelect->setFlexGrow(1.0f);
    statSelect->setOnSelectionChanged([editor, statKeys](std::size_t index, std::string_view) {
      if (index < statKeys.size()) {
        editor->applySettingChange("stat", std::string(statKeys[index]));
      }
    });
    content.addChild(makeRow(i18n::tr("desktop-widgets.editor.settings.stat"), std::move(statSelect)));

    auto stat2Options = statOptions;
    stat2Options.insert(stat2Options.begin(), i18n::tr("desktop-widgets.editor.settings.stat-none"));
    auto stat2Select = std::make_unique<Select>();
    stat2Select->setOptions(stat2Options);
    const auto stat2Str = getStr(s, "stat2");
    stat2Select->setSelectedIndex(stat2Str.empty() ? 0 : (statToIndex(stat2Str) + 1));
    stat2Select->setControlHeight(Style::controlHeightSm);
    stat2Select->setFlexGrow(1.0f);
    stat2Select->setOnSelectionChanged([editor, statKeys](std::size_t index, std::string_view) {
      if (index == 0) {
        editor->applySettingChange("stat2", std::string());
      } else if (index - 1 < statKeys.size()) {
        editor->applySettingChange("stat2", std::string(statKeys[index - 1]));
      }
    });
    content.addChild(makeRow(i18n::tr("desktop-widgets.editor.settings.stat2"), std::move(stat2Select)));

    content.addChild(makeColorButton(i18n::tr("desktop-widgets.editor.settings.color"), "color",
                                     colorSpecFromRole(ColorRole::Primary), s, editor));
    content.addChild(makeColorButton(i18n::tr("desktop-widgets.editor.settings.color2"), "color2",
                                     colorSpecFromRole(ColorRole::Secondary), s, editor));
    content.addChild(
        makeToggleRow(i18n::tr("desktop-widgets.editor.settings.show-label"), "show_label", true, s, editor));
    content.addChild(makeToggleRow(i18n::tr("desktop-widgets.editor.settings.shadow"), "shadow", true, s, editor));
  }

  void addBackgroundSettings(Flex& content, const Settings& s, DesktopWidgetsEditor* editor) {
    auto sep = std::make_unique<Separator>();
    sep->setOrientation(SeparatorOrientation::HorizontalRule);
    content.addChild(std::move(sep));

    auto heading = std::make_unique<Label>();
    heading->setText(i18n::tr("desktop-widgets.editor.settings.background-section"));
    heading->setBold(true);
    heading->setFontSize(Style::fontSizeCaption);
    heading->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
    content.addChild(std::move(heading));

    content.addChild(
        makeToggleRow(i18n::tr("desktop-widgets.editor.settings.background"), "background", true, s, editor));
    content.addChild(makeColorButton(i18n::tr("desktop-widgets.editor.settings.background-color"), "background_color",
                                     colorSpecFromRole(ColorRole::Surface, 0.8f), s, editor));
    content.addChild(makeSliderRow(i18n::tr("desktop-widgets.editor.settings.background-radius"), "background_radius",
                                   12.0f, 0.0f, 32.0f, 1.0f, s, editor));
    content.addChild(makeSliderRow(i18n::tr("desktop-widgets.editor.settings.background-padding"), "background_padding",
                                   10.0f, 0.0f, 32.0f, 1.0f, s, editor));
  }

  void addWidgetTypeSettings(Flex& content, const DesktopWidgetState& state, DesktopWidgetsEditor* editor) {
    const auto& s = state.settings;
    if (state.type == "clock") {
      addClockSettings(content, s, editor);
    } else if (state.type == "audio_visualizer") {
      addAudioVisualizerSettings(content, s, editor);
    } else if (state.type == "sticker") {
      addStickerSettings(content, s, editor);
    } else if (state.type == "weather") {
      addWeatherSettings(content, s, editor);
    } else if (state.type == "media_player") {
      addMediaPlayerSettings(content, s, editor);
    } else if (state.type == "sysmon") {
      addSysmonSettings(content, s, editor);
    }
  }

} // namespace

void DesktopWidgetsEditor::openColorPicker(const std::string& key, const Color& currentColor) {
  ColorPickerDialogOptions options;
  options.initialColor = currentColor;
  options.title = key;
  (void)ColorPickerDialog::open(std::move(options), [this, key](std::optional<Color> result) {
    if (result) {
      applySettingChange(key, formatRgbHex(*result));
    }
  });
}

void DesktopWidgetsEditor::applySettingChange(const std::string& key, WidgetSettingValue value) {
  deferEditorMutation([this, key, value = std::move(value)]() {
    auto* state = findWidgetState(m_selectedWidgetId);
    if (state == nullptr) {
      return;
    }
    state->settings[key] = value;

    OverlaySurface* surface = findSurfaceForWidget(m_selectedWidgetId);
    if (surface == nullptr) {
      return;
    }
    auto viewIt = surface->views.find(m_selectedWidgetId);
    if (viewIt == surface->views.end()) {
      return;
    }

    auto& view = viewIt->second;
    if (view.transformNode == nullptr) {
      return;
    }

    auto newWidget = m_factory->create(state->type, state->settings, widgetContentScale(*state));
    if (newWidget == nullptr) {
      return;
    }

    if (view.widget != nullptr) {
      const auto& children = view.transformNode->children();
      for (const auto& child : children) {
        view.transformNode->removeChild(child.get());
        break;
      }
    }

    newWidget->create();
    newWidget->setAnimationManager(&surface->animations);
    auto* surfacePtr = surface;
    newWidget->setUpdateCallback([surfacePtr]() {
      if (surfacePtr->surface != nullptr) {
        surfacePtr->surface->requestUpdateOnly();
      }
    });
    newWidget->setLayoutCallback([surfacePtr]() {
      if (surfacePtr->surface != nullptr) {
        surfacePtr->surface->requestUpdate();
      }
    });
    newWidget->setRedrawCallback([surfacePtr]() {
      if (surfacePtr->surface != nullptr) {
        surfacePtr->surface->requestRedraw();
      }
    });
    newWidget->setFrameTickRequestCallback([surfacePtr]() {
      if (surfacePtr->surface != nullptr) {
        surfacePtr->surface->requestFrameTick();
      }
    });
    newWidget->update(*m_renderContext);
    newWidget->layout(*m_renderContext);

    view.intrinsicWidth = std::max(1.0f, newWidget->intrinsicWidth());
    view.intrinsicHeight = std::max(1.0f, newWidget->intrinsicHeight());
    view.transformNode->addChild(newWidget->releaseRoot());
    view.widget = std::move(newWidget);

    applyViewState(view, *state, false);
    updateSelectionVisuals(*surface);
    surface->surface->requestRedraw();
  });
}

void DesktopWidgetsEditor::buildInspector(OverlaySurface& surface, Node& root,
                                          const DesktopWidgetState& selectedState) {
  auto panel = std::make_unique<Flex>();
  panel->setDirection(FlexDirection::Vertical);
  panel->setGap(0.0f);
  panel->setFill(colorSpecFromRole(ColorRole::Surface, 0.94f));
  panel->setBorder(colorSpecFromRole(ColorRole::Outline), Style::borderWidth);
  panel->setRadius(Style::radiusXl);
  panel->setZIndex(201);
  panel->setMinWidth(kInspectorWidth);
  panel->setMaxWidth(kInspectorWidth);

  // Drag handle
  auto handle = std::make_unique<Flex>();
  handle->setDirection(FlexDirection::Horizontal);
  handle->setAlign(FlexAlign::Center);
  handle->setGap(Style::spaceXs);
  handle->setPadding(Style::spaceXs, Style::spaceMd);
  handle->setFill(colorSpecFromRole(ColorRole::SurfaceVariant, 0.85f));
  handle->setRadius(Style::radiusLg);
  handle->setMinHeight(Style::controlHeightSm);
  handle->setFillWidth(true);

  auto handleGlyph = std::make_unique<Glyph>();
  handleGlyph->setGlyph("menu-2");
  handleGlyph->setGlyphSize(14.0f);
  handle->addChild(std::move(handleGlyph));

  auto handleTitle = std::make_unique<Label>();
  handleTitle->setText(i18n::tr("desktop-widgets.editor.settings.title"));
  handleTitle->setBold(true);
  handleTitle->setFontSize(Style::fontSizeBody);
  handle->addChild(std::move(handleTitle));

  auto handleArea = std::make_unique<InputArea>();
  handleArea->setParticipatesInLayout(false);
  handleArea->setZIndex(1);
  handleArea->setCursorShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE);
  handleArea->setOnPress([this, outputName = surface.outputName](const InputArea::PointerData& data) {
    if (data.button != BTN_LEFT) {
      return;
    }
    if (data.pressed) {
      startInspectorDrag(outputName);
    } else if (m_drag.mode == DragMode::InspectorMove && m_drag.surfaceOutputName == outputName) {
      finishDrag();
    }
  });
  handleArea->setOnMotion([this, outputName = surface.outputName](const InputArea::PointerData&) {
    if (m_drag.mode == DragMode::InspectorMove && m_drag.surfaceOutputName == outputName) {
      updateDrag();
    }
  });
  auto* handlePtr = handle.get();
  auto* handleAreaPtr = handleArea.get();
  handle->addChild(std::move(handleArea));
  panel->addChild(std::move(handle));

  auto scrollView = std::make_unique<ScrollView>();
  scrollView->setSize(kInspectorWidth, 0.0f);

  auto* content = scrollView->content();
  content->setDirection(FlexDirection::Vertical);
  content->setGap(Style::spaceXs);
  content->setPadding(Style::spaceSm, Style::spaceMd);

  addWidgetTypeSettings(*content, selectedState, this);
  addBackgroundSettings(*content, selectedState.settings, this);

  panel->addChild(std::move(scrollView));

  auto* panelPtr = panel.get();
  surface.inspector = panelPtr;
  root.addChild(std::move(panel));
  panelPtr->layout(*m_renderContext);
  handleAreaPtr->setPosition(0.0f, 0.0f);
  handleAreaPtr->setFrameSize(handlePtr->width(), handlePtr->height());

  if (!surface.inspectorPositionInitialized && surface.toolbar != nullptr) {
    surface.inspectorX = surface.toolbarX;
    surface.inspectorY = surface.toolbarY + surface.toolbar->height() + Style::spaceSm;
    surface.inspectorPositionInitialized = true;
  }
  clampInspectorPosition(surface, panelPtr->width(), panelPtr->height());
  panelPtr->setPosition(surface.inspectorX, surface.inspectorY);
}
