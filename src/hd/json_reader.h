#ifndef FALLOUT_HD_JSON_READER_H_
#define FALLOUT_HD_JSON_READER_H_

#include <stddef.h>

#include <string>
#include <utility>
#include <vector>

namespace fallout::hd {

// Minimal JSON reader for pipeline output (pack.json). Whole document in
// memory; numbers are doubles; no \u escapes beyond ASCII.
struct JsonValue {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> items; // Array
    std::vector<std::pair<std::string, JsonValue>> members; // Object

    // Member lookup; returns a Null value if missing or not an object.
    const JsonValue& operator[](const char* key) const;
    // Array element; Null value if out of range or not an array.
    const JsonValue& operator[](size_t index) const;
    size_t size() const { return type == Type::Array ? items.size() : 0; }
    bool has(const char* key) const;
    int asInt(int fallback = 0) const { return type == Type::Number ? static_cast<int>(number) : fallback; }
    unsigned int asUint() const { return type == Type::Number ? static_cast<unsigned int>(number) : 0u; }
};

// Parses `text`; false (and `error` set) on malformed input.
bool jsonParse(const char* text, size_t length, JsonValue& out, std::string* error = nullptr);

} // namespace fallout::hd

#endif /* FALLOUT_HD_JSON_READER_H_ */
