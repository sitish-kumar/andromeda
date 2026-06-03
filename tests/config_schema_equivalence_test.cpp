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

  checkReadInverse("audio", legacyRoot, probe.audio, audioSchema());
  checkReadInverse("weather", legacyRoot, probe.weather, weatherSchema());
  checkReadInverse("osd", legacyRoot, probe.osd, osdSchema());
  checkReadInverse("backdrop", legacyRoot, probe.backdrop, backdropSchema());
  checkReadInverse("lockscreen", legacyRoot, probe.lockscreen, lockscreenSchema());
  checkReadInverse("system", legacyRoot, probe.system, systemSchema());

  checkClamps();

  if (g_failures == 0) {
    std::puts("config_schema_equivalence: all checks passed");
    return 0;
  }
  std::fprintf(stderr, "config_schema_equivalence: %d failure(s)\n", g_failures);
  return 1;
}
