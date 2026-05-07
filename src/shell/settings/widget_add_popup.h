#pragma once

#include "ui/controls/search_picker.h"
#include "ui/dialogs/dialog_popup_host.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Button;
class Node;
class ConfigService;
class RenderContext;
class WaylandConnection;
struct Config;
struct KeyboardEvent;
struct PointerEvent;
struct wl_output;
struct xdg_surface;

namespace settings {

  class WidgetAddPopup final : public DialogPopupHost {
  public:
    using SelectCallback = std::function<void(const std::vector<std::string>& lanePath, const std::string& value)>;

    WidgetAddPopup() = default;
    ~WidgetAddPopup();

    void initialize(WaylandConnection& wayland, ConfigService& config, RenderContext& renderContext);

    void setOnSelect(SelectCallback callback);
    void setOnDismissed(std::function<void()> callback);

    void open(xdg_surface* parentXdgSurface, wl_output* output, std::uint32_t serial, Button* anchorButton,
              wl_surface* parentWlSurface, const std::vector<std::string>& lanePath, const Config& config, float scale);
    void close();

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool onPointerEvent(const PointerEvent& event);
    void onKeyboardEvent(const KeyboardEvent& event);
    [[nodiscard]] wl_surface* wlSurface() const noexcept;
    void requestLayout();
    void requestRedraw();

  protected:
    void populateContent(Node* contentParent, std::uint32_t width, std::uint32_t height) override;
    void layoutSheet(float contentWidth, float contentHeight) override;
    void cancelToFacade() override;
    [[nodiscard]] InputArea* initialFocusArea() override;
    void onSheetClose() override;

  private:
    std::vector<SearchPickerOption> m_options;
    float m_scale = 1.0f;
    std::vector<std::string> m_lanePath;
    Flex* m_root = nullptr;
    SearchPicker* m_searchPicker = nullptr;
    SelectCallback m_onSelect;
    std::function<void()> m_onDismissed;
  };

} // namespace settings
