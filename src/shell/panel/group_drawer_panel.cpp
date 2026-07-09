#include "shell/panel/group_drawer_panel.h"

#include "config/config_service.h"
#include "shell/bar/widget_factory.h"
#include "shell/panel/panel_manager.h"
#include "ui/controls/flex.h"
#include "ui/style.h"

#include <algorithm>

GroupDrawerPanel::GroupDrawerPanel(ConfigService* config, WidgetFactory* widgetFactory, std::string groupId)
    : m_config(config), m_widgetFactory(widgetFactory), m_groupId(std::move(groupId)) {}

GroupDrawerPanel::~GroupDrawerPanel() = default;

PanelPlacement GroupDrawerPanel::panelPlacement() const noexcept {
  if (m_config != nullptr) {
    for (const auto& bar : m_config->config().bars) {
      if (const auto* g = findBarCapsuleGroupStyle(bar, m_groupId)) {
        if (g->drawerDetached) {
          return PanelPlacement::Floating;
        }
        break;
      }
    }
  }
  return PanelPlacement::Attached;
}

bool GroupDrawerPanel::panelOpenNearClick() const noexcept { return true; }

float GroupDrawerPanel::preferredWidth() const {
  const float padding = scaled(Style::panelPadding) * 2.0f;
  if (m_preferredSize.width > 0) {
    return m_preferredSize.width + padding;
  }

  std::size_t count = 0;
  std::size_t drawerColumns = 3;
  float barThickness = 40.0f;
  if (m_config != nullptr) {
    for (const auto& bar : m_config->config().bars) {
      if (const auto* g = findBarCapsuleGroupStyle(bar, m_groupId)) {
        count = g->members.size();
        drawerColumns = g->drawerColumns;
        barThickness = static_cast<float>(bar.thickness);
        break;
      }
    }
  }

  drawerColumns = std::max<std::size_t>(1, drawerColumns);
  count = std::max<std::size_t>(1, count);
  const std::size_t cols = std::min<std::size_t>(drawerColumns, count);

  const float itemSize = scaled(barThickness);
  const float gap = scaled(Style::spaceXs);
  const float contentWidth = static_cast<float>(cols) * itemSize + static_cast<float>(cols > 1 ? cols - 1 : 0) * gap;

  return contentWidth + padding;
}

float GroupDrawerPanel::preferredHeight() const {
  const float padding = scaled(Style::panelPadding) * 2.0f;
  if (m_preferredSize.height > 0) {
    return m_preferredSize.height + padding;
  }

  std::size_t count = 0;
  std::size_t drawerColumns = 3;
  if (m_config != nullptr) {
    for (const auto& bar : m_config->config().bars) {
      if (const auto* g = findBarCapsuleGroupStyle(bar, m_groupId)) {
        count = g->members.size();
        drawerColumns = g->drawerColumns;
        break;
      }
    }
  }

  drawerColumns = std::max<std::size_t>(1, drawerColumns);
  count = std::max<std::size_t>(1, count);
  const std::size_t rows = (count + drawerColumns - 1U) / drawerColumns;

  const float itemSize = scaled(Style::baseGlyphSize);
  const float gap = scaled(Style::spaceXs);
  const float contentHeight = static_cast<float>(rows) * itemSize + static_cast<float>(rows > 1 ? rows - 1 : 0) * gap;

  return contentHeight + padding;
}

void GroupDrawerPanel::create() {
  auto container = std::make_unique<Flex>();
  m_container = container.get();
  m_container->setDirection(FlexDirection::Vertical);
  m_container->setGap(scaled(Style::spaceXs));
  // Do not set padding here. PanelManager wraps the panel in a padding container.

  const BarCapsuleGroupStyle* groupStyle = nullptr;
  const BarConfig* sourceBar = nullptr;

  if (m_config != nullptr) {
    for (const auto& bar : m_config->config().bars) {
      if (const auto* g = findBarCapsuleGroupStyle(bar, m_groupId)) {
        groupStyle = g;
        sourceBar = &bar;
        break;
      }
    }
  }

  if (groupStyle != nullptr && sourceBar != nullptr && m_widgetFactory != nullptr) {
    const std::size_t columns = std::max<std::size_t>(1, groupStyle->drawerColumns);

    Flex* currentRow = nullptr;
    std::size_t colCount = 0;

    for (const auto& member : groupStyle->members) {
      auto widget = m_widgetFactory->create(
          member, PanelManager::instance().attachedPanelOutput(), contentScale(), sourceBar->position, sourceBar->name,
          static_cast<float>(sourceBar->widgetSpacing)
      );
      if (widget == nullptr) {
        continue;
      }
      widget->setConfigName(member);
      widget->create();

      if (currentRow == nullptr || colCount >= columns) {
        auto row = std::make_unique<Flex>();
        currentRow = row.get();
        currentRow->setDirection(FlexDirection::Horizontal);
        currentRow->setGap(scaled(Style::spaceXs));
        m_container->addChild(std::move(row));
        colCount = 0;
      }

      if (widget->root() != nullptr) {
        currentRow->addChild(widget->releaseRoot());
      }
      m_widgets.push_back(std::move(widget));
      ++colCount;
    }
  }

  setRoot(std::move(container));
}

void GroupDrawerPanel::onClose() {
  m_widgets.clear();
  clearReleasedRoot();
  m_container = nullptr;
  m_preferredSize = {0.0f, 0.0f};
}

void GroupDrawerPanel::setAnimationManager(AnimationManager* mgr) noexcept {
  Panel::setAnimationManager(mgr);
  for (auto& widget : m_widgets) {
    widget->setAnimationManager(mgr);
  }
}

void GroupDrawerPanel::doLayout(Renderer& renderer, float width, float height) {
  for (auto& widget : m_widgets) {
    widget->layout(renderer, width, height);
  }
  if (m_container != nullptr) {
    // Clear explicit size so measure returns the true unconstrained size
    m_container->setSize(0.0f, 0.0f);
    for (const auto& row : m_container->children()) {
      row->setSize(0.0f, 0.0f);
    }

    auto size = m_container->measure(renderer, LayoutConstraints::unconstrained());

    if (std::abs(size.width - m_preferredSize.width) > 1.0f || std::abs(size.height - m_preferredSize.height) > 1.0f) {
      m_preferredSize.width = size.width;
      m_preferredSize.height = size.height;
      PanelManager::instance().requestLayout();
    }

    m_container->setSize(width, height);
    m_container->layout(renderer);
  }
}

void GroupDrawerPanel::doUpdate(Renderer& renderer) {
  for (auto& widget : m_widgets) {
    widget->update(renderer);
  }
}
