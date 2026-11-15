#include "moader/model.hpp"
#include <cctype>
#include <stdexcept>

namespace moader {
std::string preferences_json(const Preferences& p) {
    return std::string("{\"gameNames\":") + (p.game_names ? "true" : "false") +
        ",\"reducedMotion\":" + (p.reduced_motion ? "true" : "false") +
        ",\"webOnStart\":" + (p.web_on_start ? "true" : "false") +
        ",\"webPort\":" + std::to_string(p.web_port) +
        ",\"followLog\":" + (p.follow_log ? "true" : "false") + '}';
}
bool valid_title_id(const std::string& value) {
    if (value.size() != 16 || (value.compare(0, 2, "01") && value.compare(0, 2, "04"))) return false;
    for (unsigned char c : value) if (!std::isxdigit(c)) return false;
    return true;
}
bool valid_mod_id(const std::string& value) {
    if (value.empty() || value.size() > 48 || value == "." || value == "..") return false;
    for (unsigned char c : value)
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-' && c != '_') return false;
    return true;
}
std::string checked_relative_path(const std::string& value) {
    if (value.empty() || value.size() > 220 || value.front() == '/' || value.back() == '/')
        throw std::invalid_argument("Use a relative file path of at most 220 bytes.");
    size_t start = 0;
    for (size_t i = 0; i <= value.size(); ++i) {
        if (i < value.size()) {
            const unsigned char c = value[i];
            if (c < 32 || c == 127 || c == '\\' || c == ':' || c == 0)
                throw std::invalid_argument("The file path contains an unsupported character.");
            if (c != '/') continue;
        }
        const auto segment = value.substr(start, i - start);
        if (segment.empty() || segment == "." || segment == ".." ||
            segment.back() == ' ' || segment.back() == '.')
            throw std::invalid_argument("The file path contains an unsafe directory segment.");
        start = i + 1;
    }
    return value;
}
std::string json_string(const std::string& value) {
    std::string out = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += c;
    }
    return out + '"';
}
const char* apply_state_name(ApplyState value) {
    switch (value) {
    case ApplyState::Disabled: return "disabled";
    case ApplyState::Pending: return "pending";
    case ApplyState::Active: return "active";
    case ApplyState::ReopenContent: return "reopen-content";
    case ApplyState::RelaunchGame: return "relaunch-game";
    case ApplyState::Error: return "error";
    }
    return "error";
}
}
