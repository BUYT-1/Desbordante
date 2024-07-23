#include "algorithms/md/hymd/preprocessing/similarity_measure/distance_similarity_measure.h"

#include <atomic>
#include <cstddef>
#include <span>
#include <unordered_set>
#include <vector>

#include "algorithms/md/hymd/preprocessing/build_indexes.h"
#include "algorithms/md/hymd/preprocessing/ccv_id_pickers/index_uniform.h"
#include "algorithms/md/hymd/preprocessing/encode_results.h"

namespace algos::hymd::preprocessing::similarity_measure {

struct ValueProcessingWorker {
    std::shared_ptr<DataInfo const> const& data_info_left;
    std::shared_ptr<DataInfo const> const& data_info_right;
    std::vector<indexes::PliCluster> const& clusters_right;
    Similarity const min_sim;
    ValidTableResults<Similarity>& task_data;
    std::function<double(std::byte const*, std::byte const*)> compute_distance;
    std::size_t const data_left_size = data_info_left->GetElementNumber();
    std::size_t const data_right_size = data_info_right->GetElementNumber();
    std::atomic<bool> dissimilar_found = false;

    void AddValue(RowInfo<Similarity>& row_info, ValueIdentifier value_id, Similarity sim) {
        auto& [sim_value_id_vec, valid_records_number] = row_info;
        sim_value_id_vec.emplace_back(sim, value_id);
        valid_records_number += clusters_right[value_id].size();
    }

    void Start() {
        bool found_dissimilar = false;
        for (ValueIdentifier left_value_id = 0; left_value_id < data_left_size; ++left_value_id) {
            bool found_dissimilar_here = ProcessDistance(left_value_id);
            if (found_dissimilar_here) {
                found_dissimilar = true;
            }
        }
        if (found_dissimilar) {
            dissimilar_found.store(true, std::memory_order::release);
        }
    }

    bool ProcessDistance(ValueIdentifier left_value_id) {
        auto& row_info = task_data[left_value_id];
        row_info.second = 0;

        double max_distance = 0;
        std::vector<double> distances(data_right_size);
        for (ValueIdentifier right_index = 0; right_index < data_right_size; ++right_index) {
            double distance = compute_distance(data_info_left->GetAt(left_value_id),
                                               data_info_right->GetAt(right_index));
            distances[right_index] = distance;
            if (distance > max_distance) {
                max_distance = distance;
            }
        }

        bool found_dissimilar = false;
        for (ValueIdentifier right_index = 0; right_index < data_right_size; ++right_index) {
            double normalized_similarity =
                    max_distance > 0 ? 1.0 - (distances[right_index] / max_distance) : 1.0;
            if (normalized_similarity < min_sim) {
                found_dissimilar = true;
                continue;
            }
            AddValue(row_info, right_index, normalized_similarity);
        }
        return found_dissimilar;
    }

    bool DissimilarFound() const noexcept {
        return dissimilar_found.load(std::memory_order::acquire);
    }
};

indexes::SimilarityMeasureOutput DistanceSimilarityMeasure::MakeIndexes(
        std::shared_ptr<DataInfo const> data_info_left,
        std::shared_ptr<DataInfo const> data_info_right,
        std::vector<indexes::PliCluster> const& clusters_right) const {
    ValidTableResults<Similarity> task_data(data_info_left->GetElementNumber());
    ValueProcessingWorker worker{data_info_left, data_info_right, clusters_right,
                                 min_sim_,       task_data,       compute_distance_};

    if (pool_ != nullptr) {
        pool_->SetWork([&worker]() { worker.Start(); });
        pool_->WorkUntilComplete();
    } else {
        worker.Start();
    }

    auto additional_bounds = {1.0, kLowestBound};
    std::span additional_results(additional_bounds.begin(), worker.DissimilarFound() ? 2 : 1);
    auto [similarities, enumerated_results] =
            EncodeResults(std::move(worker.task_data), additional_results);

    if (data_info_left == data_info_right) SymmetricClosure(enumerated_results, clusters_right);

    auto pick_index_uniform = [this](auto const& bounds) {
        return ccv_id_pickers::IndexUniform(bounds.size(), size_limit_);
    };
    return BuildIndexes(std::move(enumerated_results), std::move(similarities), clusters_right,
                        pick_index_uniform);
}

}  // namespace algos::hymd::preprocessing::similarity_measure
