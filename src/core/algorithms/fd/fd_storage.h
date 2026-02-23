#pragma once

#include <deque>
#include <memory>
#include <ranges>

#include <boost/dynamic_bitset.hpp>

#include "core/algorithms/fd/fd.h"
#include "core/model/index.h"
#include "core/model/table/table_header.h"
#include "core/util/bitset_utils.h"

namespace algos {
class FdStorage {
public:
    struct StrippedFd {
        boost::dynamic_bitset<> lhs;
        boost::dynamic_bitset<> rhs;

        model::FunctionalDependency ToFd(model::TableHeader const& table_header_) const {
            std::vector<model::Attribute> lhs_attrs;
            lhs_attrs.reserve(lhs.count());
            util::ForEachIndex(lhs, [&](model::Index i) {
                lhs_attrs.emplace_back(table_header_.column_names[i], i);
            });
            std::vector<model::Attribute> rhs_attrs;
            rhs_attrs.reserve(rhs.count());
            util::ForEachIndex(rhs, [&](model::Index i) {
                rhs_attrs.emplace_back(table_header_.column_names[i], i);
            });
            return {table_header_.table_name, std::move(lhs_attrs), std::move(rhs_attrs)};
        }
    };

private:
    model::TableHeader table_header_;
    std::deque<StrippedFd> stripped_fds_;

    auto FullView() const {
        return std::ranges::views::transform(stripped_fds_, [this](StrippedFd const& stripped_fd) {
            return stripped_fd.ToFd(table_header_);
        });
    }

public:
    FdStorage(model::TableHeader table_header, std::deque<StrippedFd> stripped_fds)
        : table_header_(std::move(table_header)), stripped_fds_(std::move(stripped_fds)) {}

    std::deque<StrippedFd> const& GetStripped() const noexcept {
        return stripped_fds_;
    }

    auto begin() const {
        return FullView().begin();
    }

    auto end() const {
        return FullView().end();
    }
};

// Use this, not the class itself, don't waste memory copying
using FdStoragePtr = std::shared_ptr<FdStorage>;
}  // namespace algos
