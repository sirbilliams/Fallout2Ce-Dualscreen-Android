// No game files: exercise the production save readers against synthetic bytes.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "db.h"
#include "sfall_arrays.h"
#include "sfall_ext.h"
#include "sfall_global_vars.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #condition);     \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (false)

namespace {
struct Buffer {
    std::vector<unsigned char> bytes;
    size_t position = 0;
    fallout::File* file() { return reinterpret_cast<fallout::File*>(this); }
    void word(unsigned int value)
    {
        for (int i = 0; i < 4; i++) bytes.push_back((value >> (8 * i)) & 0xFF);
    }
    void number(unsigned int type, unsigned int bits) { word(type); word(bits); }
    void string(const std::string& text)
    {
        word(3);
        word(text.size() + 1);
        bytes.insert(bytes.end(), text.begin(), text.end());
        bytes.push_back(0);
    }
};
std::vector<std::string> strings;
int restoredCounter;
}

// Minimal file/VM adapters: the serialization and array implementation below
// are the actual engine sources. The byte stream lives entirely in memory.
namespace fallout {
size_t fileRead(void* out, size_t size, size_t count, File* file)
{
    auto& buffer = *reinterpret_cast<Buffer*>(file);
    size_t available = (buffer.bytes.size() - buffer.position) / size;
    count = std::min(count, available);
    memcpy(out, buffer.bytes.data() + buffer.position, size * count);
    buffer.position += size * count;
    return count;
}
size_t fileWrite(const void* in, size_t size, size_t count, File* file)
{
    auto& buffer = *reinterpret_cast<Buffer*>(file);
    const auto* bytes = static_cast<const unsigned char*>(in);
    buffer.bytes.insert(buffer.bytes.end(), bytes, bytes + size * count);
    buffer.position = buffer.bytes.size();
    return count;
}
long fileTell(File* file) { return reinterpret_cast<Buffer*>(file)->position; }
int fileGetSize(File* file) { return reinterpret_cast<Buffer*>(file)->bytes.size(); }
int fileSeek(File* file, long offset, int origin)
{
    auto& buffer = *reinterpret_cast<Buffer*>(file);
    long position = origin == SEEK_CUR ? buffer.position + offset : offset;
    if (position < 0 || static_cast<size_t>(position) > buffer.bytes.size()) return -1;
    buffer.position = position;
    return 0;
}
int fileReadInt32(File* file, int* value)
{
    unsigned char bytes[4];
    if (fileRead(bytes, 1, 4, file) != 4) return -1;
    *value = (unsigned(bytes[0]) << 24) | (unsigned(bytes[1]) << 16) | (unsigned(bytes[2]) << 8) | bytes[3];
    return 0;
}
int fileWriteInt32(File* file, int value)
{
    unsigned char bytes[4] = { static_cast<unsigned char>(value >> 24), static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value) };
    return fileWrite(bytes, 1, 4, file) == 4 ? 0 : -1;
}
int debugPrint(const char*, ...) { return 0; }
int scriptsGetUniqueObjectIdCounter() { return restoredCounter; }
void scriptsRestoreUniqueObjectIdCounter(int value) { restoredCounter = value; }
ProgramValue::ProgramValue() : opcode(VALUE_TYPE_INT), integerValue(0) { }
ProgramValue::ProgramValue(int value) : opcode(VALUE_TYPE_INT), integerValue(value) { }
ProgramValue::ProgramValue(unsigned int value) : ProgramValue(static_cast<int>(value)) { }
ProgramValue::ProgramValue(float value) : opcode(VALUE_TYPE_FLOAT), floatValue(value) { }
bool ProgramValue::isInt() const { return opcode == VALUE_TYPE_INT; }
bool ProgramValue::isPointer() const { return opcode == VALUE_TYPE_PTR; }
int ProgramValue::asInt() const { return opcode == VALUE_TYPE_INT ? integerValue : opcode == VALUE_TYPE_FLOAT ? static_cast<int>(floatValue) : 0; }
char* programGetString(Program*, opcode_t, int value) { return const_cast<char*>(strings.at(value).c_str()); }
int programPushString(Program*, const char* text) { strings.emplace_back(text); return strings.size() - 1; }
}

using namespace fallout;

static ProgramValue text(const char* string)
{
    ProgramValue value(programPushString(nullptr, string));
    value.opcode = VALUE_TYPE_DYNAMIC_STRING;
    return value;
}

static void reset()
{
    sfallArraysReset();
    sfall_gl_vars_reset();
}

// Real sfall SaveGame2/SaveArrays wire layout (all DWORDs little endian):
// globals: count + {uint64 key, int32 value, int32 padding}; object counter;
// addedYears; three {count, packed 1096-byte FakePerk[count]} sections;
// arrays: old-format count (zero for 3.4+), saved-array count, then each
// {typed save key, flags, flat element count, typed elements...}. Bit 0 of
// flags means map: alternating key/value elements, count includes both.
// Types: NONE=0, INT=1, FLOAT=2 (raw IEEE bits), STR=3 (length + bytes,
// including NUL). 4.4 may leave garbage in the type's upper 16 bits.
// There is no separate version DWORD or saved-key-to-ID table in 3.4+;
// IDs are reassigned on load. Pre-3.4 can end at the old zero count.
// Modern files finish with drug PID count/data, outside the arrays section.
// https://github.com/sfall-team/sfall/blob/master/sfall/Modules/Scripting/Arrays.cpp
// https://github.com/sfall-team/sfall/blob/master/sfall/Modules/LoadGameHook.cpp
// https://github.com/sfall-team/sfall/blob/master/sfall/Modules/Perks.cpp
static Buffer save(bool modern, bool fakePerks)
{
    Buffer buffer;
    buffer.word(1); // global count
    buffer.word(17); buffer.word(0); buffer.word(42); buffer.word(0);
    buffer.word(0); buffer.word(1); // object counter and addedYears
    for (int section = 0; section < 3; section++) {
        buffer.word(fakePerks ? 1 : 0);
        if (fakePerks) buffer.bytes.insert(buffer.bytes.end(), 1096, 0xA5);
    }
    buffer.word(0); // old arrays count
    if (!modern) return buffer;
    buffer.word(3); // saved arrays
    buffer.string("list"); buffer.word(0); buffer.word(4);
    buffer.number(1, 123); buffer.number(2, 0x3FC00000); // 1.5f
    buffer.string("synthetic string"); buffer.number(0, 0); // unset element
    buffer.number(1, 77); buffer.word(1 | 2 | 32); buffer.word(6); // map + const + transient
    buffer.string("key"); buffer.number(0xABCD0001, 456);
    buffer.number(1, 9); buffer.string("");
    buffer.number(2, 0x3FC00000); buffer.string(std::string(1023, 'x'));
    buffer.number(2, 0x40200000); buffer.word(0); buffer.word(0); // float save key, empty list
    buffer.word(0); // drug PID count (must remain unread)
    return buffer;
}

static void checkArrays()
{
    int global;
    CHECK(sfall_gl_vars_fetch(17, global) && global == 42);
    ArrayId list = LoadArray(text("list"), nullptr);
    CHECK(list != 0 && LenArray(list) == 4);
    CHECK(GetArray(list, ProgramValue(0), nullptr).asInt() == 123);
    CHECK(GetArray(list, ProgramValue(1), nullptr).floatValue == 1.5f);
    auto string = GetArray(list, ProgramValue(2), nullptr);
    CHECK(strings.at(string.integerValue) == "synthetic string");
    CHECK(GetArray(list, ProgramValue(3), nullptr).asInt() == 0);
    ArrayId map = LoadArray(ProgramValue(77), nullptr);
    CHECK(map != 0 && LenArray(map) == 3);
    CHECK(GetArrayKey(map, -1, nullptr).asInt() == 1);
    CHECK(GetArray(map, text("key"), nullptr).asInt() == 456);
    CHECK(strings.at(GetArray(map, ProgramValue(9), nullptr).integerValue).empty());
    CHECK(strings.at(GetArray(map, ProgramValue(1.5f), nullptr).integerValue) == std::string(1023, 'x'));
    SetArray(map, text("key"), ProgramValue(999), false, nullptr);
    CHECK(GetArray(map, text("key"), nullptr).asInt() == 456); // const flag retained
    CHECK(LenArray(LoadArray(ProgramValue(2.5f), nullptr)) == 0);
}

int main()
{
    CHECK(sfallArraysInit() && sfall_gl_vars_init());
    for (bool fakePerks : { false, true }) {
        reset();
        Buffer real = save(true, fakePerks);
        CHECK(sfallLoadGameData(real.file()));
        CHECK(real.position == real.bytes.size() - 4);
        checkArrays();
        Buffer ce;
        CHECK(sfallSaveGameData(ce.file()));
        reset();
        ce.position = 0;
        CHECK(sfallLoadGameData(ce.file()));
        checkArrays();
    }
    reset();
    Buffer old = save(false, false);
    CHECK(sfallLoadGameData(old.file())); // Steam's empty legacy arrays at EOF
    CHECK(old.position == old.bytes.size());
    old.bytes.push_back(1); // partial new count is corrupt, not an old save
    old.position = 0;
    CHECK(!sfallLoadGameData(old.file()));
    Buffer empty;
    empty.word(0); empty.word(0); // CE's empty modern arrays section
    CHECK(sfallArraysLoad(empty.file()));
    Buffer oddMap;
    oddMap.word(0); oddMap.word(1); oddMap.number(1, 1);
    oddMap.word(1); oddMap.word(1); // a map needs complete key/value pairs
    CHECK(!sfallArraysLoad(oddMap.file()));
    Buffer truncated = save(true, false);
    truncated.bytes.resize(truncated.bytes.size() - 9);
    CHECK(!sfallLoadGameData(truncated.file()));
    sfallArraysExit();
    sfall_gl_vars_exit();
    puts("sfall arrays: real format, legacy EOF, CE roundtrip and truncation passed");
}
