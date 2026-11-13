// Engine boundaries for the compatibility handlers. VM stack operations are real.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <strings.h>
#include <vector>

#include "character_editor.h"
#include "combat.h"
#include "critter.h"
#include "db.h"
#include "debug.h"
#include "interface.h"
#include "item.h"
#include "memory_manager.h"
#include "party_member.h"
#include "perk.h"
#include "platform_compat.h"
#include "proto.h"

namespace fallout {
std::vector<std::string> compatTestLogs;
Object* gDude = nullptr;
static bool hidden;

int debugPrint(const char* format, ...)
{
    char message[512];
    va_list args;
    va_start(args, format);
    int rc = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    compatTestLogs.emplace_back(message);
    return rc;
}
File* fileOpen(const char*, const char*) { return nullptr; }
int fileClose(File*) { return 0; }
int fileGetSize(File*) { return 0; }
size_t fileRead(void*, size_t, size_t, File*) { return 0; }
void* internal_malloc_safe(size_t size, const char*, int) { return malloc(size); }
void* internal_realloc_safe(void* ptr, size_t size, const char*, int) { return realloc(ptr, size); }
void internal_free_safe(void* ptr, const char*, int) { free(ptr); }
int combatGetTargetHighlight() { return 2; }
int killsGetByType(KillType type) { return static_cast<int>(type) == 0 ? 37 : 0; }
int characterEditorGetPerkSelectionLevel() { return 10; }
int perkGetAvailablePerks(Object*, int, Perk* perks)
{
    perks[0] = static_cast<Perk>(5);
    return 1;
}
void interfaceBarHide() { hidden = true; }
void interfaceBarShow() { hidden = false; }
bool interfaceBarIsHidden() { return hidden; }
std::vector<Object*> get_all_party_members_objects(bool)
{
    static Object npc = {};
    npc.pid = 0x1000001;
    return { &npc };
}
int partyMemberGetCurrentLevel(Object*) { return 3; }
char* critterGetName(Object*) { return const_cast<char*>("Test NPC"); }
int compat_stricmp(const char* lhs, const char* rhs) { return strcasecmp(lhs, rhs); }
int containerGetTotalSize(Object* object) { return object != nullptr ? 42 : 0; }
} // namespace fallout
