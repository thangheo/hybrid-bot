#pragma once

#include "execution/execution_service.hpp"
#include "execution/execution_types.hpp"
#include "execution_service.grpc.pb.h"
#include "common.pb.h"
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include <google/protobuf/util/json_util.h>

namespace trading {
namespace execution {
namespace grpc {

class ExecutionServiceImpl final : public ::trading::execution::grpc::ExecutionService::Service {
public:
    ExecutionServiceImpl(std::shared_ptr<trading::execution::ExecutionService> execution_service);
    
    // gRPC interface methods
    ::grpc::Status ExecuteSignal(
        ::grpc::ServerContext* context, 
        const ::trading::execution::grpc::TradingSignalProto* request,
        ::trading::execution::grpc::ExecutionResponseProto* response) override;
    
    ::grpc::Status PlaceOrder(
        ::grpc::ServerContext* context, 
        const ::trading::execution::grpc::OrderProto* request,
        ::trading::execution::grpc::ExecutionResponseProto* response) override;
    
    ::grpc::Status CancelOrder(
        ::grpc::ServerContext* context, 
        const ::trading::execution::grpc::CancelOrderRequestProto* request,
        ::trading::execution::grpc::CancelResponseProto* response) override;
    
    ::grpc::Status GetOrderStatus(
        ::grpc::ServerContext* context, 
        const ::trading::execution::grpc::OrderStatusRequestProto* request,
        ::trading::execution::grpc::OrderStatusResponseProto* response) override;
    
    ::grpc::Status GetActiveOrders(
        ::grpc::ServerContext* context, 
        const ::trading::execution::grpc::ActiveOrdersRequestProto* request,
        ::trading::execution::grpc::ActiveOrdersResponseProto* response) override;
    
    ::grpc::Status GetHealthStatus(
        ::grpc::ServerContext* context, 
        const ::trading::execution::grpc::HealthStatusRequestProto* request,
        ::trading::execution::grpc::HealthStatusResponseProto* response) override;

private:
    std::shared_ptr<trading::execution::ExecutionService> execution_service_;
    
    // Simple proto ↔ JSON conversion (built-in protobuf utilities)
    std::string protoToJson(const google::protobuf::Message& proto) const;
    template<typename ProtoType>
    ProtoType jsonToProto(const std::string& json) const;
    
    // Error handling
    ::grpc::Status handleException(const std::exception& e) const;
};

} // namespace grpc

// Server wrapper (unchanged)
class ExecutionServiceServer {
public:
    ExecutionServiceServer(
        std::shared_ptr<trading::execution::ExecutionService> execution_service,
        const std::string& server_address);
    
    void start();
    void shutdown();
    void waitForShutdown();

private:
    std::unique_ptr<::grpc::Server> server_;
    trading::execution::grpc::ExecutionServiceImpl service_;
    std::string server_address_;
};

} // namespace execution
} // namespace trading