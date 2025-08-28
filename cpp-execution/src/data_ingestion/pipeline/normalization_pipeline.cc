// src/data_ingestion/pipeline/normalization_pipeline.cpp
#include "data_ingestion/pipeline/normalization_pipeline.hpp"
#include <iostream>

namespace trading {
namespace data_ingestion {

// We'll implement any non-templated methods here
// Most of the implementation is in the header due to templates
// but we can add concrete implementations like logging or metric initialization

// Static method to print pipeline status
void printPipelineStatus(const NormalizationPipeline::PipelineStatus& status) {
    std::cout << "Pipeline Status: " << (status.is_running ? "Running" : "Stopped") << std::endl;
    std::cout << "Stages:" << std::endl;
    
    for (const auto& stage : status.stages) {
        std::cout << "  - " << stage.name << ":" << std::endl;
        std::cout << "    Queue size: " << stage.queue_size << std::endl;
        std::cout << "    Processed: " << stage.processed_count << std::endl;
        std::cout << "    Errors: " << stage.error_count << std::endl;
        std::cout << "    Running: " << (stage.is_running ? "Yes" : "No") << std::endl;
    }
}

} // namespace data_ingestion
} // namespace trading
