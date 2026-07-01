#pragma once

#include "app/poll_source.h"

#include <cstdint>
#include <memory>
#include <poll.h>
#include <vector>

// Sets volume/mute for device sinks/sources through WirePlumber's mixer-api plugin
// (the same path `wpctl` uses internally), so pipewire-pulse / pavucontrol stay in sync
// without forking an external process or racing writes. Owns a private WpCore on a
// dedicated GMainContext, bridged into the poll loop as a PollSource.
class WirePlumberMixer final : public PollSource {
public:
  WirePlumberMixer();
  ~WirePlumberMixer() override;

  WirePlumberMixer(const WirePlumberMixer&) = delete;
  WirePlumberMixer& operator=(const WirePlumberMixer&) = delete;

  // True once connected and the mixer-api plugin is active. Writes made before this are queued
  // and flushed on activation, so callers need not gate on it.
  [[nodiscard]] bool ready() const noexcept;

  // Perceptual volume in [0, 1.5] (cubic scale), applied to a node by global id.
  void setVolume(std::uint32_t id, float volume);
  void setMuted(std::uint32_t id, bool muted);

  [[nodiscard]] int pollTimeoutMs() const override;
  void dispatch(const std::vector<pollfd>& fds, std::size_t startIdx) override;

protected:
  void doAddPollFds(std::vector<pollfd>& fds) override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
