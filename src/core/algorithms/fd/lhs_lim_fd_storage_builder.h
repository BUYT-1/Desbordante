#pragma once

#include <deque>
#include <memory>

#include "core/algorithms/fd/fd_storage.h"
#include "core/config/max_lhs/type.h"

namespace algos {
class LhsLimFdStorageBuilder {
    std::deque<FdStorage::StrippedFd> stripped_fds_;
    config::MaxLhsType max_lhs_;

public:
    LhsLimFdStorageBuilder(config::MaxLhsType max_lhs) : max_lhs_(max_lhs) {}

    void AddFd(FdStorage::StrippedFd fd) {
        if (fd.lhs.count() > max_lhs_) return;
        stripped_fds_.push_back(std::move(fd));
    }

    FdStoragePtr Build(model::TableHeader table_header) {
        return std::make_shared<FdStorage>(std::move(table_header), std::move(stripped_fds_));
    }

    void Reset() {
        stripped_fds_.clear();
    }
};
}  // namespace algos
