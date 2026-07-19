#include "tools/pywalfox/pywalfox_host.h"
#include "tools/pywalfox/settings.h"

#include <print>
#include <string_view>

namespace {

  void printHelp() {
    std::println(
        "noctalia-pywalfox — Pywalfox-compatible native messaging host\n"
        "\n"
        "Usage: noctalia-pywalfox [ACTION]\n"
        "\n"
        "Actions:\n"
        "  (none)/start   Run as Firefox native messaging host (default)\n"
        "  install        Install user-local native messaging manifest\n"
        "  uninstall      Remove user-local native messaging manifest\n"
        "  update         Ask a running host to push colors to the extension\n"
        "  dark|light|auto  Persist and push theme mode\n"
        "  --version      Print host version\n"
        "  -h, --help     Show this help\n"
    );
  }

  [[nodiscard]] bool isHelp(std::string_view arg) { return arg == "-h" || arg == "--help" || arg == "help"; }

} // namespace

int main(int argc, char* argv[]) {
  // Firefox may pass extra args (extension id). Any unrecognized argv starts the daemon.
  std::string_view action;
  if (argc >= 2) {
    action = argv[1];
  }

  if (action.empty() || action == "start" || action == pywalfox_host::kExtensionId) {
    return pywalfox_host::runDaemon();
  }
  if (isHelp(action)) {
    printHelp();
    return 0;
  }
  if (action == "--version" || action == "-v" || action == "version") {
    std::println("{}", pywalfox_host::kDaemonVersion);
    return 0;
  }
  if (action == "install") {
    const auto host = pywalfox_host::resolveHostExecutable();
    std::string err;
    if (!pywalfox_host::installManifest(host, &err)) {
      std::println(stderr, "install failed: {}", err);
      return 1;
    }
    std::println("Installed native messaging host manifest:");
    std::println("  {}", pywalfox_host::userManifestPath().string());
    std::println("  path = {}", host.string());
    std::println("Restart Firefox if it was already running.");
    return 0;
  }
  if (action == "uninstall") {
    std::string err;
    if (!pywalfox_host::uninstallManifest(&err)) {
      std::println(stderr, "uninstall failed: {}", err);
      return 1;
    }
    std::println("Removed {}", pywalfox_host::userManifestPath().string());
    return 0;
  }
  if (action == "update") {
    return pywalfox_host::sendSocketCommand("action:update");
  }
  if (action == "dark") {
    pywalfox_host::settings::set("theme_mode", "dark");
    return pywalfox_host::sendSocketCommand("theme:mode:dark");
  }
  if (action == "light") {
    pywalfox_host::settings::set("theme_mode", "light");
    return pywalfox_host::sendSocketCommand("theme:mode:light");
  }
  if (action == "auto") {
    pywalfox_host::settings::set("theme_mode", "auto");
    return pywalfox_host::sendSocketCommand("theme:mode:auto");
  }

  // Firefox native messaging often launches: host <chrome-path> <extension-id>
  // Treat any unrecognized argv as a host start request.
  return pywalfox_host::runDaemon();
}
