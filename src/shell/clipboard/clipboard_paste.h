#pragma once

#include "config/config_service.h"
#include "wayland/virtual_keyboard_service.h"

namespace clipboard_paste {

  [[nodiscard]] bool pasteEntry(bool isImage, ClipboardAutoPasteMode mode, VirtualKeyboardService& virtualKeyboard);

} // namespace clipboard_paste
