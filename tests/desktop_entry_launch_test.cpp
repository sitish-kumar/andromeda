#include "system/desktop_entry_launch.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

  bool expect(bool condition, const char* message) {
    if (!condition) {
      std::fprintf(stderr, "desktop_entry_launch_test: %s\n", message);
    }
    return condition;
  }

  bool expectArgs(
      const std::optional<desktop_entry_launch::PreparedCommand>& command, const std::vector<std::string>& expected,
      const char* message
  ) {
    if (!expect(command.has_value(), message)) {
      return false;
    }
    if (command->args == expected) {
      return true;
    }

    std::fprintf(stderr, "desktop_entry_launch_test: %s\n", message);
    std::fprintf(stderr, "  expected:");
    for (const auto& arg : expected) {
      std::fprintf(stderr, " [%s]", arg.c_str());
    }
    std::fprintf(stderr, "\n  actual:");
    for (const auto& arg : command->args) {
      std::fprintf(stderr, " [%s]", arg.c_str());
    }
    std::fprintf(stderr, "\n");
    return false;
  }

} // namespace

int main() {
  bool ok = true;

  ok = expectArgs(
           desktop_entry_launch::prepareCommand("sample --name %% --file %f --url %U --keep", false),
           {"sample", "--name", "%", "--file", "--url", "--keep"}, "field codes should be removed"
       )
      && ok;

  ok = expectArgs(
           desktop_entry_launch::prepareCommand("sample --title \"Hello World\" --single 'Two Words'", false),
           {"sample", "--title", "Hello World", "--single", "Two Words"}, "quoted arguments should stay together"
       )
      && ok;

  desktop_entry_launch::PrepareOptions terminalOptions;
  terminalOptions.terminalCandidates = {"test-terminal"};
  ok = expectArgs(
           desktop_entry_launch::prepareCommand("sample --flag", true, terminalOptions),
           {"test-terminal", "-e", "sh", "-lc", "sample --flag"}, "terminal candidates should wrap the command"
       )
      && ok;

  desktop_entry_launch::PrepareOptions noTerminalDiscovery;
  noTerminalDiscovery.useSystemTerminalDiscovery = false;
  ok = expect(
           !desktop_entry_launch::prepareCommand("sample --flag", true, noTerminalDiscovery).has_value(),
           "terminal preparation should fail when discovery is disabled and no candidates are provided"
       )
      && ok;

  ok = expect(
           !desktop_entry_launch::prepareCommand(" %f %U ", false).has_value(),
           "field-code-only command should not prepare an empty argv"
       )
      && ok;

  return ok ? 0 : 1;
}
