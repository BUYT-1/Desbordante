#pragma once

#include "datasets.h"
#include "fd_algorithm.h"

#include <gtest/gtest.h>

#include <filesystem>

#include "algorithms/algo_factory.h"
#include "algorithms/options/names.h"

template <typename T>
class AlgorithmTest : public LightDatasets, public HeavyDatasets, public ::testing::Test {
protected:
    CSVParser MakeCsvParser(std::string const& path, char separator = ',',
                            bool has_header = true) {
        return {path, separator, has_header};
    }

    std::unique_ptr<algos::FDAlgorithm> CreateAndConfToFit() {
        std::unique_ptr<algos::FDAlgorithm> prim = std::make_unique<T>();
        algos::ConfigureFromMap(*prim, algos::StdParamsMap{});
        return prim;
    }

    algos::StdParamsMap GetParamMap(std::string const& path, char separator = ',',
                                    bool has_header = true) {
        using namespace algos::config::names;
        return {
                {kData, path},
                {kSeparator, separator},
                {kHasHeader, has_header},
                {kError, algos::config::ErrorType{0.0}},
                {kSeed, decltype(Configuration::seed){0}},
        };
    }

    std::unique_ptr<algos::FDAlgorithm> CreateAlgorithmInstance(
            std::string const& path, char separator = ',', bool has_header = true) {
        return algos::CreateAndLoadPrimitive<T>(GetParamMap(path, separator, has_header));
    }
};

