// Typing the target PC by hand (the Basic tab's "Enter IP"): checking what was typed, and the on-panel keypad's
// editing. Nothing here draws or talks to OpenVR, so it is tested on its own (text_test.cpp).
#pragma once

#include <string>

namespace host_entry {

/** What is wrong with a typed host. */
enum class HostError {
    None,   ///< an IPv4 address or a host name frameeyeosc accepts
    Empty,  ///< nothing typed
    Space,  ///< it contains a space
    Port,   ///< it contains ":" (a port goes in the Port row)
    Ipv4,   ///< only digits and dots, but not four numbers 0-255
    Name,   ///< a host name with other characters, or an empty / too long part
};

/** The keypad's backspace key (the others are '0'-'9' and '.'). */
constexpr int kBackspace = 8;
/** The keypad takes at most this many characters ("255.255.255.255"). */
constexpr size_t kKeypadMax = 15;
/** A host name is at most this long. */
constexpr size_t kNameMax = 253;

/**
 * Check a typed host: an IPv4 address, or a host name (letters, digits, "-" and "." only; each part 1-63 long and
 * not starting or ending with "-").
 * @param text what was typed
 * @return what is wrong, or HostError::None
 * @example
 * checkHost("192.168.1.20") // HostError::None
 * checkHost("192.168.1.20:9000") // HostError::Port
 */
HostError checkHost(const std::string& text);

/**
 * One keypad press.
 * @param text the text so far
 * @param key '0'-'9', '.' or kBackspace
 * @return the new text (unchanged if the key does not fit)
 */
std::string keypadInput(const std::string& text, int key);

}  // namespace host_entry
