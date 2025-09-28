#include "communication/zmq_server_client.hpp"
#include <regex>
#include <sstream>
#include <iostream>

namespace trading::communication {
std::string generateCorrelationId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) {
            ss << '-';
        }
        ss << std::hex << dis(gen);
    }
    
    return ss.str();
}

bool isValidTopic(const std::string& topic) {
    if (topic.empty() || topic.length() > 255) {
        return false;
    }
    
    // Check for invalid characters (control characters, spaces)
    for (char c : topic) {
        if (c <= 32 || c >= 127) {
            return false;
        }
    }
    
    return true;
}

std::vector<std::string> splitTopic(const std::string& topic) {
    std::vector<std::string> parts;
    std::stringstream ss(topic);
    std::string part;
    
    while (std::getline(ss, part, '.')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    
    return parts;
}

bool matchesPattern(const std::string& topic, const std::string& pattern) {
    // Simple wildcard pattern matching
    // * matches any sequence of characters
    // ? matches any single character
    
    if (pattern == "*") {
        return true;
    }
    
    if (pattern.find('*') == std::string::npos && pattern.find('?') == std::string::npos) {
        return topic == pattern;
    }
    
    // Convert wildcard pattern to regex using a single-pass builder to avoid
    // double-escaping issues (especially with backslashes).
    std::string regex_pattern;
    regex_pattern.reserve(pattern.size() * 2);

    // Regex metacharacters that must be escaped
    const std::string regex_meta = R"(^.\$|()[]{}+?)"; // note: we'll handle '*' and '?' specially

    for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];
        if (c == '*') {
            regex_pattern.append(".*");
        } else if (c == '?') {
            regex_pattern.append(".");
        } else {
            // Escape if metacharacter for regex
            if (c == '^' || c == '.' || c == '\\' || c == '$' || c == '|' || c == '(' || c == ')' ||
                c == '[' || c == ']' || c == '{' || c == '}' || c == '+' ) {
                regex_pattern.push_back('\\');
            }
            regex_pattern.push_back(c);
        }
    }
    
    try {
        std::string full_regex = "^" + regex_pattern + "$";
        std::regex pattern_regex(full_regex);
        bool res = std::regex_match(topic, pattern_regex);
        std::cout << "[ZMQ Utils] pattern='" << pattern << "' -> regex='" << full_regex << "' topic='" << topic << "' -> " << (res ? "MATCH" : "NO") << std::endl;
        return res;
    } catch (const std::regex_error& e) {
        // If regex compilation fails, fall back to exact match
        return topic == pattern;
    }
}

} // namespace trading::communication