#include <functional>
#include <string>
#include <vector>

#include "algorithms/md/hymd/preprocessing/similarity_measure/smith_waterman_gotoh.h"

double MongeElkan(
        std::vector<std::string> const &a, std::vector<std::string> const &b,
        std::function<double(std::string const &, std::string const &)> similarityFunction =
                [](std::string const &s1, std::string const &s2) {
                    return NormalizedSmithWatermanGotoh(s1, s2);
                });