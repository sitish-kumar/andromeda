#pragma once

#include "shell/settings/settings_content.h"
#include "ui/dialogs/dialog_popup_host.h"

#include <functional>
#include <string>

class Flex;
class RenderContext;
class WaylandConnection;
struct KeyboardEvent;
struct PointerEvent;
struct wl_output;
struct wl_surface;
struct xdg_surface;

namespace settings {

  class SessionActionsEditorPopup final : public DialogPopupHost {
  public:
    SessionActionsEditorPopup() = default;
    ~SessionActionsEditorPopup();

    void initialize(WaylandConnection& wayland, ConfigService& config, RenderContext& renderContext);

    void open(xdg_surface* parentXdgSurface, wl_output* output, std::uint32_t serial, wl_surface* parentWlSurface,
              std::uint32_t parentWidth, std::uint32_t parentHeight, float scale, std::string sheetTitle,
              std::function<void()> removeAction, std::function<void(Flex& sheetBody)> populateSheetBody);
    void close();

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool onPointerEvent(const PointerEvent& event);
    void onKeyboardEvent(const KeyboardEvent& event);
    [[nodiscard]] wl_surface* wlSurface() const noexcept;

  protected:
    void populateContent(Node* contentParent, std::uint32_t width, std::uint32_t height) override;
    void layoutSheet(float contentWidth, float contentHeight) override;
    void cancelToFacade() override;
    [[nodiscard]] InputArea* initialFocusArea() override;
    void onSheetClose() override;

  private:
    float m_scale = 1.0f;
    std::string m_sheetTitle;
    std::function<void()> m_removeAction;
    std::function<void(Flex&)> m_populateSheetBody;

    Flex* m_root = nullptr;
  };

} // namespace settings
