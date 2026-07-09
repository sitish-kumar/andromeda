#include "shell/bar/widgets/group_drawer_widget.h"

#include "render/scene/input_area.h"
#include "shell/panel/panel_manager.h"
#include "ui/controls/glyph.h"
#include "ui/style.h"

#include <cmath>

GroupDrawerWidget::GroupDrawerWidget(std::string groupId, std::string glyphClosed, std::string glyphOpened)
    : m_groupId(std::move(groupId)), m_glyphClosed(std::move(glyphClosed)), m_glyphOpened(std::move(glyphOpened)) {}

void GroupDrawerWidget::create() {
  auto trigger = std::make_unique<InputArea>();
  m_trigger = trigger.get();
  m_trigger->setAcceptedButtons(InputArea::buttonMask({BTN_LEFT}));

  auto glyph = std::make_unique<Glyph>();
  m_glyph = glyph.get();
  m_glyph->setGlyph(m_glyphClosed);
  m_trigger->addChild(std::move(glyph));

  m_trigger->setOnClick([this](const InputArea::PointerData& data) {
    if (data.button != BTN_LEFT) {
      return;
    }
    const std::string panelId = "group-drawer:" + m_groupId;
    float ax = 0.0f, ay = 0.0f;
    Node::absolutePosition(m_trigger, ax, ay);
    const float anchorX = std::round(ax + m_trigger->width() * 0.5f);
    const float anchorY = std::round(ay + m_trigger->height() * 0.5f);

    if (m_panelToggleCallback) {
      m_panelToggleCallback(panelId, "", anchorX, anchorY);
    }
  });

  setRoot(std::move(trigger));
}

void GroupDrawerWidget::doLayout(Renderer& renderer, float /*containerWidth*/, float /*containerHeight*/) {
  if (m_trigger == nullptr) {
    return;
  }
  const float size = Style::baseGlyphSize * m_contentScale;
  m_glyph->setSize(size, size);

  // Arrange glyph in the center of the trigger
  m_glyph->measure(renderer);
  m_trigger->setSize(size, size);
  m_glyph->setPosition(std::round((size - m_glyph->width()) * 0.5f), std::round((size - m_glyph->height()) * 0.5f));
  m_trigger->arrange(renderer, {0, 0, size, size});
}

void GroupDrawerWidget::doUpdate(Renderer& /*renderer*/) {
  if (m_trigger == nullptr) {
    return;
  }
  const std::string panelId = "group-drawer:" + m_groupId;
  const bool isOpen = PanelManager::instance().isOpenPanel(panelId);
  if (isOpen != m_wasOpen) {
    m_wasOpen = isOpen;
    m_glyph->setGlyph(isOpen ? m_glyphOpened : m_glyphClosed);
    if (m_redrawCallback) {
      m_redrawCallback();
    }
  }
}
