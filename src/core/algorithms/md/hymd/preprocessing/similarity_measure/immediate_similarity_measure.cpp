#include "algorithms/md/hymd/preprocessing/similarity_measure/immediate_similarity_measure.h"

#include "algorithms/md/hymd/preprocessing/similarity_measure/value_processing_worker.h"

namespace algos::hymd::preprocessing::similarity_measure {

template <typename IndexType>
class ImmediateValueProcessingWorker : public ValueProcessingWorker<IndexType> {
    using Base = ValueProcessingWorker<IndexType>;

    std::function<Similarity(std::byte const*, std::byte const*)> compute_similarity_;

public:
    ImmediateValueProcessingWorker(
            std::shared_ptr<DataInfo const> const& data_info_left,
            std::shared_ptr<DataInfo const> const& data_info_right,
            std::vector<indexes::PliCluster> const& clusters_right,
            ValidTableResults<Similarity>& task_data,
            std::function<Similarity(std::byte const*, std::byte const*)> compute_similarity)
        : Base(data_info_left, data_info_right, clusters_right, task_data),
          compute_similarity_(compute_similarity) {}

    bool CalcAndAdd(ValueIdentifier left_value_id, RowInfo<Similarity>& row_info,
                    ValueIdentifier start_from) override {
        bool dissimilar_found_here = false;
        for (ValueIdentifier value_id_right = start_from; value_id_right != Base::data_right_size_;
             ++value_id_right) {
            Similarity similarity =
                    compute_similarity_(Base::data_info_left_->GetAt(left_value_id),
                                        Base::data_info_right_->GetAt(value_id_right));
            if (similarity == kLowestBound) {
                dissimilar_found_here = true;
                continue;
            }
            Base::AddValue(row_info, value_id_right, similarity);
        }
        return dissimilar_found_here;
    }
};

indexes::SimilarityMeasureOutput ImmediateSimilarityMeasure::MakeIndexes(
        std::shared_ptr<DataInfo const> data_info_left,
        std::shared_ptr<DataInfo const> data_info_right,
        std::vector<indexes::PliCluster> const& clusters_right) const {
    return MakeIndexesTemplate<ImmediateValueProcessingWorker>(
            data_info_left, data_info_right, clusters_right, pool_, picker_, compute_similarity_);
}
}  // namespace algos::hymd::preprocessing::similarity_measure
