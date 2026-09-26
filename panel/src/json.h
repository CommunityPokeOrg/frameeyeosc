// A small JSON parser and writer for the config and status files.
#pragma once

#include <string>
#include <utility>
#include <vector>

/**
 * A single JSON value. Objects keep their members in an ordered vector of pairs.
 */
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    bool integer = false;  ///< the number was written without a fraction or exponent (written back the same way)
    std::string text;
    std::vector<JsonValue> items;                              ///< elements of an Array
    std::vector<std::pair<std::string, JsonValue>> members;    ///< members of an Object

    /**
     * Look up a value by key in an object.
     * @param key the key to find
     * @return pointer to the value found, or nullptr if not an object or the key is missing
     */
    const JsonValue* get(const std::string& key) const;

    /**
     * Set a member of an object, replacing it in place or appending it at the end (keeps the other members' order).
     * @param key the key
     * @param value the new value
     */
    void set(const std::string& key, JsonValue value);

    /**
     * Make a number value.
     * @param value the number
     * @param isInteger write it without a fraction (for counts and ports)
     * @return the value
     */
    static JsonValue makeNumber(double value, bool isInteger = false);

    /**
     * Make a boolean value.
     * @param value the boolean
     * @return the value
     */
    static JsonValue makeBool(bool value);

    /**
     * Make a string value.
     * @param value the string (UTF-8)
     * @return the value
     */
    static JsonValue makeString(const std::string& value);

    /** @return a null value */
    static JsonValue makeNull() { return JsonValue(); }

    /** @return true if a number */
    bool isNumber() const { return type == Type::Number; }
    /** @return true if a boolean */
    bool isBool() const { return type == Type::Bool; }
    /** @return true if a string */
    bool isString() const { return type == Type::String; }
    /** @return true if an object */
    bool isObject() const { return type == Type::Object; }
    /** @return true if null */
    bool isNull() const { return type == Type::Null; }
    /** @return true if an array */
    bool isArray() const { return type == Type::Array; }
};

/**
 * Parse a JSON string.
 * @param source the string to parse (UTF-8)
 * @param out destination for the parsed result
 * @param error reason for failure (includes the character position)
 * @return true on success
 */
bool parseJson(const std::string& source, JsonValue& out, std::string& error);

/**
 * Write a value as pretty-printed JSON (2-space indent, members in their stored order, trailing newline).
 * Fractional numbers are written with up to 6 significant digits so that 0.1 steps stay readable.
 * @param value the value to write
 * @return the JSON text
 */
std::string writeJson(const JsonValue& value);
