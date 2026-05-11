#pragma once

#include "util/string_utils.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace tray {

  inline std::vector<std::string> identifierVariants(std::string_view value) {
    std::vector<std::string> out;
    if (value.empty()) {
      return out;
    }

    auto pushUnique = [&out](std::string candidate) {
      if (candidate.empty()) {
        return;
      }
      if (std::ranges::find(out, candidate) == out.end()) {
        out.push_back(std::move(candidate));
      }
    };

    std::string base(value);
    pushUnique(base);
    pushUnique(StringUtils::toLower(base));

    if (const auto slash = base.find_last_of('/'); slash != std::string::npos && slash + 1 < base.size()) {
      base = base.substr(slash + 1);
      pushUnique(base);
      pushUnique(StringUtils::toLower(base));
    }

    std::string dashed = base;
    std::replace(dashed.begin(), dashed.end(), '_', '-');
    pushUnique(dashed);
    pushUnique(StringUtils::toLower(dashed));

    std::string underscored = base;
    std::replace(underscored.begin(), underscored.end(), '-', '_');
    pushUnique(underscored);
    pushUnique(StringUtils::toLower(underscored));

    auto pushReducedForms = [&pushUnique](std::string candidate) {
      if (candidate.empty()) {
        return;
      }

      pushUnique(candidate);
      pushUnique(StringUtils::toLower(candidate));

      bool changed = true;
      while (changed && !candidate.empty()) {
        changed = false;

        for (const auto& suffix : {"_client", "-client", ".desktop", "_indicator", "-indicator", "_tray", "-tray",
                                   "_status", "-status", "_panel", "-panel"}) {
          if (candidate.size() > std::char_traits<char>::length(suffix) && candidate.ends_with(suffix)) {
            candidate = candidate.substr(0, candidate.size() - std::char_traits<char>::length(suffix));
            pushUnique(candidate);
            pushUnique(StringUtils::toLower(candidate));
            changed = true;
            break;
          }
        }

        if (changed || candidate.empty()) {
          continue;
        }

        const auto separator = candidate.find_last_of("-_");
        if (separator != std::string::npos && separator + 1 < candidate.size()) {
          const std::string tail = candidate.substr(separator + 1);
          const bool numericTail = std::ranges::all_of(tail, [](unsigned char c) { return std::isdigit(c) != 0; });
          if (numericTail) {
            candidate = candidate.substr(0, separator);
            pushUnique(candidate);
            pushUnique(StringUtils::toLower(candidate));
            changed = true;
            continue;
          }
        }

        for (const auto& suffix : {"-linux", "_linux"}) {
          if (candidate.size() > std::char_traits<char>::length(suffix) && candidate.ends_with(suffix)) {
            candidate = candidate.substr(0, candidate.size() - std::char_traits<char>::length(suffix));
            pushUnique(candidate);
            pushUnique(StringUtils::toLower(candidate));
            changed = true;
            break;
          }
        }
      }
    };

    for (const auto& candidate : std::vector<std::string>{base, dashed, underscored}) {
      pushReducedForms(candidate);
    }

    return out;
  }

} // namespace tray
