#pragma once

#include "shell/bar/widget.h"

#include <string>

class InputArea;
class Glyph;

class GroupDrawerWidget : public Widget {
public:
  GroupDrawerWidget(std::string groupId, std::string glyphClosed, std::string glyphOpened);

  void create() override;
  [[nodiscard]] bool wantsBarHoverHighlight() const noexcept override { return true; }

private:
  void doLayout(Renderer& renderer, float containerWidth, float containerHeight) override;
  void doUpdate(Renderer& renderer) override;

  std::string m_groupId;
  std::string m_glyphClosed;
  std::string m_glyphOpened;

  InputArea* m_trigger = nullptr;
  Glyph* m_glyph = nullptr;
  bool m_wasOpen = false;
};
