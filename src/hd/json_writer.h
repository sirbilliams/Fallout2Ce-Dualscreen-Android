#ifndef FALLOUT_HD_JSON_WRITER_H_
#define FALLOUT_HD_JSON_WRITER_H_

#include <stddef.h>

#include <string>
#include <vector>

namespace fallout::hd {

// Minimal streaming JSON writer for capture files. Commas between members and
// elements are inserted automatically; keys must be plain ASCII.
class JsonWriter {
public:
    JsonWriter& beginObject();
    JsonWriter& endObject();
    JsonWriter& beginArray();
    JsonWriter& endArray();

    JsonWriter& key(const char* name);
    JsonWriter& value(int v);
    JsonWriter& value(unsigned int v);
    JsonWriter& value(bool v);
    JsonWriter& value(const char* v);
    JsonWriter& value(const std::string& v) { return value(v.c_str()); }

    // Binary data as a base64 string.
    JsonWriter& base64(const void* data, size_t size);

    // [left, top, right, bottom], inclusive like CE's Rect.
    JsonWriter& rect(int left, int top, int right, int bottom);

    template <typename T>
    JsonWriter& member(const char* name, const T& v) { return key(name).value(v); }

    // Starts a new line before the next value (keeps draw lists readable).
    JsonWriter& newline();

    const std::string& str() const { return _out; }

private:
    void separate();

    std::string _out;
    std::vector<bool> _hasMembers;
    bool _afterKey = false;
};

} // namespace fallout::hd

#endif /* FALLOUT_HD_JSON_WRITER_H_ */
