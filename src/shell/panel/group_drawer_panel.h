#pragma once

#include "shell/panel/panel.h"

#include <memory>
#include <string>
#include <vector>

class ConfigService;
class Flex;
class Widget;
class WidgetFactory;

class GroupDrawerPanel : public Panel {
public:
  GroupDrawerPanel(ConfigService* config, WidgetFactory* widgetFactory, std::string groupId);
  ~GroupDrawerPanel() override;

  [[nodiscard]] PanelPlacement panelPlacement() const noexcept override;
  [[nodiscard]] bool panelOpenNearClick() const noexcept override;
  [[nodiscard]] float preferredWidth() const override;
  [[nodiscard]] float preferredHeight() const override;
  void create() override;
  void onClose() override;
  void setAnimationManager(AnimationManager* mgr) noexcept override;

private:
  void doLayout(Renderer& renderer, float width, float height) override;
  void doUpdate(Renderer& renderer) override;

  ConfigService* m_config = nullptr;
  WidgetFactory* m_widgetFactory = nullptr;
  std::string m_groupId;

  struct Size {
    float width = 0.0f;
    float height = 0.0f;
  };
  Size m_preferredSize;

  Flex* m_container = nullptr;
  std::vector<std::unique_ptr<Widget>> m_widgets;
};
