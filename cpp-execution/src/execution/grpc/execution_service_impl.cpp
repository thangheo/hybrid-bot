#include "execution/grpc/execution_service_impl.hpp"
#include <google/protobuf/util/json_util.h>
#include <iostream>

namespace trading {
namespace execution {
namespace grpc {

ExecutionServiceImpl::ExecutionServiceImpl(
    std::shared_ptr<trading::execution::ExecutionService> execution_service)
    : execution_service_(std::move(execution_service)) {
    if (!execution_service_) {
        throw std::invalid_argument("ExecutionService cannot be null");
    }
}

::grpc::Status ExecutionServiceImpl::ExecuteSignal(
    ::grpc::ServerContext* context,
    const ::trading::execution::grpc::TradingSignalProto* request,
    ::trading::execution::grpc::ExecutionResponseProto* response) {
    
    try {
        // Proto → JSON (built-in, fast)
        std::string request_json = protoToJson(*request);
        
        // Your existing service handles everything
        std::string response_json = execution_service_->executeSignalJson(request_json);
        
        // JSON → Proto (built-in, fast)
        *response = jsonToProto<::trading::execution::grpc::ExecutionResponseProto>(response_json);
        
        return ::grpc::Status::OK;
        
    } catch (const std::exception& e) {
        return handleException(e);
    }
}

::grpc::Status ExecutionServiceImpl::PlaceOrder(
    ::grpc::ServerContext* context,
    const ::trading::execution::grpc::OrderProto* request,
    ::trading::execution::grpc::ExecutionResponseProto* response) {
    
    try {
        std::string request_json = protoToJson(*request);
        std::string response_json = execution_service_->placeOrderJson(request_json);
        *response = jsonToProto<::trading::execution::grpc::ExecutionResponseProto>(response_json);
        return ::grpc::Status::OK;
    } catch (const std::exception& e) {
        return handleException(e);
    }
}

::grpc::Status ExecutionServiceImpl::CancelOrder(
    ::grpc::ServerContext* context,
    const ::trading::execution::grpc::CancelOrderRequestProto* request,
    ::trading::execution::grpc::CancelResponseProto* response) {
    
    try {
        std::string request_json = protoToJson(*request);
        std::string response_json = execution_service_->cancelOrderJson(request_json);
        *response = jsonToProto<::trading::execution::grpc::CancelResponseProto>(response_json);
        return ::grpc::Status::OK;
    } catch (const std::exception& e) {
        return handleException(e);
    }
}

::grpc::Status ExecutionServiceImpl::GetOrderStatus(
    ::grpc::ServerContext* context,
    const ::trading::execution::grpc::OrderStatusRequestProto* request,
    ::trading::execution::grpc::OrderStatusResponseProto* response) {
    
    try {
        std::string request_json = protoToJson(*request);
        std::string response_json = execution_service_->getOrderStatusJson(request_json);
        *response = jsonToProto<::trading::execution::grpc::OrderStatusResponseProto>(response_json);
        return ::grpc::Status::OK;
    } catch (const std::exception& e) {
        return handleException(e);
    }
}

::grpc::Status ExecutionServiceImpl::GetActiveOrders(
    ::grpc::ServerContext* context,
    const ::trading::execution::grpc::ActiveOrdersRequestProto* request,
    ::trading::execution::grpc::ActiveOrdersResponseProto* response) {
    
    try {
        std::string request_json = protoToJson(*request);
        std::string response_json = execution_service_->getActiveOrdersJson(request_json);
        *response = jsonToProto<::trading::execution::grpc::ActiveOrdersResponseProto>(response_json);
        return ::grpc::Status::OK;
    } catch (const std::exception& e) {
        return handleException(e);
    }
}

::grpc::Status ExecutionServiceImpl::GetHealthStatus(
    ::grpc::ServerContext* context,
    const ::trading::execution::grpc::HealthStatusRequestProto* request,
    ::trading::execution::grpc::HealthStatusResponseProto* response) {
    
    try {
        std::string health_json = execution_service_->getHealthStatus();
        response->set_health_json(health_json);
        return ::grpc::Status::OK;
    } catch (const std::exception& e) {
        return handleException(e);
    }
}

// Utility methods
std::string ExecutionServiceImpl::protoToJson(const google::protobuf::Message& proto) const {
    std::string json;
    google::protobuf::util::MessageToJsonString(proto, &json);
    return json;
}

template<typename ProtoType>
ProtoType ExecutionServiceImpl::jsonToProto(const std::string& json) const {
    ProtoType proto;
    google::protobuf::util::JsonStringToMessage(json, &proto);
    return proto;
}

::grpc::Status ExecutionServiceImpl::handleException(const std::exception& e) const {
    std::cerr << "ExecutionService error: " << e.what() << std::endl;
    return ::grpc::Status(::grpc::StatusCode::INTERNAL, e.what());
}

} // namespace grpc
} // namespace execution
} // namespace trading