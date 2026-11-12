#include "hd/json_writer.h"

#include <stdio.h>

namespace fallout::hd {

void JsonWriter::separate()
{
    if (_afterKey) {
        _afterKey = false;
        return;
    }

    if (!_hasMembers.empty()) {
        if (_hasMembers.back()) {
            _out += ',';
        }
        _hasMembers.back() = true;
    }
}

JsonWriter& JsonWriter::beginObject()
{
    separate();
    _out += '{';
    _hasMembers.push_back(false);
    return *this;
}

JsonWriter& JsonWriter::endObject()
{
    _hasMembers.pop_back();
    _out += '}';
    return *this;
}

JsonWriter& JsonWriter::beginArray()
{
    separate();
    _out += '[';
    _hasMembers.push_back(false);
    return *this;
}

JsonWriter& JsonWriter::endArray()
{
    _hasMembers.pop_back();
    _out += ']';
    return *this;
}

JsonWriter& JsonWriter::key(const char* name)
{
    separate();
    _out += '"';
    _out += name;
    _out += "\":";
    _afterKey = true;
    return *this;
}

JsonWriter& JsonWriter::value(int v)
{
    separate();
    _out += std::to_string(v);
    return *this;
}

JsonWriter& JsonWriter::value(unsigned int v)
{
    separate();
    _out += std::to_string(v);
    return *this;
}

JsonWriter& JsonWriter::value(bool v)
{
    separate();
    _out += v ? "true" : "false";
    return *this;
}

JsonWriter& JsonWriter::value(const char* v)
{
    separate();
    if (v == nullptr) {
        _out += "null";
        return *this;
    }

    _out += '"';
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(v); *p != '\0'; p++) {
        switch (*p) {
        case '"':
            _out += "\\\"";
            break;
        case '\\':
            _out += "\\\\";
            break;
        case '\n':
            _out += "\\n";
            break;
        case '\r':
            _out += "\\r";
            break;
        case '\t':
            _out += "\\t";
            break;
        default:
            if (*p < 0x20 || *p >= 0x80) {
                // Game strings are code page 1252; keep them lossless as \u00XX.
                char escaped[8];
                snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
                _out += escaped;
            } else {
                _out += static_cast<char>(*p);
            }
            break;
        }
    }
    _out += '"';
    return *this;
}

JsonWriter& JsonWriter::base64(const void* data, size_t size)
{
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    separate();
    _out += '"';

    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    size_t index = 0;
    for (; index + 2 < size; index += 3) {
        unsigned int chunk = (bytes[index] << 16) | (bytes[index + 1] << 8) | bytes[index + 2];
        _out += kAlphabet[(chunk >> 18) & 0x3F];
        _out += kAlphabet[(chunk >> 12) & 0x3F];
        _out += kAlphabet[(chunk >> 6) & 0x3F];
        _out += kAlphabet[chunk & 0x3F];
    }

    size_t remaining = size - index;
    if (remaining != 0) {
        unsigned int chunk = bytes[index] << 16;
        if (remaining == 2) {
            chunk |= bytes[index + 1] << 8;
        }
        _out += kAlphabet[(chunk >> 18) & 0x3F];
        _out += kAlphabet[(chunk >> 12) & 0x3F];
        _out += remaining == 2 ? kAlphabet[(chunk >> 6) & 0x3F] : '=';
        _out += '=';
    }

    _out += '"';
    return *this;
}

JsonWriter& JsonWriter::rect(int left, int top, int right, int bottom)
{
    beginArray();
    value(left);
    value(top);
    value(right);
    value(bottom);
    return endArray();
}

JsonWriter& JsonWriter::newline()
{
    _out += '\n';
    return *this;
}

} // namespace fallout::hd
