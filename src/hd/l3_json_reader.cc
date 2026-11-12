#include "hd/json_reader.h"

#include <stdlib.h>
#include <string.h>

namespace fallout::hd {

namespace {

    const JsonValue kNull;

    struct Parser {
        const char* p;
        const char* end;
        std::string error;

        void skipSpace()
        {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
                p++;
            }
        }

        bool fail(const char* what)
        {
            if (error.empty()) {
                error = what;
            }
            return false;
        }

        bool literal(const char* word)
        {
            size_t n = strlen(word);
            if (static_cast<size_t>(end - p) < n || memcmp(p, word, n) != 0) {
                return fail("bad literal");
            }
            p += n;
            return true;
        }

        bool string(std::string& out)
        {
            p++; // opening quote
            out.clear();
            while (p < end && *p != '"') {
                char c = *p++;
                if (c == '\\') {
                    if (p >= end) {
                        return fail("bad escape");
                    }
                    char e = *p++;
                    switch (e) {
                    case 'n':
                        c = '\n';
                        break;
                    case 't':
                        c = '\t';
                        break;
                    case 'r':
                        c = '\r';
                        break;
                    case 'b':
                        c = '\b';
                        break;
                    case 'f':
                        c = '\f';
                        break;
                    case 'u': {
                        if (end - p < 4) {
                            return fail("bad \\u escape");
                        }
                        char hex[5] = { p[0], p[1], p[2], p[3], 0 };
                        c = static_cast<char>(strtol(hex, nullptr, 16) & 0x7F);
                        p += 4;
                        break;
                    }
                    default:
                        c = e; // \" \\ \/
                        break;
                    }
                }
                out.push_back(c);
            }
            if (p >= end) {
                return fail("unterminated string");
            }
            p++; // closing quote
            return true;
        }

        bool value(JsonValue& out, int depth)
        {
            if (depth > 64) {
                return fail("nested too deep");
            }
            skipSpace();
            if (p >= end) {
                return fail("unexpected end");
            }
            switch (*p) {
            case '{': {
                out.type = JsonValue::Type::Object;
                p++;
                skipSpace();
                if (p < end && *p == '}') {
                    p++;
                    return true;
                }
                while (true) {
                    skipSpace();
                    if (p >= end || *p != '"') {
                        return fail("expected key");
                    }
                    out.members.emplace_back();
                    if (!string(out.members.back().first)) {
                        return false;
                    }
                    skipSpace();
                    if (p >= end || *p != ':') {
                        return fail("expected ':'");
                    }
                    p++;
                    if (!value(out.members.back().second, depth + 1)) {
                        return false;
                    }
                    skipSpace();
                    if (p < end && *p == ',') {
                        p++;
                        continue;
                    }
                    if (p < end && *p == '}') {
                        p++;
                        return true;
                    }
                    return fail("expected ',' or '}'");
                }
            }
            case '[': {
                out.type = JsonValue::Type::Array;
                p++;
                skipSpace();
                if (p < end && *p == ']') {
                    p++;
                    return true;
                }
                while (true) {
                    out.items.emplace_back();
                    if (!value(out.items.back(), depth + 1)) {
                        return false;
                    }
                    skipSpace();
                    if (p < end && *p == ',') {
                        p++;
                        continue;
                    }
                    if (p < end && *p == ']') {
                        p++;
                        return true;
                    }
                    return fail("expected ',' or ']'");
                }
            }
            case '"':
                out.type = JsonValue::Type::String;
                return string(out.string);
            case 't':
                out.type = JsonValue::Type::Bool;
                out.boolean = true;
                return literal("true");
            case 'f':
                out.type = JsonValue::Type::Bool;
                out.boolean = false;
                return literal("false");
            case 'n':
                out.type = JsonValue::Type::Null;
                return literal("null");
            default: {
                // strtod needs a terminated buffer; numbers are short.
                char buffer[64];
                size_t n = 0;
                while (p + n < end && n < sizeof(buffer) - 1 && strchr("+-0123456789.eE", p[n]) != nullptr) {
                    buffer[n] = p[n];
                    n++;
                }
                if (n == 0) {
                    return fail("unexpected character");
                }
                buffer[n] = '\0';
                out.type = JsonValue::Type::Number;
                out.number = strtod(buffer, nullptr);
                p += n;
                return true;
            }
            }
        }
    };

} // namespace

const JsonValue& JsonValue::operator[](const char* key) const
{
    if (type == Type::Object) {
        for (const auto& member : members) {
            if (member.first == key) {
                return member.second;
            }
        }
    }
    return kNull;
}

const JsonValue& JsonValue::operator[](size_t index) const
{
    return type == Type::Array && index < items.size() ? items[index] : kNull;
}

bool JsonValue::has(const char* key) const
{
    return &(*this)[key] != &kNull;
}

bool jsonParse(const char* text, size_t length, JsonValue& out, std::string* error)
{
    Parser parser = { text, text + length, {} };
    out = JsonValue();
    bool ok = parser.value(out, 0);
    if (ok) {
        parser.skipSpace();
        if (parser.p != parser.end) {
            ok = parser.fail("trailing characters");
        }
    }
    if (!ok && error != nullptr) {
        *error = parser.error + " at offset " + std::to_string(parser.p - text);
    }
    return ok;
}

} // namespace fallout::hd
