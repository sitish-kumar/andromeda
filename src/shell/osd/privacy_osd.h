#pragma once

class OsdOverlay;
class PipeWireService;
struct PrivacyState;

class PrivacyOsd {
public:
  void bindOverlay(OsdOverlay& overlay);
  void onPrivacyStateChanged(const PipeWireService& service);

private:
  struct State {
    bool mic = false;
    bool camera = false;
    bool screen = false;

    bool operator==(const State&) const = default;
  };

  [[nodiscard]] static State fromPipewireState(const PrivacyState& privacyState);

  OsdOverlay* m_overlay = nullptr;
  // Baseline starts empty by contract: the first PipeWire enumeration announces
  // any capture already active at launch as an on-transition. Do not prime from
  // live state or these startup notifications are lost.
  State m_lastState;
};
