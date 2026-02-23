#pragma once

#include <deque>
#include <memory>

#include "core/algorithms/fd/fd_storage.h"

namespace algos {
class PlainFdStorageBuilder {
    std::deque<FdStorage::StrippedFd> stripped_fds_;

public:
    void AddFd(FdStorage::StrippedFd fd) {
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
