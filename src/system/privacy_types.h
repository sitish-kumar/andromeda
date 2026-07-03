#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class PrivacyCaptureKind { Microphone, Camera, Screen };

struct PrivacyCapture {
  PrivacyCaptureKind kind;
  std::uint32_t id;
  std::string appName;

  bool operator==(const PrivacyCapture&) const = default;
};

struct PrivacyState {
  std::vector<PrivacyCapture> captures;

  bool operator==(const PrivacyState&) const = default;
};
