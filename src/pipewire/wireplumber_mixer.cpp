#include "pipewire/wireplumber_mixer.h"

#include "core/log.h"

#include <glib.h>
#include <optional>
#include <unordered_map>
#include <wp/wp.h>

namespace {
  constexpr Logger kLog("wireplumber");

  // wp_mixer_api_volume_scale_enum: SCALE_LINEAR = 0, SCALE_CUBIC = 1. Cubic makes the
  // "volume" value match what pavucontrol/wpctl display, so we pass our perceptual value directly.
  constexpr int kScaleCubic = 1;
} // namespace

struct WirePlumberMixer::Impl {
  GMainContext* context = nullptr;
  GCancellable* cancellable = nullptr;
  WpCore* core = nullptr;
  WpPlugin* mixer = nullptr;
  bool ready = false;

  // Writes requested before the mixer-api finished activating (~1s at startup). Keyed by node id,
  // latest value wins; flushed once ready so early volume/mute changes are not lost.
  struct PendingWrite {
    std::optional<float> volume;
    std::optional<bool> mute;
  };
  std::unordered_map<std::uint32_t, PendingWrite> pendingBeforeReady;

  // GLib poll-loop bridge state (see reference_glib_mainloop_bridge pattern).
  mutable std::vector<GPollFD> glibPollFds;
  mutable gint glibMaxPriority = G_PRIORITY_DEFAULT;
  mutable int glibPollTimeoutMs = -1;

  Impl() {
    static gboolean s_wpInit = [] {
      wp_init(static_cast<WpInitFlags>(WP_INIT_PIPEWIRE | WP_INIT_SPA_TYPES));
      return TRUE;
    }();
    (void)s_wpInit;

    context = g_main_context_new();
    cancellable = g_cancellable_new();
    core = wp_core_new(context, nullptr, nullptr);

    if (wp_core_connect(core) == FALSE) {
      kLog.warn("could not connect to PipeWire; device volume control unavailable");
      return;
    }

    wp_core_load_component(
        core, "libwireplumber-module-mixer-api", "module", nullptr, nullptr, cancellable, &Impl::onComponentLoaded, this
    );
  }

  ~Impl() {
    if (cancellable != nullptr) {
      g_cancellable_cancel(cancellable);
      g_object_unref(cancellable);
    }
    if (mixer != nullptr) {
      g_object_unref(mixer);
    }
    if (core != nullptr) {
      wp_core_disconnect(core);
      g_object_unref(core);
    }
    if (context != nullptr) {
      g_main_context_unref(context);
    }
  }

  static void onComponentLoaded(GObject* /*source*/, GAsyncResult* res, gpointer data) noexcept {
    auto* self = static_cast<Impl*>(data);
    GError* err = nullptr;
    if (wp_core_load_component_finish(self->core, res, &err) == FALSE) {
      kLog.warn("mixer-api load failed: {}", err != nullptr ? err->message : "unknown");
      g_clear_error(&err);
      return;
    }

    self->mixer = wp_plugin_find(self->core, "mixer-api");
    if (self->mixer == nullptr) {
      kLog.warn("mixer-api plugin not found after load");
      return;
    }

    wp_object_activate(WP_OBJECT(self->mixer), WP_PLUGIN_FEATURE_ENABLED, nullptr, &Impl::onMixerActivated, self);
  }

  static void onMixerActivated(GObject* /*source*/, GAsyncResult* res, gpointer data) noexcept {
    auto* self = static_cast<Impl*>(data);
    GError* err = nullptr;
    if (wp_object_activate_finish(WP_OBJECT(self->mixer), res, &err) == FALSE) {
      kLog.warn("mixer-api activation failed: {}", err != nullptr ? err->message : "unknown");
      g_clear_error(&err);
      return;
    }

    g_object_set(self->mixer, "scale", kScaleCubic, nullptr);
    self->ready = true;
    kLog.info("mixer-api ready");

    for (const auto& [id, write] : self->pendingBeforeReady) {
      if (write.volume.has_value()) {
        self->applyVolume(id, *write.volume);
      }
      if (write.mute.has_value()) {
        self->applyMute(id, *write.mute);
      }
    }
    self->pendingBeforeReady.clear();
  }

  void requestVolume(std::uint32_t id, float volume) {
    if (ready) {
      applyVolume(id, volume);
    } else {
      pendingBeforeReady[id].volume = volume;
    }
  }

  void requestMute(std::uint32_t id, bool muted) {
    if (ready) {
      applyMute(id, muted);
    } else {
      pendingBeforeReady[id].mute = muted;
    }
  }

  void applyVolume(std::uint32_t id, float volume) { emit(id, "volume", g_variant_new_double(volume)); }
  void applyMute(std::uint32_t id, bool muted) { emit(id, "mute", g_variant_new_boolean(muted ? TRUE : FALSE)); }

  void emit(std::uint32_t id, const char* key, GVariant* value) {
    if (mixer == nullptr) {
      return;
    }
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&builder, "{sv}", key, value);
    GVariant* variant = g_variant_ref_sink(g_variant_builder_end(&builder));
    gboolean res = FALSE;
    g_signal_emit_by_name(mixer, "set-volume", static_cast<guint>(id), variant, &res);
    g_variant_unref(variant);

    // Wake the loop so the queued write flushes on the next dispatch. A synchronous drain here
    // chews through an ever-growing echo backlog under held media keys and lags the final value.
    g_main_context_wakeup(context);
  }

  void addPollFds(std::vector<pollfd>& fds) const {
    glibPollFds.clear();
    glibMaxPriority = G_PRIORITY_DEFAULT;
    glibPollTimeoutMs = -1;

    if (g_main_context_acquire(context) == FALSE) {
      return;
    }

    const gboolean ready_ = g_main_context_prepare(context, &glibMaxPriority);
    gint timeout = -1;
    const gint count = g_main_context_query(context, glibMaxPriority, &timeout, nullptr, 0);
    glibPollTimeoutMs = ready_ != FALSE ? 0 : timeout;
    if (count > 0) {
      glibPollFds.resize(static_cast<std::size_t>(count));
      g_main_context_query(context, glibMaxPriority, &timeout, glibPollFds.data(), count);
      glibPollTimeoutMs = ready_ != FALSE ? 0 : timeout;
      for (const GPollFD& glibFd : glibPollFds) {
        fds.push_back({.fd = glibFd.fd, .events = static_cast<short>(glibFd.events), .revents = 0});
      }
    }
    g_main_context_release(context);
  }

  void dispatch(const std::vector<pollfd>& fds, std::size_t startIdx) {
    if (g_main_context_acquire(context) == FALSE) {
      return;
    }
    for (std::size_t i = 0; i < glibPollFds.size(); ++i) {
      const std::size_t pollIndex = startIdx + i;
      glibPollFds[i].revents =
          pollIndex < fds.size() ? static_cast<gushort>(fds[pollIndex].revents) : static_cast<gushort>(0);
    }
    const gboolean ready_ =
        g_main_context_check(context, glibMaxPriority, glibPollFds.data(), static_cast<gint>(glibPollFds.size()));
    if (ready_ != FALSE) {
      g_main_context_dispatch(context);
    }
    g_main_context_release(context);
  }
};

WirePlumberMixer::WirePlumberMixer() : m_impl(std::make_unique<Impl>()) {}
WirePlumberMixer::~WirePlumberMixer() = default;

bool WirePlumberMixer::ready() const noexcept { return m_impl->ready; }

void WirePlumberMixer::setVolume(std::uint32_t id, float volume) { m_impl->requestVolume(id, volume); }

void WirePlumberMixer::setMuted(std::uint32_t id, bool muted) { m_impl->requestMute(id, muted); }

int WirePlumberMixer::pollTimeoutMs() const {
  // WpCore's async connect/load/activate makes GLib vote 0 ("dispatch now") in a burst, which
  // hot-spins the shared loop until the mixer is ready. Socket wakeups still come through the
  // polled fds, so flooring a 0 vote to a few ms paces the spin without slowing real progress.
  const int t = m_impl->glibPollTimeoutMs;
  return t == 0 ? 4 : t;
}

void WirePlumberMixer::doAddPollFds(std::vector<pollfd>& fds) { m_impl->addPollFds(fds); }

void WirePlumberMixer::dispatch(const std::vector<pollfd>& fds, std::size_t startIdx) {
  m_impl->dispatch(fds, startIdx);
}
