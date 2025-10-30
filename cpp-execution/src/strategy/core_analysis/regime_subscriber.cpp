// regime_subscriber.cpp
// Implementation of RegimeSubscriber declared in regime_subscriber.hpp

#include "strategy/core_analysis/regime_subscriber.hpp"

#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <csignal>

using json = nlohmann::json;
using namespace trading::strategy::core_analysis;

struct trading::strategy::core_analysis::RegimeSubscriber::Impl {
    std::string addr;
    std::string topic;
    mutable std::mutex mtx;

    std::unordered_map<std::string, RegimeInfo> store;

    std::atomic<bool> running{false};
    std::thread thread;

    Impl(const std::string& a, const std::string& t) : addr(a), topic(t) {}

    ~Impl() {
        stop();
    }

    void stop() {
        bool expected = true;
        if (running.compare_exchange_strong(expected, false)) {
            if (thread.joinable()) thread.join();
        }
    }

    // Parse single-frame message of format "<topic> <json>"
    static bool parse_message(zmq::message_t& msg, std::string& topic_out, std::string& json_str) {
        std::string full_msg(static_cast<char*>(msg.data()), msg.size());
        size_t space_pos = full_msg.find(' ');
        if (space_pos == std::string::npos) return false;
        topic_out = full_msg.substr(0, space_pos);
        json_str = full_msg.substr(space_pos + 1);
        return true;
    }

    void process_message(const std::string& json_str) {
        try {
            json data = json::parse(json_str);

            if (!data.contains("ts") || !data.contains("source") || !data.contains("regimes")) {
                std::cerr << "Error: Missing required top-level fields (ts, source, or regimes)" << std::endl;
                return;
            }

            std::string message_ts = data["ts"];
            std::string source = data["source"];

            std::cout << "[" << message_ts << "] Update from " << source << std::endl;

            const json& regimes = data["regimes"];
            if (!regimes.is_object()) {
                std::cerr << "Error: 'regimes' field is not an object" << std::endl;
                return;
            }

            for (auto& [symbol, regime_data] : regimes.items()) {
                RegimeInfo info;
                info.valid = true;

                if (!regime_data.is_object()) {
                    std::cerr << "Warning: Regime data for " << symbol << " is not an object, skipping" << std::endl;
                    continue;
                }

                // regime
                if (regime_data.contains("regime")) {
                    if (regime_data["regime"].is_null()) {
                        info.regime = -1;
                    } else if (regime_data["regime"].is_number_integer()) {
                        info.regime = regime_data["regime"].get<int>();
                    } else {
                        info.regime = -1;
                        info.error = "Invalid regime field";
                    }
                } else {
                    info.regime = -1;
                    info.error = "Missing regime field";
                }

                // probs
                if (regime_data.contains("probs")) {
                    if (regime_data["probs"].is_null()) {
                        info.probs = {};
                    } else if (regime_data["probs"].is_array()) {
                        for (const auto& p : regime_data["probs"]) {
                            if (p.is_number()) info.probs.push_back(p.get<double>());
                        }
                    } else {
                        if (info.error.empty()) info.error = "Invalid probs field";
                    }
                } else {
                    if (info.error.empty()) info.error = "Missing probs field";
                }

                if (regime_data.contains("last_ts") && !regime_data["last_ts"].is_null()) {
                    if (regime_data["last_ts"].is_string()) info.last_ts = regime_data["last_ts"].get<std::string>();
                }

                if (regime_data.contains("error") && regime_data["error"].is_string()) {
                    info.error = regime_data["error"].get<std::string>();
                }

                // --- optional diagnostics from regime_publisher.py ---
                if (regime_data.contains("recommended_action") && regime_data["recommended_action"].is_string()) {
                    info.recommended_action = regime_data["recommended_action"].get<std::string>();
                }

                auto parse_optional_double = [&](const json& o) -> std::optional<double> {
                    if (o.is_number()) return o.get<double>();
                    if (o.is_string()) {
                        try {
                            return std::stod(o.get<std::string>());
                        } catch (...) { return std::nullopt; }
                    }
                    return std::nullopt;
                };

                if (regime_data.contains("short_alpha")) {
                    info.short_alpha = parse_optional_double(regime_data["short_alpha"]);
                }
                if (regime_data.contains("p_trend_up")) {
                    info.p_trend_up = parse_optional_double(regime_data["p_trend_up"]);
                }
                if (regime_data.contains("stay_prob")) {
                    info.stay_prob = parse_optional_double(regime_data["stay_prob"]);
                }
                if (regime_data.contains("mixture_half_life_min")) {
                    info.mixture_half_life_min = parse_optional_double(regime_data["mixture_half_life_min"]);
                }

                if (regime_data.contains("micro_ok")) {
                    if (regime_data["micro_ok"].is_boolean()) {
                        info.micro_ok = regime_data["micro_ok"].get<bool>();
                    } else if (regime_data["micro_ok"].is_number_integer()) {
                        info.micro_ok = (regime_data["micro_ok"].get<int>() != 0);
                    }
                }

                // reason field (publisher may call it 'reason')
                if (regime_data.contains("reason") && regime_data["reason"].is_string()) {
                    info.recommendation_reason = regime_data["reason"].get<std::string>();
                } else if (regime_data.contains("recommendation_reason") && regime_data["recommendation_reason"].is_string()) {
                    info.recommendation_reason = regime_data["recommendation_reason"].get<std::string>();
                }

                if (regime_data.contains("suggested_time_cap_min")) {
                    if (regime_data["suggested_time_cap_min"].is_number_integer()) {
                        info.suggested_time_cap_min = regime_data["suggested_time_cap_min"].get<int>();
                    } else if (regime_data["suggested_time_cap_min"].is_string()) {
                        try { info.suggested_time_cap_min = std::stoi(regime_data["suggested_time_cap_min"].get<std::string>()); } catch (...) {}
                    }
                }

                {
                    std::lock_guard<std::mutex> lk(mtx);
                    auto it = store.find(symbol);
                    bool changed = false;
                    if (it != store.end()) {
                        if (it->second.regime != info.regime) changed = true;
                    }
                    store[symbol] = info;
                    if (changed) {
                        std::cout << ">>> REGIME CHANGE: " << symbol << " -> " << info.regime << std::endl;
                    }
                }

                // Compact diagnostics log (one-line) - do not change behavior, log-only
                std::ostringstream diag;
                diag << "  " << symbol << ": regime=" << info.regime << ", probs_count=" << info.probs.size();
                if (!info.error.empty()) diag << ", error=" << info.error;
                if (!info.recommended_action.empty()) diag << ", recommended=" << info.recommended_action;
                if (info.stay_prob.has_value()) diag << ", stay_prob=" << std::fixed << std::setprecision(3) << info.stay_prob.value();
                if (info.p_trend_up.has_value()) diag << ", p_trend_up=" << std::fixed << std::setprecision(3) << info.p_trend_up.value();
                if (info.short_alpha.has_value()) diag << ", short_alpha=" << std::fixed << std::setprecision(4) << info.short_alpha.value();
                if (info.mixture_half_life_min.has_value()) diag << ", hl_min=" << std::fixed << std::setprecision(1) << info.mixture_half_life_min.value();
                if (info.micro_ok.has_value()) diag << ", micro_ok=" << (info.micro_ok.value() ? "true" : "false");
                if (!info.recommendation_reason.empty()) diag << ", reason=" << info.recommendation_reason;
                if (info.suggested_time_cap_min.has_value()) diag << ", cap_min=" << info.suggested_time_cap_min.value();
                std::cout << diag.str() << std::endl;
            }

        } catch (const json::parse_error& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error processing message: " << e.what() << std::endl;
        }
    }

    void run() {
        zmq::context_t context(1);
        zmq::socket_t subscriber(context, zmq::socket_type::sub);
        int linger = 0;
        subscriber.set(zmq::sockopt::linger, linger);

        try {
            std::cout << "Connecting to " << addr << std::endl;
            subscriber.connect(addr);
            std::cout << "Subscribing to topic: " << topic << std::endl;
            subscriber.set(zmq::sockopt::subscribe, topic);
        } catch (const std::exception& e) {
            std::cerr << "Failed to connect/subscribe: " << e.what() << std::endl;
            return;
        }

        zmq::pollitem_t items[] = {{subscriber, 0, ZMQ_POLLIN, 0}};

        while (running.load()) {
            try {
                zmq::poll(items, 1, std::chrono::milliseconds(100));

                if (items[0].revents & ZMQ_POLLIN) {
                    zmq::message_t message;
                    if (!subscriber.recv(message, zmq::recv_flags::none)) continue;

                    std::string received_topic;
                    std::string json_str;

                    bool more = subscriber.get(zmq::sockopt::rcvmore);
                    if (more) {
                        received_topic = std::string(static_cast<char*>(message.data()), message.size());
                        zmq::message_t json_message;
                        subscriber.recv(json_message, zmq::recv_flags::none);
                        json_str = std::string(static_cast<char*>(json_message.data()), json_message.size());
                    } else {
                        if (!parse_message(message, received_topic, json_str)) continue;
                    }

                    process_message(json_str);
                }

            } catch (const std::exception& e) {
                std::cerr << "Subscriber run loop error: " << e.what() << std::endl;
            }
        }

        try {
            subscriber.close();
            context.close();
        } catch (...) {}
    }
};

// Public RegimeSubscriber methods
RegimeSubscriber::RegimeSubscriber(const std::string& addr, const std::string& topic) {
    impl_ = new Impl(addr, topic);
}

RegimeSubscriber::~RegimeSubscriber() {
    if (impl_) delete impl_;
}

void RegimeSubscriber::start() {
    if (!impl_) return;
    bool expected = false;
    if (impl_->running.compare_exchange_strong(expected, true)) {
        impl_->thread = std::thread([this]() { impl_->run(); });
    }
}

void RegimeSubscriber::stop() {
    if (!impl_) return;
    impl_->stop();
}

bool RegimeSubscriber::running() const {
    if (!impl_) return false;
    return impl_->running.load();
}

bool RegimeSubscriber::get(const std::string& symbol, RegimeInfo& out) const {
    if (!impl_) return false;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    auto it = impl_->store.find(symbol);
    if (it == impl_->store.end()) return false;
    out = it->second;
    return true;
}

void RegimeSubscriber::dump() const {
    if (!impl_) return;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    std::cout << "\n=== REGIME STORE DUMP ===" << std::endl;
    std::cout << "Total symbols: " << impl_->store.size() << std::endl;
    for (const auto& kv : impl_->store) {
        const auto& symbol = kv.first;
        const auto& info = kv.second;
        std::cout << "\nSymbol: " << symbol << std::endl;
        std::cout << "  Regime: " << info.regime << std::endl;
        std::cout << "  Probs: [";
        for (size_t i = 0; i < info.probs.size(); ++i) {
            std::cout << std::fixed << std::setprecision(3) << info.probs[i];
            if (i + 1 < info.probs.size()) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
        std::cout << "  Last TS: " << info.last_ts << std::endl;
    if (!info.error.empty()) std::cout << "  Error: " << info.error << std::endl;
    if (!info.recommended_action.empty()) std::cout << "  Recommended: " << info.recommended_action << std::endl;
    if (info.stay_prob.has_value()) std::cout << "  StayProb: " << std::fixed << std::setprecision(3) << info.stay_prob.value() << std::endl;
    if (info.p_trend_up.has_value()) std::cout << "  PTrendUp: " << std::fixed << std::setprecision(3) << info.p_trend_up.value() << std::endl;
    if (info.short_alpha.has_value()) std::cout << "  ShortAlpha: " << std::fixed << std::setprecision(4) << info.short_alpha.value() << std::endl;
    if (info.mixture_half_life_min.has_value()) std::cout << "  MixtureHalfLifeMin: " << std::fixed << std::setprecision(1) << info.mixture_half_life_min.value() << std::endl;
    if (info.micro_ok.has_value()) std::cout << "  MicroOK: " << (info.micro_ok.value() ? "true" : "false") << std::endl;
    if (!info.recommendation_reason.empty()) std::cout << "  Reason: " << info.recommendation_reason << std::endl;
    if (info.suggested_time_cap_min.has_value()) std::cout << "  SuggestedCapMin: " << info.suggested_time_cap_min.value() << std::endl;
    }
    std::cout << "========================\n" << std::endl;
}