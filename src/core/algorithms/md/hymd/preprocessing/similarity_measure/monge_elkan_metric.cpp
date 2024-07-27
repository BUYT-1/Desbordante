#include "algorithms/md/hymd/preprocessing/similarity_measure/monge_elkan_metric.h"

#include <algorithm>
#include <functional>
#include <limits>

double MongeElkan(
        std::vector<std::string> const& a, std::vector<std::string> const& b,
        std::function<double(std::string const&, std::string const&)> similarityFunction) {
    if (a.empty() && b.empty()) return 1.0;
    double sum = 0.0;
    for (auto const& s : a) {
        double max_sim = std::numeric_limits<double>::lowest();
        for (auto const& q : b) {
            double similarity = similarityFunction(s, q);
            max_sim = std::max(max_sim, similarity);
        }
        sum += (max_sim == std::numeric_limits<double>::lowest() ? 0 : max_sim);
    }

    return a.empty() ? 0 : sum / a.size();
}