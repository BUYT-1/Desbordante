#include "algorithms/md/hymd/preprocessing/similarity_measure/monge_elkan_metric.h"

#include <algorithm>

#include "algorithms/md/hymd/preprocessing/similarity_measure/levenshtein_sim_measure.h"

double normalizedLevenshteinSimilarity(std::string const& s1, std::string const& s2) {
    int dist = LevenshteinDistance(s1, s2);
    int max_len = std::max(s1.size(), s2.size());
    return 1.0 - static_cast<double>(dist) / max_len;
}

double MongeElkan(std::vector<std::string> const& a, std::vector<std::string> const& b) {
    if (a.empty()) return 0.0;

    double sum = 0.0;
    for (auto const& s : a) {
        double max_sim = std::numeric_limits<double>::lowest();
        for (auto const& q : b) {
            double similarity = normalizedLevenshteinSimilarity(s, q);
            max_sim = std::max(max_sim, similarity);
        }
        sum += (max_sim == std::numeric_limits<double>::lowest() ? 0 : max_sim);
    }

    return sum / a.size();
}