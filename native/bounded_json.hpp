#pragma once
#include <string>

namespace moader_ui {
inline bool bounded_json(const std::string& json, size_t limit = 1024 * 1024) {
    if (json.size() > limit)
        return false;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (char c : json) {
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 24)
                return false;
        } else if (c == '}' || c == ']') {
            if (depth == 0)
                return false;
            --depth;
        }
    }
    return !quoted && depth == 0;
}
} // namespace moader_ui
