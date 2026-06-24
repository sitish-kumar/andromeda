#pragma once

#include <cstddef>
#include <string>

namespace FdDiagnostics {

  [[nodiscard]] std::string describeOpenFileDescriptors(std::size_t maxTargets = 8);

} // namespace FdDiagnostics
