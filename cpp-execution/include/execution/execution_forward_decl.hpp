// execution_forward_decl.hpp
#pragma once

namespace trading {
namespace execution {

// Forward declarations
// Forward declarations
class OrderGenerator;
class SmartOrderRouter;
class OrderTypeSelector;
class ExecutionAlgorithmFactory;  // Add this line
class OrderLifecycleTracker;
class FillReconciliation;
class ApiClientManager;

namespace algorithms {
class ExecutionAlgorithm;
}

// Add any other necessary forward declarations
struct ExecutionResponse;
class ExecutionService;
} // namespace execution
} // namespace trading