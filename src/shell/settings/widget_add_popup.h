#pragma once

#include "ui/controls/popup_window.h"
#include "ui/controls/search_picker.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Button;
class InputDispatcher;
class RenderContext;
class WaylandConnection;
struct Config;
struct KeyboardEvent;
struct PointerEvent;
struct wl_output;
struct xdg_surface;

namespace settings {

  class WidgetAddPopup {
  public:
    using SelectCallback = std::function<void(const std::vector<std::string>& lanePath, const std::string& value)>;

    WidgetAddPopup(WaylandConnection& wayland, RenderContext& renderContext);
    ~WidgetAddPopup();

    void setOnSelect(SelectCallback callback);
    void setOnDismissed(std::function<void()> callback);

    void open(xdg_surface* parentXdgSurface, wl_output* output, std::uint32_t serial, Button* anchorButton,
              const std::vector<std::string>& lanePath, const Config& config, float scale,
              PopupWindow::AnchorMode anchorMode = PopupWindow::AnchorMode::CenterOnAnchor);
    void close();

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool onPointerEvent(const PointerEvent& event);
    void onKeyboardEvent(const KeyboardEvent& event);
    void requestLayout();
    void requestRedraw();

  private:
    WaylandConnection& m_wayland;
    RenderContext& m_renderContext;
    std::unique_ptr<PopupWindow> m_popup;
    std::vector<std::string> m_lanePath;
    SearchPicker* m_searchPicker = nullptr;
    bool m_focusSearchOnOpen = false;
    SelectCallback m_onSelect;
    std::function<void()> m_onDismissed;
  };

} // namespace settings
