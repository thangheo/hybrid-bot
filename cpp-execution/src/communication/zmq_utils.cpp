#include "communication/zmq_server_client.hpp"
#include <regex>
#include <sstream>

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
    
    // Convert wildcard pattern to regex
    std::string regex_pattern = pattern;
    
    // Escape special regex characters except * and ?
    std::string special_chars = "()[]{}+.^$|\\";
    for (char c : special_chars) {
        std::string char_str(1, c);
        std::string escaped_char = "\\" + char_str;
        
        size_t pos = 0;
        while ((pos = regex_pattern.find(char_str, pos)) != std::string::npos) {
            regex_pattern.replace(pos, 1, escaped_char);
            pos += escaped_char.length();
        }
    }
    
    // Replace wildcards with regex equivalents
    size_t pos = 0;
    while ((pos = regex_pattern.find('*', pos)) != std::string::npos) {
        regex_pattern.replace(pos, 1, ".*");
        pos += 2;
    }
    
    pos = 0;
    while ((pos = regex_pattern.find('?', pos)) != std::string::npos) {
        regex_pattern.replace(pos, 1, ".");
        pos += 1;
    }
    
    try {
        std::regex pattern_regex("^" + regex_pattern + "$");
        return std::regex_match(topic, pattern_regex);
    } catch (const std::regex_error& e) {
        // If regex compilation fails, fall back to exact match
        return topic == pattern;
    }
}

} // namespace trading::communication