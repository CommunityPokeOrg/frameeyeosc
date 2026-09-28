// Typing the target PC by hand.
#include "host_entry.h"

#include <cctype>

namespace host_entry {

namespace {

/**
 * Whether text is four numbers 0-255 separated by dots.
 * @param text digits and dots only
 * @return true if an IPv4 address
 */
bool isIpv4(const std::string& text) {
    int parts = 0;
    size_t start = 0;
    while (true) {
        const size_t dot = text.find('.', start);
        const std::string part = text.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (part.empty() || part.size() > 3 || std::stoi(part) > 255) return false;
        ++parts;
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return parts == 4;
}

/**
 * Whether text is a host name: parts of letters, digits and "-", 1-63 long, not starting or ending with "-".
 * @param text the name
 * @return true if usable
 */
bool isName(const std::string& text) {
    if (text.size() > kNameMax) return false;
    size_t start = 0;
    while (true) {
        const size_t dot = text.find('.', start);
        const std::string part = text.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (part.empty() || part.size() > 63 || part.front() == '-' || part.back() == '-') return false;
        for (const char c : part) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') return false;
        }
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return true;
}

}  // namespace

HostError checkHost(const std::string& text) {
    if (text.empty()) return HostError::Empty;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) return HostError::Space;
    }
    if (text.find(':') != std::string::npos) return HostError::Port;
    bool numeric = true;
    for (const char c : text) numeric &= std::isdigit(static_cast<unsigned char>(c)) || c == '.';
    if (numeric) return isIpv4(text) ? HostError::None : HostError::Ipv4;
    return isName(text) ? HostError::None : HostError::Name;
}

std::string keypadInput(const std::string& text, int key) {
    if (key == kBackspace) return text.empty() ? text : text.substr(0, text.size() - 1);
    const bool digit = key >= '0' && key <= '9';
    if ((!digit && key != '.') || text.size() >= kKeypadMax) return text;
    return text + static_cast<char>(key);
}

}  // namespace host_entry
