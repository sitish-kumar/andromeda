#include "config/schema/config_schema.h"

#include "config/schema/engine.h"

namespace noctalia::config::schema {

  const Schema<AudioConfig>& audioSchema() {
    static const Schema<AudioConfig> s = {
        field(&AudioConfig::enableOverdrive, "enable_overdrive"),
        field(&AudioConfig::enableSounds, "enable_sounds"),
        field(&AudioConfig::soundVolume, "sound_volume", Range<float>{0.0f, 1.0f}),
        field(&AudioConfig::volumeChangeSound, "volume_change_sound"),
        field(&AudioConfig::notificationSound, "notification_sound"),
    };
    return s;
  }

  const Schema<WeatherConfig>& weatherSchema() {
    static const Schema<WeatherConfig> s = {
        field(&WeatherConfig::enabled, "enabled"),
        field(&WeatherConfig::effects, "effects"),
        field(&WeatherConfig::refreshMinutes, "refresh_minutes"),
        field(&WeatherConfig::unit, "unit"),
    };
    return s;
  }

  const Schema<OsdConfig>& osdSchema() {
    static const Schema<OsdConfig> s = {
        field(&OsdConfig::position, "position"),
        field(&OsdConfig::orientation, "orientation"),
        field(&OsdConfig::scale, "scale", Range<float>{0.5f, 2.5f}),
        field(&OsdConfig::backgroundOpacity, "background_opacity", Range<float>{0.0f, 1.0f}),
        field(&OsdConfig::offsetX, "offset_x", Range<std::int64_t>{0, std::nullopt}),
        field(&OsdConfig::offsetY, "offset_y", Range<std::int64_t>{0, std::nullopt}),
        field(&OsdConfig::monitors, "monitors"),
        field(&OsdConfig::lockKeys, "lock_keys"),
        field(&OsdConfig::keyboardLayout, "keyboard_layout"),
    };
    return s;
  }

  const Schema<BackdropConfig>& backdropSchema() {
    static const Schema<BackdropConfig> s = {
        field(&BackdropConfig::enabled, "enabled"),
        field(&BackdropConfig::blurIntensity, "blur_intensity", Range<float>{0.0f, 1.0f}),
        field(&BackdropConfig::tintIntensity, "tint_intensity", Range<float>{0.0f, 1.0f}),
    };
    return s;
  }

  const Schema<LockscreenConfig>& lockscreenSchema() {
    static const Schema<LockscreenConfig> s = {
        field(&LockscreenConfig::blurredDesktop, "blurred_desktop"),
        field(&LockscreenConfig::blurIntensity, "blur_intensity", Range<float>{0.0f, 1.0f}),
        field(&LockscreenConfig::tintIntensity, "tint_intensity", Range<float>{0.0f, 1.0f}),
        field(&LockscreenConfig::wallpaperBlurIntensity, "wallpaper_blur_intensity", Range<float>{0.0f, 1.0f}),
        field(&LockscreenConfig::wallpaperTintIntensity, "wallpaper_tint_intensity", Range<float>{0.0f, 1.0f}),
    };
    return s;
  }

  namespace {
    // Poll-second floats are stored verbatim here; the [1,120]/disabled clamping
    // happens at consumption, not at parse time — so no Range is attached.
    const Schema<SystemConfig::MonitorConfig>& systemMonitorSchema() {
      static const Schema<SystemConfig::MonitorConfig> s = {
          field(&SystemConfig::MonitorConfig::enabled, "enabled"),
          field(&SystemConfig::MonitorConfig::cpuPollSeconds, "cpu_poll_seconds"),
          field(&SystemConfig::MonitorConfig::gpuPollSeconds, "gpu_poll_seconds"),
          field(&SystemConfig::MonitorConfig::memoryPollSeconds, "memory_poll_seconds"),
          field(&SystemConfig::MonitorConfig::networkPollSeconds, "network_poll_seconds"),
          field(&SystemConfig::MonitorConfig::diskPollSeconds, "disk_poll_seconds"),
      };
      return s;
    }
  } // namespace

  const Schema<SystemConfig>& systemSchema() {
    static const Schema<SystemConfig> s = {
        subTable(&SystemConfig::monitor, "monitor", systemMonitorSchema()),
    };
    return s;
  }

} // namespace noctalia::config::schema
