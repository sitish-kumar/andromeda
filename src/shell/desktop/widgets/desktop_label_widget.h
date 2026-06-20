#pragma once

#include "shell/desktop/desktop_widget.h"
#include "ui/palette.h"

#include <string>

class Label;

class DesktopLabelWidget : public DesktopWidget {
public:
  DesktopLabelWidget(std::string title, std::string description, ColorSpec color, float opacity, bool shadow);

  void create() override;
  bool applySetting(
      const std::string& key, const WidgetSettingValue& value,
      const std::unordered_map<std::string, WidgetSettingValue>& allSettings, Renderer& renderer
  ) override;

private:
  void doLayout(Renderer& renderer) override;
  void onFontFamilyChanged(const std::string& family, Renderer& renderer) override;
  void applyLabelColors();
  void applyShadow();

  std::string m_title;
  std::string m_description;
  ColorSpec m_color;
  float m_opacity = 1.0f;
  bool m_shadow;
  Label* m_titleLabel = nullptr;
  Label* m_descriptionLabel = nullptr;
};
