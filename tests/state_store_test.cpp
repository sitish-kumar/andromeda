#include "config/state_store.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
  bool expect(bool condition, const char* message) {
    if (!condition) {
      std::fprintf(stderr, "state_store_test: %s\n", message);
    }
    return condition;
  }

  std::filesystem::path uniqueTestDir() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("noctalia-state-store-test-" + std::to_string(now));
  }

  std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string out;
    char buffer[256]{};
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
      out.append(buffer, static_cast<std::size_t>(in.gcount()));
    }
    return out;
  }

  bool boolStateRoundTrips() {
    const auto dir = uniqueTestDir();
    const auto path = dir / "state.toml";
    std::filesystem::remove_all(dir);

    StateStore store(path);
    store.load();

    bool ok = true;
    ok = expect(!store.boolValue("wallpaper_panel", "flatten").has_value(), "missing bool should be empty") && ok;
    ok = expect(store.setBool("wallpaper_panel", "flatten", true), "failed to write bool") && ok;

    const std::string content = readText(path);
    ok = expect(content.find("[wallpaper_panel]") != std::string::npos, "owner table was not written") && ok;
    ok = expect(content.find("flatten = true") != std::string::npos, "bool value was not written") && ok;

    StateStore loaded(path);
    loaded.load();
    ok = expect(loaded.boolValue("wallpaper_panel", "flatten").value_or(false), "bool did not reload") && ok;
    ok = expect(loaded.setBool("wallpaper_panel", "flatten", false), "failed to update bool") && ok;

    StateStore reloaded(path);
    reloaded.load();
    ok = expect(!reloaded.boolValue("wallpaper_panel", "flatten").value_or(true), "updated bool did not reload") && ok;
    ok = expect(!reloaded.setBool("wallpaper.panel", "flatten", true), "invalid owner was accepted") && ok;

    std::filesystem::remove_all(dir);
    return ok;
  }

  bool wrongTypeIsNotReadAsBool() {
    const auto dir = uniqueTestDir();
    const auto path = dir / "state.toml";
    std::filesystem::create_directories(dir);
    {
      std::ofstream out(path, std::ios::trunc);
      out << "[wallpaper_panel]\nflatten = \"yes\"\n";
    }

    StateStore store(path);
    store.load();

    bool ok = true;
    ok = expect(!store.boolValue("wallpaper_panel", "flatten").has_value(), "wrong type was read as bool") && ok;
    ok = expect(store.setBool("wallpaper_panel", "flatten", true), "failed to replace wrong type") && ok;
    ok =
        expect(store.boolValue("wallpaper_panel", "flatten").value_or(false), "replacement bool was not visible") && ok;

    std::filesystem::remove_all(dir);
    return ok;
  }
} // namespace

int main() {
  bool ok = true;
  ok = boolStateRoundTrips() && ok;
  ok = wrongTypeIsNotReadAsBool() && ok;
  return ok ? 0 : 1;
}
