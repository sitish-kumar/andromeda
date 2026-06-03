// Golden equivalence harness for the declarative config schema (Phase 1).
//
// For every migrated section it asserts two things against the legacy code:
//   1. write parity   — writeTable(section, schema) serializes byte-identically
//                        to the section emitted by config_export::configToToml.
//   2. read inverse    — readInto(configToToml(c)[section]) reconstructs the
//                        original section value (schema read undoes schema write,
//                        which equals the legacy serialization).
// Plus targeted clamp goldens that pin parse-time range behavior.
//
// This is the safety net: a section may only have its hand-written
// parseTableInto/configToToml branch deleted once it is covered and green here.

#include "config/config_export.h"
#include "config/config_types.h"
#include "config/schema/config_schema.h"
#include "config/schema/engine.h"
#include "core/key_chord.h"
#include "core/toml.h"

#include <cstdio>
#include <sstream>
#include <string>

using namespace noctalia::config::schema;

namespace {

  int g_failures = 0;

  void fail(const std::string& message) {
    std::fprintf(stderr, "config_schema_equivalence: FAIL: %s\n", message.c_str());
    ++g_failures;
  }

  // Mirror of ConfigService::formatToml so serialized output matches exactly.
  std::string formatToml(const toml::table& table) {
    std::ostringstream out;
    out << toml::toml_formatter{table, toml::toml_formatter::default_flags & ~toml::format_flags::allow_literal_strings};
    return out.str();
  }

  template <typename T>
  void checkWriteParity(const std::string& section, const toml::table& legacyRoot, const T& value,
                        const Schema<T>& schema) {
    const auto* legacySection = legacyRoot[section].as_table();
    if (legacySection == nullptr) {
      fail(section + ": configToToml emitted no [" + section + "] table");
      return;
    }
    const std::string legacy = formatToml(*legacySection);
    const std::string fresh = formatToml(writeTable(value, schema));
    if (legacy != fresh) {
      fail(section + ": write mismatch\n--- legacy ---\n" + legacy + "\n--- schema ---\n" + fresh);
    }
  }

  template <typename T>
  void checkReadInverse(const std::string& section, const toml::table& legacyRoot, const T& expected,
                        const Schema<T>& schema) {
    const auto* legacySection = legacyRoot[section].as_table();
    if (legacySection == nullptr) {
      fail(section + ": configToToml emitted no [" + section + "] table");
      return;
    }
    T roundtrip{};
    Diagnostics diag;
    readInto(*legacySection, roundtrip, schema, section, diag);
    if (!(roundtrip == expected)) {
      fail(section + ": read inverse did not reconstruct the original value");
    }
  }

  // Build a config whose migrated sections hold non-default values, so parity
  // checks exercise real serialization rather than all-defaults.
  Config makeProbe() {
    Config c;
    c.audio = AudioConfig{true, true, 0.73f, "change.ogg", "notify.ogg"};
    c.weather = WeatherConfig{false, false, 17, "imperial"};
    c.osd.position = "bottom_left";
    c.osd.orientation = "vertical";
    c.osd.scale = 1.4f;
    c.osd.backgroundOpacity = 0.42f;
    c.osd.offsetX = 33;
    c.osd.offsetY = 11;
    c.osd.monitors = {"DP-1", "HDMI-A-1"};
    c.osd.lockKeys = false;
    c.osd.keyboardLayout = false;
    c.backdrop = BackdropConfig{true, 0.8f, 0.2f};
    c.lockscreen = LockscreenConfig{true, 0.6f, 0.25f, 0.4f, 0.15f};
    c.system.monitor.enabled = false;
    c.system.monitor.cpuPollSeconds = 5.0f;
    c.system.monitor.gpuPollSeconds = 4.0f;
    c.system.monitor.memoryPollSeconds = 6.0f;
    c.system.monitor.networkPollSeconds = 7.0f;
    c.system.monitor.diskPollSeconds = 12.0f;
    c.nightlight = NightLightConfig{true, true, 6000, 3500}; // gap satisfied
    c.location.autoLocate = true;
    c.location.address = "Berlin";
    c.location.sunset = "20:30";
    c.location.sunrise = "06:15";
    c.location.latitude = 52.52;
    c.location.longitude = 13.405;
    c.notification = NotificationConfig{
        false, false, "bottom_left", "overlay", 1.3f, 0.5f, 12, 6, {"DP-2"}, false, {"discord"}, true, {"normal", "critical"},
    };
    c.dock.enabled = true;
    c.dock.position = "left";
    c.dock.iconSize = 40;
    c.dock.radius = 20;
    c.dock.radiusTopLeft = 10;
    c.dock.radiusTopRight = 12;
    c.dock.radiusBottomLeft = 14;
    c.dock.radiusBottomRight = 16;
    c.dock.launcherPosition = "start";
    c.dock.pinned = {"firefox.desktop"};
    c.dock.monitors = {"DP-1"};
    c.brightness.enableDdcutil = true;
    c.brightness.ddcutilIgnoreMmids = {"ABC123"};
    c.brightness.monitorOverrides = {
        {"DP-1", BrightnessBackendPreference::Ddcutil},
        {"eDP-1", std::nullopt},
    };
    c.battery.warningThreshold = 15;
    c.battery.deviceThresholds = {{"BAT0", 10}, {"hidpp:1", 25}};
    c.controlCenter.sidebarMode = ControlCenterSidebarMode::Full;
    c.controlCenter.sidebarSectionMode = ControlCenterSidebarMode::None;
    c.controlCenter.shortcuts = {{"wifi"}, {"bluetooth"}};
    c.calendar.enabled = true;
    c.calendar.refreshMinutes = 30;
    c.calendar.accounts = {
        {"acc1", "google", "Work", "#ff0000", "", "me@example.com"},
        {"acc2", "caldav", "Home", "", "https://dav.example.com/cal", "user"},
    };
    // Explicit chords so write→read round-trips (empty would emit defaults instead).
    c.keybinds.validate = {*parseKeyChordSpec("Return")};
    c.keybinds.cancel = {*parseKeyChordSpec("Escape")};
    c.keybinds.left = {*parseKeyChordSpec("Left")};
    c.keybinds.right = {*parseKeyChordSpec("Right")};
    c.keybinds.up = {*parseKeyChordSpec("Up")};
    c.keybinds.down = {*parseKeyChordSpec("Down")};
    c.hooks.commands[0] = {"notify-send hi"};
    c.hooks.commands[2] = {"cmd-a", "cmd-b"};
    c.idle.preActionFadeSeconds = 3.0f;
    // Explicit normalized actions so normalizeIdleBehaviorAction is a no-op on read.
    c.idle.behaviors = {
        {"dim", true, 60, "lock", "", "", true},
        {"off", false, 300, "screen_off", "", "", true},
    };
    c.wallpaper.enabled = false;
    c.wallpaper.fillColor = colorSpecFromConfigString("#ff8800");
    c.wallpaper.transitions = {WallpaperTransition::Wipe, WallpaperTransition::Zoom};
    c.wallpaper.transitionDurationMs = 2000.0f;
    c.wallpaper.edgeSmoothness = 0.5f;
    c.wallpaper.directory = "/srv/wallpapers"; // absolute: expandUserPath leaves it unchanged
    c.wallpaper.automation.enabled = true;
    c.wallpaper.automation.intervalMinutes = 30;
    c.wallpaper.automation.order = WallpaperAutomationConfig::Order::Alphabetical;
    c.wallpaper.monitorOverrides = {
        {"DP-1", true, colorSpecFromConfigString("#00ff00"), std::string("/srv/wp1"), std::nullopt, std::nullopt},
    };
    c.shell.uiScale = 1.25f;
    c.shell.fontFamily = "Inter";
    c.shell.lang = "en_US";
    c.shell.timeFormat = "{:%H:%M:%S}";
    c.shell.passwordMaskStyle = PasswordMaskStyle::RandomIcons;
    c.shell.clipboardHistoryMaxEntries = 80;
    c.shell.clipboardAutoPaste = ClipboardAutoPasteMode::CtrlV;
    c.shell.avatarPath = "/home/u/face.png";
    c.shell.animation.speed = 1.5f;
    c.shell.shadow.direction = ShadowDirection::UpLeft;
    c.shell.panel.transparencyMode = PanelTransparencyMode::Glass;
    c.shell.panel.launcherPlacement = PanelPlacement::Floating;
    c.shell.panel.launcherCompact = true;
    c.shell.screenCorners.enabled = true;
    c.shell.screenCorners.size = 24;
    c.shell.mpris.blacklist = {"firefox"};
    c.shell.screenshot.directory = "/shots";
    c.shell.screenshot.pipeToCommand = true;
    c.shell.session.actions = {
        SessionPanelActionConfig{
            "lock", true, std::nullopt, std::string("Lock"), std::string("lock"),
            SessionActionButtonVariant::Primary, parseKeyChordSpec("Ctrl+l"),
        },
        SessionPanelActionConfig{"shutdown", false, std::nullopt, std::nullopt, std::nullopt,
                                 SessionActionButtonVariant::Destructive, std::nullopt},
    };
    c.theme.source = PaletteSource::Wallpaper;
    c.theme.builtinPalette = "Tokyo";
    c.theme.mode = ThemeMode::Light;
    c.theme.templates.enableBuiltinTemplates = false;
    c.theme.templates.builtinIds = {"a", "b"};
    c.theme.templates.customColors = {{"accent", "#112233", true}, {"bg", "#000000", false}};
    c.theme.templates.userTemplates = {
        ThemeConfig::UserTemplateConfig{
            "tmpl1", true, "/in.png", ThemeConfig::TemplateInputPathModesConfig{"/d.png", "/l.png"},
            {"/out1", "/out2"}, "/dyn", "compareX", {{"c1", "#aabbcc"}}, "pre", "post", 3,
        },
    };
    return c;
  }

  void checkClamps() {
    // sound_volume above the max clamps to 1.0.
    {
      auto t = toml::parse("sound_volume = 2.5");
      AudioConfig a{};
      Diagnostics d;
      readInto(t, a, audioSchema(), "audio", d);
      if (a.soundVolume != 1.0f) {
        fail("audio.sound_volume clamp: expected 1.0");
      }
    }
    // osd.offset_x has a min-only floor at 0.
    {
      auto t = toml::parse("offset_x = -5");
      OsdConfig o{};
      Diagnostics d;
      readInto(t, o, osdSchema(), "osd", d);
      if (o.offsetX != 0) {
        fail("osd.offset_x floor: expected 0");
      }
    }
    // Unknown enum-like string is left untouched on a plain string field (no enum here),
    // so just verify osd.scale below the min clamps up.
    {
      auto t = toml::parse("scale = 0.1");
      OsdConfig o{};
      Diagnostics d;
      readInto(t, o, osdSchema(), "osd", d);
      if (o.scale != 0.5f) {
        fail("osd.scale clamp: expected 0.5");
      }
    }
  }

} // namespace

int main() {
  const Config probe = makeProbe();
  const toml::table legacyRoot = config_export::configToToml(probe);

  checkWriteParity("audio", legacyRoot, probe.audio, audioSchema());
  checkWriteParity("weather", legacyRoot, probe.weather, weatherSchema());
  checkWriteParity("osd", legacyRoot, probe.osd, osdSchema());
  checkWriteParity("backdrop", legacyRoot, probe.backdrop, backdropSchema());
  checkWriteParity("lockscreen", legacyRoot, probe.lockscreen, lockscreenSchema());
  checkWriteParity("system", legacyRoot, probe.system, systemSchema());
  checkWriteParity("nightlight", legacyRoot, probe.nightlight, nightlightSchema());
  checkWriteParity("location", legacyRoot, probe.location, locationSchema());
  checkWriteParity("notification", legacyRoot, probe.notification, notificationSchema());
  checkWriteParity("dock", legacyRoot, probe.dock, dockSchema());
  checkWriteParity("brightness", legacyRoot, probe.brightness, brightnessSchema());
  checkWriteParity("battery", legacyRoot, probe.battery, batterySchema());
  checkWriteParity("control_center", legacyRoot, probe.controlCenter, controlCenterSchema());
  checkWriteParity("calendar", legacyRoot, probe.calendar, calendarSchema());
  checkWriteParity("keybinds", legacyRoot, probe.keybinds, keybindsSchema());
  checkWriteParity("hooks", legacyRoot, probe.hooks, hooksSchema());
  checkWriteParity("idle", legacyRoot, probe.idle, idleSchema());
  checkWriteParity("wallpaper", legacyRoot, probe.wallpaper, wallpaperSchema());
  checkWriteParity("theme", legacyRoot, probe.theme, themeSchema());
  checkWriteParity("shell", legacyRoot, probe.shell, shellSchema());

  checkReadInverse("audio", legacyRoot, probe.audio, audioSchema());
  checkReadInverse("weather", legacyRoot, probe.weather, weatherSchema());
  checkReadInverse("osd", legacyRoot, probe.osd, osdSchema());
  checkReadInverse("backdrop", legacyRoot, probe.backdrop, backdropSchema());
  checkReadInverse("lockscreen", legacyRoot, probe.lockscreen, lockscreenSchema());
  checkReadInverse("system", legacyRoot, probe.system, systemSchema());
  checkReadInverse("nightlight", legacyRoot, probe.nightlight, nightlightSchema());
  checkReadInverse("location", legacyRoot, probe.location, locationSchema());
  checkReadInverse("notification", legacyRoot, probe.notification, notificationSchema());
  checkReadInverse("dock", legacyRoot, probe.dock, dockSchema());
  checkReadInverse("brightness", legacyRoot, probe.brightness, brightnessSchema());
  checkReadInverse("battery", legacyRoot, probe.battery, batterySchema());
  checkReadInverse("control_center", legacyRoot, probe.controlCenter, controlCenterSchema());
  checkReadInverse("calendar", legacyRoot, probe.calendar, calendarSchema());
  checkReadInverse("keybinds", legacyRoot, probe.keybinds, keybindsSchema());
  checkReadInverse("hooks", legacyRoot, probe.hooks, hooksSchema());
  checkReadInverse("idle", legacyRoot, probe.idle, idleSchema());
  checkReadInverse("wallpaper", legacyRoot, probe.wallpaper, wallpaperSchema());
  checkReadInverse("theme", legacyRoot, probe.theme, themeSchema());
  checkReadInverse("shell", legacyRoot, probe.shell, shellSchema());

  checkClamps();

  if (g_failures == 0) {
    std::puts("config_schema_equivalence: all checks passed");
    return 0;
  }
  std::fprintf(stderr, "config_schema_equivalence: %d failure(s)\n", g_failures);
  return 1;
}
