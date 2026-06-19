#pragma once

class OsdOverlay;
class PipeWireService;
struct PrivacyState;

class PrivacyOsd {
public:
  void bindOverlay(OsdOverlay& overlay);
  void primeFromService(const PipeWireService& service);
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
  State m_lastState;
};
