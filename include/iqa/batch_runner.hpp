#pragma once

#include <cstddef>
#include <string>

namespace iqa {

struct BatchOptions {
    std::string manifest_path;
    std::string output_path;
};

struct BatchSummary {
    std::size_t total = 0;
    std::size_t succeeded = 0;
    std::size_t failed = 0;
};

BatchSummary runBatch(const BatchOptions& options);

}  // namespace iqa
