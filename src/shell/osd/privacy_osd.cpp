#include "shell/osd/privacy_osd.h"

#include "i18n/i18n.h"
#include "pipewire/pipewire_service.h"
#include "shell/osd/osd_overlay.h"

namespace {

  enum class PrivacyKind {
    Mic,
    Camera,
    Screen,
  };

  OsdContent makePrivacyContent(PrivacyKind kind, bool active) {
    switch (kind) {
    case PrivacyKind::Mic:
      return OsdContent{
          .kind = OsdKind::Privacy,
          .icon = active ? "microphone" : "microphone-off",
          .value = i18n::tr(active ? "osd.privacy.mic-on" : "osd.privacy.mic-off"),
          .showProgress = false,
      };
    case PrivacyKind::Camera:
      return OsdContent{
          .kind = OsdKind::Privacy,
          .icon = active ? "camera" : "camera-off",
          .value = i18n::tr(active ? "osd.privacy.camera-on" : "osd.privacy.camera-off"),
          .showProgress = false,
      };
    case PrivacyKind::Screen:
      return OsdContent{
          .kind = OsdKind::Privacy,
          .icon = active ? "screen-share" : "screen-share-off",
          .value = i18n::tr(active ? "osd.privacy.screen-on" : "osd.privacy.screen-off"),
          .showProgress = false,
      };
    }

    return OsdContent{};
  }

} // namespace

PrivacyOsd::State PrivacyOsd::fromPipewireState(const PrivacyState& privacyState) {
  State out;
  for (const auto& capture : privacyState.captures) {
    switch (capture.kind) {
    case PrivacyCaptureKind::Microphone:
      out.mic = true;
      break;
    case PrivacyCaptureKind::Camera:
      out.camera = true;
      break;
    case PrivacyCaptureKind::Screen:
      out.screen = true;
      break;
    }
  }
  return out;
}

void PrivacyOsd::bindOverlay(OsdOverlay& overlay) { m_overlay = &overlay; }

void PrivacyOsd::onPrivacyStateChanged(const PipeWireService& service) {
  const State current = fromPipewireState(service.privacyState());

  if (m_overlay == nullptr) {
    m_lastState = current;
    return;
  }

  // Surface each capture independently: multiple kinds can change in one update
  // (e.g. mic + screen both stop on call-leave, or both already active at startup).
  if (m_lastState.mic != current.mic) {
    m_overlay->show(makePrivacyContent(PrivacyKind::Mic, current.mic));
  }
  if (m_lastState.camera != current.camera) {
    m_overlay->show(makePrivacyContent(PrivacyKind::Camera, current.camera));
  }
  if (m_lastState.screen != current.screen) {
    m_overlay->show(makePrivacyContent(PrivacyKind::Screen, current.screen));
  }

  m_lastState = current;
}
