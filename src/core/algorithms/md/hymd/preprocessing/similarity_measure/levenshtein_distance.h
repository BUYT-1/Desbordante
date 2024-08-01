#pragma once

#include <string_view>

namespace algos::hymd::preprocessing {
unsigned LevenshteinDistance(std::string_view l, std::string_view r) noexcept;
}  // namespace algos::hymd::preprocessing
