#pragma once

#include "zmq_server_client.hpp"
#include <google/protobuf/message.h>
#include <google/protobuf/util/delimited_message_util.h>
#include <type_traits>
#include <memory>

// Protobuf-specific type aliases
using ProtobufMessageHandler = std::function<void(const std::string& topic, 
                                                 std::unique_ptr<google::protobuf::Message> message,
                                                 const std::unordered_map<std::string, std::string>& metadata)>;

using ProtobufRequestHandler = std::function<std::unique_ptr<google::protobuf::Message>(
    std::unique_ptr<google::protobuf::Message> request,
    const std::unordered_map<std::string, std::string>& metadata)>;

// Protobuf serialization options
struct ProtobufOptions {
    bool use_delimited_format = true;  // Use length-delimited format for better parsing
    bool compress = false;             // Enable compression (future extension)
    bool validate_on_parse = true;     // Validate messages on parsing
};

/**
 * Protobuf-optimized ZMQ Server Extension
 * Extends GenericZmqServer with protobuf-specific methods while maintaining backward compatibility
 */
class ProtobufZmqServer : public GenericZmqServer {
public:
    explicit ProtobufZmqServer(const ZmqServerConfig& config, const ProtobufOptions& pb_options = {})
        : GenericZmqServer(config), protobuf_options_(pb_options) {}

    // Protobuf publishing methods
    template<typename T>
    bool publishProtobuf(const std::string& topic, const T& message, 
                        MessagePriority priority = MessagePriority::NORMAL,
                        const std::string& correlation_id = "") {
        static_assert(std::is_base_of_v<google::protobuf::Message, T>, 
                     "T must be a protobuf message type");
        
        std::string serialized_data;
        if (!serializeMessage(message, serialized_data)) {
            setLastError("Failed to serialize protobuf message");
            return false;
        }

        // Add protobuf metadata
        std::unordered_map<std::string, std::string> metadata;
        metadata["message_type"] = message.GetTypeName();
        metadata["serialization"] = protobuf_options_.use_delimited_format ? "delimited" : "binary";
        if (!correlation_id.empty()) {
            metadata["correlation_id"] = correlation_id;
        }

        return publishWithMetadata(topic, serialized_data, metadata, priority);
    }

    template<typename T>
    bool publishProtobufSync(const std::string& topic, const T& message,
                            MessagePriority priority = MessagePriority::NORMAL,
                            const std::string& correlation_id = "") {
        static_assert(std::is_base_of_v<google::protobuf::Message, T>, 
                     "T must be a protobuf message type");
        
        std::string serialized_data;
        if (!serializeMessage(message, serialized_data)) {
            setLastError("Failed to serialize protobuf message");
            return false;
        }

        // Add protobuf metadata
        std::unordered_map<std::string, std::string> metadata;
        metadata["message_type"] = message.GetTypeName();
        metadata["serialization"] = protobuf_options_.use_delimited_format ? "delimited" : "binary";
        if (!correlation_id.empty()) {
            metadata["correlation_id"] = correlation_id;
        }

        return publishSync(topic, serialized_data, priority, correlation_id);
    }

    // Protobuf request handling
    template<typename RequestType, typename ResponseType>
    void registerProtobufHandler(const std::string& request_type,
                                std::function<ResponseType(const RequestType&, 
                                                         const std::unordered_map<std::string, std::string>&)> handler) {
        auto wrapper = [this, handler](const std::string& data, 
                                     const std::unordered_map<std::string, std::string>& metadata) -> std::string {
            try {
                RequestType request;
                if (!deserializeMessage(data, request)) {
                    return createErrorResponse("Failed to deserialize request");
                }

                ResponseType response = handler(request, metadata);
                
                std::string serialized_response;
                if (!serializeMessage(response, serialized_response)) {
                    return createErrorResponse("Failed to serialize response");
                }

                return serialized_response;
            } catch (const std::exception& e) {
                return createErrorResponse("Handler exception: " + std::string(e.what()));
            }
        };

        registerRequestHandler(request_type, wrapper);
    }

private:
    ProtobufOptions protobuf_options_;

    template<typename T>
    bool serializeMessage(const T& message, std::string& output) {
        try {
            if (protobuf_options_.use_delimited_format) {
                google::protobuf::io::StringOutputStream string_stream(&output);
                return google::protobuf::util::SerializeDelimitedToZeroCopyStream(message, &string_stream);
            } else {
                return message.SerializeToString(&output);
            }
        } catch (const std::exception& e) {
            setLastError("Protobuf serialization error: " + std::string(e.what()));
            return false;
        }
    }

    template<typename T>
    bool deserializeMessage(const std::string& data, T& message) {
        try {
            if (protobuf_options_.use_delimited_format) {
                google::protobuf::io::ArrayInputStream array_stream(data.data(), data.size());
                return google::protobuf::util::ParseDelimitedFromZeroCopyStream(&message, &array_stream, nullptr);
            } else {
                return message.ParseFromString(data);
            }
        } catch (const std::exception& e) {
            setLastError("Protobuf deserialization error: " + std::string(e.what()));
            return false;
        }
    }

    std::string createErrorResponse(const std::string& error_message) {
        // Create a simple JSON error response
        nlohmann::json error_json;
        error_json["error"] = error_message;
        error_json["success"] = false;
        return error_json.dump();
    }
};

/**
 * Protobuf-optimized ZMQ Client Extension
 */
class ProtobufZmqClient : public GenericZmqClient {
public:
    explicit ProtobufZmqClient(const ZmqClientConfig& config, const ProtobufOptions& pb_options = {})
        : GenericZmqClient(config), protobuf_options_(pb_options) {}

    // Protobuf subscription methods
    template<typename T>
    bool subscribeProtobuf(const std::string& topic, 
                          std::function<void(const std::string&, const T&, 
                                           const std::unordered_map<std::string, std::string>&)> handler) {
        static_assert(std::is_base_of_v<google::protobuf::Message, T>, 
                     "T must be a protobuf message type");

        auto wrapper = [this, handler](const std::string& recv_topic, const std::string& data,
                                     const std::unordered_map<std::string, std::string>& metadata) {
            try {
                // Check if this is a protobuf message
                auto it = metadata.find("message_type");
                if (it == metadata.end()) {
                    std::cerr << "Received non-protobuf message on protobuf subscription" << std::endl;
                    return;
                }

                T message;
                if (!deserializeMessage(data, message)) {
                    std::cerr << "Failed to deserialize protobuf message of type: " << it->second << std::endl;
                    return;
                }

                handler(recv_topic, message, metadata);
            } catch (const std::exception& e) {
                std::cerr << "Protobuf handler exception: " << e.what() << std::endl;
            }
        };

        return subscribe(topic, wrapper);
    }

    // Mixed subscription for both protobuf and raw messages
    bool subscribeHybrid(const std::string& topic, 
                        MessageHandler raw_handler,
                        ProtobufMessageHandler protobuf_handler) {
        auto wrapper = [this, raw_handler, protobuf_handler](const std::string& recv_topic, 
                                                            const std::string& data,
                                                            const std::unordered_map<std::string, std::string>& metadata) {
            auto it = metadata.find("message_type");
            if (it != metadata.end()) {
                // This is a protobuf message
                auto message = deserializeAnyMessage(data, it->second);
                if (message) {
                    protobuf_handler(recv_topic, std::move(message), metadata);
                } else {
                    std::cerr << "Failed to deserialize protobuf message of type: " << it->second << std::endl;
                }
            } else {
                // This is a raw message
                raw_handler(recv_topic, data, metadata);
            }
        };

        return subscribe(topic, wrapper);
    }

    // Protobuf request methods
    template<typename RequestType, typename ResponseType>
    ResponseType sendProtobufRequest(const std::string& request_type, const RequestType& request,
                                   const std::unordered_map<std::string, std::string>& metadata = {}) {
        static_assert(std::is_base_of_v<google::protobuf::Message, RequestType>, 
                     "RequestType must be a protobuf message type");
        static_assert(std::is_base_of_v<google::protobuf::Message, ResponseType>, 
                     "ResponseType must be a protobuf message type");

        std::string serialized_request;
        if (!serializeMessage(request, serialized_request)) {
            throw std::runtime_error("Failed to serialize protobuf request");
        }

        // Add protobuf metadata
        auto extended_metadata = metadata;
        extended_metadata["request_type"] = request.GetTypeName();
        extended_metadata["response_type"] = ResponseType().GetTypeName();
        extended_metadata["serialization"] = protobuf_options_.use_delimited_format ? "delimited" : "binary";

        std::string response_data = sendRequest(request_type, serialized_request, extended_metadata);
        
        ResponseType response;
        if (!deserializeMessage(response_data, response)) {
            throw std::runtime_error("Failed to deserialize protobuf response");
        }

        return response;
    }

    template<typename RequestType, typename ResponseType>
    std::future<ResponseType> sendProtobufRequestAsync(const std::string& request_type, 
                                                      const RequestType& request,
                                                      const std::unordered_map<std::string, std::string>& metadata = {}) {
        return std::async(std::launch::async, [this, request_type, request, metadata]() {
            return sendProtobufRequest<RequestType, ResponseType>(request_type, request, metadata);
        });
    }

private:
    ProtobufOptions protobuf_options_;

    template<typename T>
    bool serializeMessage(const T& message, std::string& output) {
        try {
            if (protobuf_options_.use_delimited_format) {
                google::protobuf::io::StringOutputStream string_stream(&output);
                return google::protobuf::util::SerializeDelimitedToZeroCopyStream(message, &string_stream);
            } else {
                return message.SerializeToString(&output);
            }
        } catch (const std::exception& e) {
            setLastError("Protobuf serialization error: " + std::string(e.what()));
            return false;
        }
    }

    template<typename T>
    bool deserializeMessage(const std::string& data, T& message) {
        try {
            if (protobuf_options_.use_delimited_format) {
                google::protobuf::io::ArrayInputStream array_stream(data.data(), data.size());
                return google::protobuf::util::ParseDelimitedFromZeroCopyStream(&message, &array_stream, nullptr);
            } else {
                return message.ParseFromString(data);
            }
        } catch (const std::exception& e) {
            setLastError("Protobuf deserialization error: " + std::string(e.what()));
            return false;
        }
    }

    std::unique_ptr<google::protobuf::Message> deserializeAnyMessage(const std::string& data, 
                                                                    const std::string& type_name) {
        try {
            const google::protobuf::Descriptor* descriptor = 
                google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type_name);
            
            if (!descriptor) {
                std::cerr << "Unknown message type: " << type_name << std::endl;
                return nullptr;
            }

            const google::protobuf::Message* prototype = 
                google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor);
            
            if (!prototype) {
                std::cerr << "No prototype for message type: " << type_name << std::endl;
                return nullptr;
            }

            auto message = std::unique_ptr<google::protobuf::Message>(prototype->New());
            
            if (protobuf_options_.use_delimited_format) {
                google::protobuf::io::ArrayInputStream array_stream(data.data(), data.size());
                if (google::protobuf::util::ParseDelimitedFromZeroCopyStream(message.get(), &array_stream, nullptr)) {
                    return message;
                }
            } else {
                if (message->ParseFromString(data)) {
                    return message;
                }
            }

            return nullptr;
        } catch (const std::exception& e) {
            std::cerr << "Error deserializing message: " << e.what() << std::endl;
            return nullptr;
        }
    }
};

// Factory functions for easier creation
inline std::unique_ptr<ProtobufZmqServer> createProtobufServer(const ZmqServerConfig& config, 
                                                              const ProtobufOptions& pb_options = {}) {
    return std::make_unique<ProtobufZmqServer>(config, pb_options);
}

inline std::unique_ptr<ProtobufZmqClient> createProtobufClient(const ZmqClientConfig& config, 
                                                              const ProtobufOptions& pb_options = {}) {
    return std::make_unique<ProtobufZmqClient>(config, pb_options);
}
