
#include "shell/osd/media_osd.h"

#include "dbus/mpris/mpris_service.h"
#include "shell/osd/osd_overlay.h"

void MediaOsd::bindOverlay(OsdOverlay& overlay) { m_overlay = &overlay; }

OsdContent makeMprisContent(const MediaOsdData& data) {
  return OsdContent{
      .kind = OsdKind::Media,
      .icon = "music",
      .value = data.title + "-" + data.artist,
      .showProgress = false,
  };
}
void MediaOsd::onMprisChanged(const MprisService& service) {
  const auto activePlayerOpt = service.activePlayer();
  if (!activePlayerOpt.has_value())
    return;
  const auto activePlayer = activePlayerOpt.value();
  // first artist only for now
  MediaOsdData osdData = {.title = activePlayer.title, .artist = joinedArtists(activePlayer.artists)};
  if (osdData == m_lastData || m_overlay == nullptr)
    return;
  m_overlay->show(makeMprisContent(osdData));
  m_lastData = osdData;
}
