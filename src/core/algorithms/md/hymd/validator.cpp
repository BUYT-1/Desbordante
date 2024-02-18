#include "algorithms/md/hymd/validator.h"

#include <cassert>
#include <functional>
#include <vector>

#include "algorithms/md/hymd/decision_boundary_vector.h"
#include "algorithms/md/hymd/indexes/compressed_records.h"
#include "algorithms/md/hymd/lowest_bound.h"
#include "algorithms/md/hymd/table_identifiers.h"
#include "algorithms/md/hymd/utility/java_hash.h"
#include "model/index.h"
#include "util/bitset_utils.h"
#include "util/py_tuple_hash.h"

namespace {
using model::Index, model::md::DecisionBoundary;
using namespace algos::hymd;
using indexes::RecSet;
using RecommendationVector = std::vector<Recommendation>;
using RecordVector = std::vector<CompressedRecord>;
using IndexVector = std::vector<Index>;
using AllRecomVecs = std::vector<RecommendationVector>;
using RecIdVec = std::vector<RecordIdentifier>;

IndexVector GetNonZeroIndices(DecisionBoundaryVector const& lhs) {
    IndexVector indices;
    std::size_t const col_match_number = lhs.size();
    for (Index i = 0; i != col_match_number; ++i) {
        if (lhs[i] != 0) indices.push_back(i);
    }
    return indices;
}

template <typename ElementType>
std::vector<ElementType> GetAllocatedVector(std::size_t size) {
    std::vector<ElementType> vec;
    vec.reserve(size);
    return vec;
}
}  // namespace

namespace std {
template <>
struct hash<vector<ValueIdentifier>> {
    size_t operator()(vector<ValueIdentifier> const& p) const {
        constexpr bool kUseJavaHash = true;
        if constexpr (kUseJavaHash) {
            return utility::HashIterable(p);
        } else {
            auto hasher = util::PyTupleHash<ValueIdentifier>(p.size());
            for (ValueIdentifier el : p) {
                hasher.AddValue(el);
            }
            return hasher.GetResult();
        }
    }
};
}  // namespace std

namespace algos::hymd {

struct WorkingInfo {
    RecommendationVector& recommendations;
    DecisionBoundary const old_bound;
    Index const index;
    DecisionBoundary current_bound;
    std::size_t const col_match_values;
    DecisionBoundary interestingness_boundary;
    RecordVector const& right_records;
    indexes::SimilarityMatrix const& similarity_matrix;
    Index const left_index;
    Index const right_index;

    bool EnoughRecommendations() const {
        return recommendations.size() >= 20;
    }

    bool ShouldStop() const {
        return current_bound == kLowestBound && EnoughRecommendations();
    }

    WorkingInfo(DecisionBoundary old_bound, Index col_match_index,
                RecommendationVector& recommendations, std::size_t col_match_values,
                RecordVector const& right_records,
                indexes::SimilarityMatrix const& similarity_matrix, Index const left_index,
                Index const right_index)
        : recommendations(recommendations),
          old_bound(old_bound),
          index(col_match_index),
          current_bound(old_bound),
          col_match_values(col_match_values),
          right_records(right_records),
          similarity_matrix(similarity_matrix),
          left_index(left_index),
          right_index(right_index) {}
};

RecSet const* Validator::GetSimilarRecords(ValueIdentifier value_id, DecisionBoundary lhs_bound,
                                           Index column_match_index) const {
    assert(lhs_bound != kLowestBound);
    indexes::SimilarityIndex const& similarity_index =
            (*column_matches_info_)[column_match_index].similarity_info.similarity_index;
    indexes::MatchingRecsMapping const& val_index = similarity_index[value_id];
    auto it = val_index.lower_bound(lhs_bound);
    if (it == val_index.end()) return nullptr;
    return &it->second;
}

template <typename PairProvider>
class Validator::SetPairProcessor {
    Validator const* const validator_;
    std::vector<ColumnMatchInfo> const& column_matches_info_ = *validator_->column_matches_info_;
    RecordVector const& left_records_ = validator_->GetLeftCompressor().GetRecords();
    RecordVector const& right_records_ = validator_->GetRightCompressor().GetRecords();
    InvalidatedRhss& invalidated_;
    DecisionBoundaryVector& rhs_bounds_;
    DecisionBoundaryVector const& lhs_bounds_;
    PairProvider pair_provider_;

    enum class Status { kInvalidated, kCheckedAll };

    [[nodiscard]] Status LowerForColumnMatch(WorkingInfo& working_info,
                                             indexes::PliCluster const& cluster,
                                             RecSet const& similar_records) const;
    [[nodiscard]] Status LowerForColumnMatch(
            WorkingInfo& working_info, std::vector<CompressedRecord const*> const& matched_records,
            RecIdVec const& similar_records) const;

    template <typename Collection>
    Status LowerForColumnMatchNoCheck(WorkingInfo& working_info,
                                      std::vector<CompressedRecord const*> const& matched_records,
                                      Collection const& similar_records) const;

    std::pair<std::vector<WorkingInfo>, AllRecomVecs> MakeWorkingAndRecs(
            boost::dynamic_bitset<> const& indices_bitset);

    bool Supported(std::size_t support) {
        return validator_->Supported(support);
    }

    Result MakeAllInvalidatedAndSupportedResult(std::vector<WorkingInfo> const& working,
                                                AllRecomVecs&& recommendations) {
        for (WorkingInfo const& working_info : working) {
            Index const index = working_info.index;
            DecisionBoundary const old_bound = working_info.old_bound;
            DecisionBoundary const new_bound = working_info.current_bound;
            assert(old_bound != kLowestBound);
            assert(new_bound == kLowestBound);
            invalidated_.emplace_back(index, old_bound, kLowestBound);
        }
        return {std::move(recommendations), std::move(invalidated_), false};
    }

    Result MakeOutOfClustersResult(std::vector<WorkingInfo> const& working,
                                   AllRecomVecs&& recommendations, std::size_t support) {
        for (WorkingInfo const& working_info : working) {
            Index const index = working_info.index;
            DecisionBoundary const old_bound = working_info.old_bound;
            DecisionBoundary const new_bound = working_info.current_bound;
            if (new_bound == old_bound) continue;
            invalidated_.emplace_back(index, old_bound, new_bound);
        }
        return {std::move(recommendations), std::move(invalidated_), !Supported(support)};
    }

public:
    SetPairProcessor(Validator const* validator, InvalidatedRhss& invalidated,
                     DecisionBoundaryVector& rhs_bounds, DecisionBoundaryVector const& lhs_bounds,
                     IndexVector const& non_zero_indices)
        : validator_(validator),
          invalidated_(invalidated),
          rhs_bounds_(rhs_bounds),
          lhs_bounds_(lhs_bounds),
          pair_provider_(validator, non_zero_indices, lhs_bounds) {}

    Result ProcessPairs(boost::dynamic_bitset<> const& indices_bitset) {
        auto [working, recommendations] = MakeWorkingAndRecs(indices_bitset);
        std::size_t support = 0;
        while (pair_provider_.TryGetNextPair()) {
            auto const& cluster = pair_provider_.GetCluster();
            auto const& similar = pair_provider_.GetSimilarRecords();
            support += cluster.size() * similar.size();
            bool all_invalid = true;
            for (WorkingInfo& working_info : working) {
                Status const status = LowerForColumnMatch(working_info, cluster, similar);
                if (status == Status::kCheckedAll) all_invalid = false;
            }
            if (all_invalid && Supported(support))
                return MakeAllInvalidatedAndSupportedResult(working, std::move(recommendations));
        }
        return MakeOutOfClustersResult(working, std::move(recommendations), support);
    }
};

template <typename PairProvider>
std::pair<std::vector<WorkingInfo>, AllRecomVecs>
Validator::SetPairProcessor<PairProvider>::MakeWorkingAndRecs(
        boost::dynamic_bitset<> const& indices_bitset) {
    std::pair<std::vector<WorkingInfo>, AllRecomVecs> working_and_recs;
    auto& [working, recommendations] = working_and_recs;
    std::size_t const working_size = indices_bitset.count();
    working.reserve(working_size);
    recommendations.reserve(working_size);
    IndexVector indices = util::BitsetToIndices<Index>(indices_bitset);
    if constexpr (kSortIndices) {
        // TODO: investigate best order.
        std::sort(indices.begin(), indices.end(), [this](Index ind1, Index ind2) {
            return column_matches_info_[ind1].similarity_info.lhs_bounds.size() <
                   column_matches_info_[ind2].similarity_info.lhs_bounds.size();
        });
    }
    for (Index index : indices) {
        RecommendationVector& last_recs = recommendations.emplace_back();
        auto const& [sim_info, left_index, right_index] = column_matches_info_[index];
        working.emplace_back(rhs_bounds_[index], index, last_recs,
                             validator_->GetLeftValueNum(index), right_records_,
                             sim_info.similarity_matrix, left_index, right_index);
    }

    auto for_each_working = [&](auto f) { std::for_each(working.begin(), working.end(), f); };
    for_each_working([&](WorkingInfo const& w) { rhs_bounds_[w.index] = kLowestBound; });
    std::vector<DecisionBoundary> const gen_max_rhs =
            validator_->lattice_->GetRhsInterestingnessBounds(lhs_bounds_, indices);
    for_each_working([&](WorkingInfo const& w) { rhs_bounds_[w.index] = w.old_bound; });

    auto it = working.begin();
    auto set_advance = [&it](DecisionBoundary bound) { it++->interestingness_boundary = bound; };
    std::for_each(gen_max_rhs.begin(), gen_max_rhs.end(), set_advance);
    return working_and_recs;
}

template <typename PairProvider>
template <typename Collection>
auto Validator::SetPairProcessor<PairProvider>::LowerForColumnMatchNoCheck(
        WorkingInfo& working_info, std::vector<CompressedRecord const*> const& matched_records,
        Collection const& similar_records) const -> Status {
    assert(!similar_records.empty());
    assert(!matched_records.empty());

    std::unordered_map<ValueIdentifier, std::vector<CompressedRecord const*>> grouped(
            std::min(matched_records.size(), working_info.col_match_values));
    for (CompressedRecord const* left_record_ptr : matched_records) {
        grouped[(*left_record_ptr)[working_info.left_index]].push_back(left_record_ptr);
    }
    DecisionBoundary& current_rhs_bound = working_info.current_bound;
    indexes::SimilarityMatrix const& similarity_matrix = working_info.similarity_matrix;
    for (auto const& [left_value_id, records_left] : grouped) {
        for (RecordIdentifier record_id_right : similar_records) {
            CompressedRecord const& right_record = working_info.right_records[record_id_right];
            auto add_recommendations = [&records_left, &right_record, &working_info]() {
                for (CompressedRecord const* left_record_ptr : records_left) {
                    working_info.recommendations.emplace_back(left_record_ptr, &right_record);
                }
            };
            auto const& row = similarity_matrix[left_value_id];
            ValueIdentifier const right_value_id = right_record[working_info.right_index];
            auto it_right = row.find(right_value_id);
            if (it_right == row.end()) {
                add_recommendations();
            rhs_not_valid:
                current_rhs_bound = kLowestBound;
                if (working_info.EnoughRecommendations()) return Status::kInvalidated;
                continue;
            }

            preprocessing::Similarity const pair_similarity = it_right->second;
            if (pair_similarity < working_info.old_bound) add_recommendations();
            if (pair_similarity < current_rhs_bound) current_rhs_bound = pair_similarity;
            if (current_rhs_bound <= working_info.interestingness_boundary) goto rhs_not_valid;
        }
    }
    return Status::kCheckedAll;
}

template <typename PairProvider>
auto Validator::SetPairProcessor<PairProvider>::LowerForColumnMatch(
        WorkingInfo& working_info, std::vector<CompressedRecord const*> const& matched_records,
        RecIdVec const& similar_records) const -> Status {
    if (working_info.ShouldStop()) return Status::kInvalidated;
    return LowerForColumnMatchNoCheck(working_info, matched_records, similar_records);
}

template <typename PairProvider>
auto Validator::SetPairProcessor<PairProvider>::LowerForColumnMatch(
        WorkingInfo& working_info, indexes::PliCluster const& cluster,
        RecSet const& similar_records) const -> Status {
    if (working_info.ShouldStop()) return Status::kInvalidated;

    assert(!similar_records.empty());
    std::vector<CompressedRecord const*> cluster_records =
            GetAllocatedVector<CompressedRecord const*>(cluster.size());
    for (RecordIdentifier left_record_id : cluster) {
        cluster_records.push_back(&left_records_[left_record_id]);
    }
    return LowerForColumnMatchNoCheck(working_info, cluster_records, similar_records);
}

class Validator::OneCardPairProvider {
    Validator const* const validator_;
    ValueIdentifier value_id_ = ValueIdentifier(-1);
    Index const non_zero_index_;
    DecisionBoundary const decision_boundary_;
    std::vector<indexes::PliCluster> const& clusters_ =
            validator_->GetLeftCompressor()
                    .GetPli(validator_->GetLeftPliIndex(non_zero_index_))
                    .GetClusters();
    std::size_t const clusters_size_ = clusters_.size();
    RecSet const* similar_records_ptr_{};

public:
    OneCardPairProvider(Validator const* validator, IndexVector const& non_zero_indices,
                        DecisionBoundaryVector const& lhs_bounds)
        : validator_(validator),
          non_zero_index_(non_zero_indices.front()),
          decision_boundary_(lhs_bounds[non_zero_index_]) {}

    bool TryGetNextPair() {
        for (++value_id_; value_id_ != clusters_size_; ++value_id_) {
            similar_records_ptr_ =
                    validator_->GetSimilarRecords(value_id_, decision_boundary_, non_zero_index_);
            if (similar_records_ptr_ != nullptr) return true;
        }
        return false;
    }

    indexes::PliCluster const& GetCluster() const {
        return clusters_[value_id_];
    }

    RecSet const& GetSimilarRecords() const {
        return *similar_records_ptr_;
    }
};

class Validator::MultiCardPairProvider {
    struct InitInfo {
        Validator const* validator;
        std::vector<std::pair<Index, Index>> col_match_val_idx_vec;
        IndexVector non_first_indices;
        DecisionBoundaryVector const& lhs_bounds;
        Index first_pli_index;
        std::size_t plis_involved = 1;

        InitInfo(Validator const* validator, IndexVector const& non_zero_indices,
                 DecisionBoundaryVector const& lhs_bounds)
            : validator(validator), lhs_bounds(lhs_bounds) {
            std::size_t const cardinality = non_zero_indices.size();
            col_match_val_idx_vec.reserve(cardinality);

            std::size_t const left_pli_number = validator->GetLeftCompressor().GetPliNumber();
            non_first_indices.reserve(std::min(cardinality, left_pli_number));
            std::vector<IndexVector> pli_map(left_pli_number);
            for (Index col_match_index : non_zero_indices) {
                pli_map[validator->GetLeftPliIndex(col_match_index)].push_back(col_match_index);
            }

            Index pli_idx = 0;
            while (pli_map[pli_idx].empty()) ++pli_idx;

            Index value_ids_index = 0;
            auto fill_for_value_ids_idx = [this, &value_ids_index](IndexVector const& indices) {
                for (Index const col_match_idx : indices) {
                    col_match_val_idx_vec.emplace_back(col_match_idx, value_ids_index);
                }
                ++value_ids_index;
            };
            first_pli_index = pli_idx;
            fill_for_value_ids_idx(pli_map[pli_idx]);
            for (++pli_idx; pli_idx != left_pli_number; ++pli_idx) {
                IndexVector const& col_match_idxs = pli_map[pli_idx];
                if (col_match_idxs.empty()) continue;
                ++plis_involved;
                non_first_indices.push_back(pli_idx);
                fill_for_value_ids_idx(col_match_idxs);
            }
        }
    };

    using RecordCluster = std::vector<CompressedRecord const*>;
    using GroupMap = std::unordered_map<std::vector<ValueIdentifier>, RecordCluster>;

    Validator const* const validator_;
    GroupMap grouped_;
    GroupMap::iterator cur_group_iter_ = grouped_.begin();
    GroupMap::iterator end_group_iter_ = grouped_.end();
    ValueIdentifier first_value_id_ = ValueIdentifier(-1);
    std::vector<ValueIdentifier> value_ids_;
    std::vector<RecSet const*> rec_sets_;
    IndexVector const non_first_indices_;
    IndexVector::const_iterator non_first_start_ = non_first_indices_.begin();
    IndexVector::const_iterator non_first_end_ = non_first_indices_.end();
    std::vector<indexes::PliCluster> const& first_pli_;
    std::size_t first_pli_size_ = first_pli_.size();
    RecordVector const& left_records_ = validator_->GetLeftCompressor().GetRecords();
    std::vector<std::pair<Index, Index>> const col_match_val_idx_vec_;
    DecisionBoundaryVector const& lhs_bounds_;
    RecIdVec similar_records_;
    RecordCluster const* cluster_ptr_;

    MultiCardPairProvider(InitInfo init_info)
        : validator_(init_info.validator),
          non_first_indices_(std::move(init_info.non_first_indices)),
          first_pli_(
                  validator_->GetLeftCompressor().GetPli(init_info.first_pli_index).GetClusters()),
          col_match_val_idx_vec_(std::move(init_info.col_match_val_idx_vec)),
          lhs_bounds_(init_info.lhs_bounds) {
        value_ids_.reserve(init_info.plis_involved);
        rec_sets_.reserve(col_match_val_idx_vec_.size());
    }

    bool TryGetNextGroup() {
        if (++first_value_id_ == first_pli_size_) return false;
        grouped_.clear();
        indexes::PliCluster const& cluster = first_pli_[first_value_id_];
        value_ids_.push_back(first_value_id_);
        for (RecordIdentifier record_id : cluster) {
            std::vector<ValueIdentifier> const& record = left_records_[record_id];
            for (auto ind_it = non_first_start_; ind_it != non_first_end_; ++ind_it) {
                value_ids_.push_back(record[*ind_it]);
            }
            grouped_[value_ids_].push_back(&record);
            value_ids_.erase(++value_ids_.begin(), value_ids_.end());
        }
        value_ids_.clear();
        cur_group_iter_ = grouped_.begin();
        end_group_iter_ = grouped_.end();
        return true;
    }

public:
    MultiCardPairProvider(Validator const* validator, IndexVector const& non_zero_indices,
                          DecisionBoundaryVector const& lhs_bounds)
        : MultiCardPairProvider(InitInfo{validator, non_zero_indices, lhs_bounds}) {}

    bool TryGetNextPair() {
        similar_records_.clear();
        do {
            while (cur_group_iter_ != end_group_iter_) {
                auto const& [val_ids, cluster] = *cur_group_iter_;
                rec_sets_.clear();
                ++cur_group_iter_;
                for (auto const& [column_match_index, value_ids_index] : col_match_val_idx_vec_) {
                    RecSet const* similar_records_ptr = validator_->GetSimilarRecords(
                            val_ids[value_ids_index], lhs_bounds_[column_match_index],
                            column_match_index);
                    if (similar_records_ptr == nullptr) goto no_similar_records;
                    rec_sets_.push_back(similar_records_ptr);
                }
                goto matched_on_all;
            no_similar_records:
                continue;
            matched_on_all:
                auto check_set_begin = rec_sets_.begin();
                auto check_set_end = rec_sets_.end();
                using CRecSet = RecSet const;
                auto size_cmp = [](CRecSet* p1, CRecSet* p2) { return p1->size() < p2->size(); };
                std::sort(check_set_begin, check_set_end, size_cmp);
                CRecSet& first = **check_set_begin;
                ++check_set_begin;
                for (RecordIdentifier rec : first) {
                    auto rec_cont = [rec](CRecSet* set_ptr) { return set_ptr->contains(rec); };
                    if (std::all_of(check_set_begin, check_set_end, rec_cont)) {
                        similar_records_.push_back(rec);
                    }
                }
                if (similar_records_.empty()) continue;
                cluster_ptr_ = &cluster;
                return true;
            }
        } while (TryGetNextGroup());
        return false;
    }

    RecordCluster const& GetCluster() const {
        return *cluster_ptr_;
    }

    RecIdVec const& GetSimilarRecords() const {
        return similar_records_;
    }
};

Validator::Result Validator::Validate(lattice::ValidationInfo& info) const {
    DecisionBoundaryVector const& lhs_bounds = info.node_info->lhs_bounds;
    DecisionBoundaryVector& rhs_bounds = *info.node_info->rhs_bounds;
    // After a call to this method, info.rhs_indices must not be used
    boost::dynamic_bitset<>& indices_bitset = info.rhs_indices;
    IndexVector non_zero_indices = GetNonZeroIndices(lhs_bounds);
    std::size_t const cardinality = non_zero_indices.size();
    InvalidatedRhss invalidated = GetAllocatedVector<InvalidatedRhs>(indices_bitset.count());
    if (cardinality == 0) [[unlikely]] {
        util::ForEachIndex(indices_bitset, [&](auto index) {
            DecisionBoundary const old_bound = rhs_bounds[index];
            DecisionBoundary const new_bound =
                    (*column_matches_info_)[index].similarity_info.lowest_similarity;
            if (old_bound == new_bound) [[unlikely]]
                return;
            invalidated.emplace_back(index, old_bound, new_bound);
        });
        return {{}, std::move(invalidated), !Supported(GetTotalPairsNum())};
    }

    if (cardinality == 1) {
        Index const non_zero_index = non_zero_indices.front();
        // Never happens when disjointedness pruning is on.
        if (indices_bitset.test_set(non_zero_index, false)) {
            invalidated.emplace_back(non_zero_index, rhs_bounds[non_zero_index], kLowestBound);
        }
        SetPairProcessor<OneCardPairProvider> processor(this, invalidated, rhs_bounds, lhs_bounds,
                                                        non_zero_indices);
        return processor.ProcessPairs(indices_bitset);
    }

    SetPairProcessor<MultiCardPairProvider> processor(this, invalidated, rhs_bounds, lhs_bounds,
                                                      non_zero_indices);
    return processor.ProcessPairs(indices_bitset);
}

}  // namespace algos::hymd