
#include "shell/osd/mpris_osd.h"

#include "dbus/mpris/mpris_service.h"
#include "shell/osd/osd_overlay.h"

void MprisOsd::bindOverlay(OsdOverlay& overlay) { m_overlay = &overlay; }

OsdContent makeMprisContent(const MprisOsdData& data) {
  return OsdContent{
      .kind = OsdKind::Mpris,
      .icon = "music",
      .value = data.Title + "-" + data.Artist,
      .showProgress = false,
  };
}
void MprisOsd::onMprisChanged(const MprisService& service) {
  const auto activePlayerOpt = service.activePlayer();
  if (!activePlayerOpt.has_value())
    return;
  const auto activePlayer = activePlayerOpt.value();
  // first artist only for now
  MprisOsdData osd_data = {.Title = activePlayer.title, .Artist = activePlayer.artists[0]};
  if (osd_data == lastData)
    return;
  m_overlay->show(makeMprisContent(osd_data));
  lastData = osd_data;
}
