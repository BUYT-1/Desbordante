#pragma once

#include <cstddef>
#include <vector>

#include "algorithms/md/decision_boundary.h"
#include "algorithms/md/hymd/decision_boundary_vector.h"
#include "algorithms/md/hymd/lattice/lattice_child_array.h"
#include "algorithms/md/hymd/lattice/md_lattice_node_info.h"
#include "algorithms/md/hymd/similarity_vector.h"
#include "model/index.h"

namespace algos::hymd::lattice {

class MdLatticeNode {
private:
    DecisionBoundaryVector rhs_;
    LatticeChildArray<MdLatticeNode> children_;

    void AddUnchecked(DecisionBoundaryVector const& lhs_sims, model::md::DecisionBoundary rhs_sim,
                      model::Index rhs_index, model::Index this_node_index);

public:
    void GetLevel(std::vector<MdLatticeNodeInfo>& collected, DecisionBoundaryVector& this_node_lhs,
                  model::Index this_node_index, size_t sims_left);
    void GetMaxValidGeneralizationRhs(DecisionBoundaryVector const& lhs,
                                      std::vector<model::md::DecisionBoundary>& cur_rhs,
                                      model::Index this_node_index) const;

    [[nodiscard]] bool HasGeneralization(DecisionBoundaryVector const& lhs_sims,
                                         model::md::DecisionBoundary rhs_sim,
                                         model::Index rhs_index,
                                         model::Index this_node_index) const;
    void AddIfMinimal(DecisionBoundaryVector const& lhs_sims, model::md::DecisionBoundary rhs_sim,
                      model::Index rhs_index, model::Index this_node_index);

    void FindViolated(std::vector<MdLatticeNodeInfo>& found, DecisionBoundaryVector& this_node_lhs,
                      SimilarityVector const& similarity_vector, model::Index this_node_index);

    explicit MdLatticeNode(size_t attributes_num) : rhs_(attributes_num) {}
    explicit MdLatticeNode(DecisionBoundaryVector rhs) : rhs_(std::move(rhs)) {}
};

}  // namespace algos::hymd::lattice