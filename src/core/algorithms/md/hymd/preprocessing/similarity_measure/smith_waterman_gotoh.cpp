#include "algorithms/md/hymd/preprocessing/similarity_measure/smith_waterman_gotoh.h"

#include <algorithm>
#include <vector>

double substitutionCompare(char a, char b) {
    return (a == b) ? 1.0 : -2.0;
}

double smithWatermanGotoh(std::string const& s, std::string const& t, double gapValue = -0.5) {
    std::vector<double> v0(t.length() + 1, 0.0);
    std::vector<double> v1(t.length() + 1, 0.0);

    double max = 0.0;

    for (size_t i = 1; i <= s.length(); ++i) {
        for (size_t j = 1; j <= t.length(); ++j) {
            double match = v0[j - 1] + substitutionCompare(s[i - 1], t[j - 1]);
            double deleteFromS = v0[j] + gapValue;
            double deleteFromT = v1[j - 1] + gapValue;
            v1[j] = std::max({0.0, match, deleteFromS, deleteFromT});
            max = std::max(max, v1[j]);
        }
        std::swap(v0, v1);
    }

    return max;
}

double normalizedSmithWatermanGotoh(std::string const& s, std::string const& t,
                                    double gapValue = -0.5) {
    if (s.empty() && t.empty()) {
        return 1.0;
    }

    if (s.empty() || t.empty()) {
        return 0.0;
    }

    double maxDistance = std::min(s.length(), t.length()) * std::max(1.0, gapValue);
    return smithWatermanGotoh(s, t, gapValue) / maxDistance;
}