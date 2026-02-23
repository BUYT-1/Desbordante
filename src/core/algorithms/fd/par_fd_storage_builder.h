#pragma once

#include <deque>
#include <mutex>

#include "core/algorithms/fd/fd_storage.h"

namespace algos {
class ParFdStorageBuilder {
    model::TableHeader table_header_;
    std::deque<FdStorage::StrippedFd> stripped_fds_;
    std::mutex mutex_;

public:
    ParFdStorageBuilder(model::TableHeader table_header)
        : table_header_(std::move(table_header)) {};

    void AddFd(FdStorage::StrippedFd fd) {
        std::scoped_lock lock{mutex_};
        stripped_fds_.push_back(std::move(fd));
    }

    FdStoragePtr Build() {
        return std::make_shared<FdStorage>(std::move(table_header_), std::move(stripped_fds_));
    }

    void Reset() {
        stripped_fds_.clear();
    }
};
}  // namespace algos
