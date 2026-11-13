#include "sfall_compat.h"

#include "character_editor.h"
#include "combat.h"
#include "critter.h"
#include "debug.h"
#include "interface.h"
#include "interpreter.h"
#include "item.h"
#include "opcode_context.h"
#include "party_member.h"
#include "perk.h"
#include "platform_compat.h"
#include "proto.h"

namespace fallout {

// Signatures: sfall's opcode list / Opcodes.cpp. See docs/compat/README.md
// for the shipping sfall 5.x additions. Consume values without interpreting
// strings, pointers or EXE addresses; PopValue also releases dynamic strings.
static void compatStub(Program* program, const char* name, int arity, bool returnsValue, int result, bool& logged)
{
    for (int index = 0; index < arity; index++) {
        programStackPopValue(program);
    }
    if (!logged) {
        debugPrint("sfall stub: %s\n", name);
        logged = true;
    }
    if (returnsValue) {
        programStackPushInteger(program, result);
    }
}

static void op_read_short(Program* program)
{
    static bool logged = false;
    compatStub(program, "read_short", 1, true, 0, logged);
}

static void op_read_int(Program* program)
{
    static bool logged = false;
    compatStub(program, "read_int", 1, true, 0, logged);
}

static void op_write_byte(Program* program)
{
    static bool logged = false;
    compatStub(program, "write_byte", 2, false, 0, logged);
}

static void op_write_short(Program* program)
{
    static bool logged = false;
    compatStub(program, "write_short", 2, false, 0, logged);
}

static void op_write_int(Program* program)
{
    static bool logged = false;
    compatStub(program, "write_int", 2, false, 0, logged);
}

static void op_call_offset_v0(Program* program)
{
    static bool logged = false;
    compatStub(program, "call_offset_v0", 1, false, 0, logged);
}

static void op_call_offset_v1(Program* program)
{
    static bool logged = false;
    compatStub(program, "call_offset_v1", 2, false, 0, logged);
}

static void op_call_offset_v2(Program* program)
{
    static bool logged = false;
    compatStub(program, "call_offset_v2", 3, false, 0, logged);
}

static void op_call_offset_v3(Program* program)
{
    static bool logged = false;
    compatStub(program, "call_offset_v3", 4, false, 0, logged);
}

static void op_call_offset_v4(Program* program)
{
    static bool logged = false;
    compatStub(program, "call_offset_v4", 5, false, 0, logged);
}

static void op_call_offset_r0(Program* program)
{
    static bool logged = false;
    compatStub(program, "call_offset_r0", 1, true, 0, logged);
}

static void op_call_offset_r1(Program* program)
{
    static bool logged = false;
    compatStub(program, "call_offset_r1", 2, true, 0, logged);
}

static void op_call_offset_r2(Program* program)
{
    static bool logged = false;
    compatStub(program, "call_offset_r2", 3, true, 0, logged);
}

static void op_call_offset_r3(Program* program)
{
    static bool logged = false;
    compatStub(program, "call_offset_r3", 4, true, 0, logged);
}

static void op_call_offset_r4(Program* program)
{
    static bool logged = false;
    compatStub(program, "call_offset_r4", 5, true, 0, logged);
}

static void op_graphics_funcs_available(Program* program)
{
    static bool logged = false;
    compatStub(program, "graphics_funcs_available", 0, true, 0, logged);
}

static void op_load_shader(Program* program)
{
    static bool logged = false;
    compatStub(program, "load_shader", 1, true, -1, logged);
}

static void op_free_shader(Program* program)
{
    static bool logged = false;
    compatStub(program, "free_shader", 1, false, 0, logged);
}

static void op_activate_shader(Program* program)
{
    static bool logged = false;
    compatStub(program, "activate_shader", 1, false, 0, logged);
}

static void op_deactivate_shader(Program* program)
{
    static bool logged = false;
    compatStub(program, "deactivate_shader", 1, false, 0, logged);
}

static void op_set_shader_int(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_shader_int", 3, false, 0, logged);
}

static void op_set_shader_float(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_shader_float", 3, false, 0, logged);
}

static void op_set_shader_vector(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_shader_vector", 6, false, 0, logged);
}

static void op_get_shader_version(Program* program)
{
    static bool logged = false;
    compatStub(program, "get_shader_version", 0, true, 0, logged);
}

static void op_set_shader_type(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_shader_type", 2, false, 0, logged);
}

static void op_set_pipboy_available(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_pipboy_available", 1, false, 0, logged);
}

static void op_set_fake_perk(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_fake_perk", 4, false, 0, logged);
}

static void op_set_fake_trait(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_fake_trait", 4, false, 0, logged);
}

static void op_set_selectable_perk(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_selectable_perk", 4, false, 0, logged);
}

static void op_set_perkbox_title(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_perkbox_title", 1, false, 0, logged);
}

static void op_hide_real_perks(Program* program)
{
    static bool logged = false;
    compatStub(program, "hide_real_perks", 0, false, 0, logged);
}

static void op_show_real_perks(Program* program)
{
    static bool logged = false;
    compatStub(program, "show_real_perks", 0, false, 0, logged);
}

static void op_has_fake_perk(Program* program)
{
    static bool logged = false;
    compatStub(program, "has_fake_perk", 1, true, 0, logged);
}

static void op_has_fake_trait(Program* program)
{
    static bool logged = false;
    compatStub(program, "has_fake_trait", 1, true, 0, logged);
}

static void op_perk_add_mode(Program* program)
{
    static bool logged = false;
    compatStub(program, "perk_add_mode", 1, false, 0, logged);
}

static void op_clear_selectable_perks(Program* program)
{
    static bool logged = false;
    compatStub(program, "clear_selectable_perks", 0, false, 0, logged);
}

static void op_set_xp_mod(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_xp_mod", 1, false, 0, logged);
}

static void op_set_perk_level_mod(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_perk_level_mod", 1, false, 0, logged);
}

static void op_set_hp_per_level_mod(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_hp_per_level_mod", 1, false, 0, logged);
}

static void op_set_critter_hit_chance_mod(Program* program)
{
    static bool logged = false;
    compatStub(program, "set_critter_hit_chance_mod", 3, false, 0, logged);
}

static void op_inc_npc_level(Program* program)
{
    static bool logged = false;
    compatStub(program, "inc_npc_level", 1, false, 0, logged);
}

// sfall accepts a prototype id or a case-insensitive display name.
static void op_get_npc_level(Program* program)
{
    ProgramValue key = programStackPopValue(program);
    int level = 0;
    for (Object* member : get_all_party_members_objects(true)) {
        if ((key.isInt() && member->pid == key.integerValue)
            || (key.isString() && compat_stricmp(critterGetName(member), key.asString(program)) == 0)) {
            level = partyMemberGetCurrentLevel(member);
            break;
        }
    }
    programStackPushInteger(program, level);
}

static void op_apply_heaveho_fix(Program* program)
{
    static bool logged = false;
    compatStub(program, "apply_heaveho_fix", 0, false, 0, logged);
}

static void op_init_hook(Program* program)
{
    static bool logged = false;
    compatStub(program, "init_hook", 0, true, 0, logged);
}

static void op_array_key_exists(Program* program)
{
    static bool logged = false;
    compatStub(program, "array_key_exists", 2, true, 0, logged);
}

static void op_get_config_setting(Program* program)
{
    static bool logged = false;
    compatStub(program, "get_config_setting", 3, true, 0, logged);
}

static void op_read_byte(Program* program)
{
    int address = programStackPopInteger(program);
    // Preserve the pre-existing CE mapping used by party-control scripts.
    if (address == 0x56D38C) {
        programStackPushInteger(program, combatGetTargetHighlight());
        return;
    }
    static bool logged = false;
    compatStub(program, "read_byte", 0, true, 0, logged);
}

static void op_get_kill_counter(Program* program)
{
    int type = programStackPopInteger(program);
    programStackPushInteger(program, killsGetByType(static_cast<KillType>(type)));
}

static void op_get_perk_available(Program* program)
{
    int requested = programStackPopInteger(program);
    int available = 0;
    if (gDude != nullptr && requested >= 0 && requested < PERK_COUNT) {
        Perk perks[PERK_COUNT];
        int count = perkGetAvailablePerks(gDude, characterEditorGetPerkSelectionLevel(), perks);
        for (int index = 0; index < count; index++) {
            if (static_cast<int>(perks[index]) == requested) {
                available = 1;
                break;
            }
        }
    }
    programStackPushInteger(program, available);
}

// sfall 5.x ++ operator. Keeping this real avoids breaking counted loops in
// the shipping globals. Unsigned addition gives defined integer wraparound.
static void op_increment(Program* program)
{
    ProgramValue value = programStackPopValue(program);
    if (value.isInt()) {
        programStackPushInteger(program, static_cast<unsigned int>(value.integerValue) + 1U);
    } else if (value.isFloat()) {
        programStackPushFloat(program, value.floatValue + 1.0f);
    } else {
        programStackPushInteger(program, 0);
    }
}

void sfallCompatibilityInit()
{
    interpreterRegisterOpcode(0x8157, op_read_short);
    interpreterRegisterOpcode(0x8158, op_read_int);
    interpreterRegisterOpcode(0x81CF, op_write_byte);
    interpreterRegisterOpcode(0x81D0, op_write_short);
    interpreterRegisterOpcode(0x81D1, op_write_int);
    interpreterRegisterOpcode(0x81D2, op_call_offset_v0);
    interpreterRegisterOpcode(0x81D3, op_call_offset_v1);
    interpreterRegisterOpcode(0x81D4, op_call_offset_v2);
    interpreterRegisterOpcode(0x81D5, op_call_offset_v3);
    interpreterRegisterOpcode(0x81D6, op_call_offset_v4);
    interpreterRegisterOpcode(0x81D7, op_call_offset_r0);
    interpreterRegisterOpcode(0x81D8, op_call_offset_r1);
    interpreterRegisterOpcode(0x81D9, op_call_offset_r2);
    interpreterRegisterOpcode(0x81DA, op_call_offset_r3);
    interpreterRegisterOpcode(0x81DB, op_call_offset_r4);
    interpreterRegisterOpcode(0x8165, op_graphics_funcs_available);
    interpreterRegisterOpcode(0x8166, op_load_shader);
    interpreterRegisterOpcode(0x8167, op_free_shader);
    interpreterRegisterOpcode(0x8168, op_activate_shader);
    interpreterRegisterOpcode(0x8169, op_deactivate_shader);
    interpreterRegisterOpcode(0x816D, op_set_shader_int);
    interpreterRegisterOpcode(0x816E, op_set_shader_float);
    interpreterRegisterOpcode(0x816F, op_set_shader_vector);
    interpreterRegisterOpcode(0x81AD, op_get_shader_version);
    interpreterRegisterOpcode(0x8284, op_set_shader_type);
    interpreterRegisterOpcode(0x818B, op_set_pipboy_available);
    interpreterRegisterOpcode(0x81BB, op_set_fake_perk);
    interpreterRegisterOpcode(0x81BC, op_set_fake_trait);
    interpreterRegisterOpcode(0x81BD, op_set_selectable_perk);
    interpreterRegisterOpcode(0x81BE, op_set_perkbox_title);
    interpreterRegisterOpcode(0x81BF, op_hide_real_perks);
    interpreterRegisterOpcode(0x81C0, op_show_real_perks);
    interpreterRegisterOpcode(0x81C1, op_has_fake_perk);
    interpreterRegisterOpcode(0x81C2, op_has_fake_trait);
    interpreterRegisterOpcode(0x81C3, op_perk_add_mode);
    interpreterRegisterOpcode(0x81C4, op_clear_selectable_perks);
    interpreterRegisterOpcode(0x81AA, op_set_xp_mod);
    interpreterRegisterOpcode(0x81AB, op_set_perk_level_mod);
    interpreterRegisterOpcode(0x81CE, op_set_hp_per_level_mod);
    interpreterRegisterOpcode(0x81C5, op_set_critter_hit_chance_mod);
    interpreterRegisterOpcode(0x81A5, op_inc_npc_level);
    interpreterRegisterOpcode(0x8241, op_get_npc_level);
    interpreterRegisterOpcode(0x81CC, op_apply_heaveho_fix);
    interpreterRegisterOpcode(0x81EA, op_init_hook);
    interpreterRegisterOpcode(0x8258, op_array_key_exists);
    interpreterRegisterOpcode(0x8282, op_get_config_setting);
    interpreterRegisterOpcode(0x8156, op_read_byte);
    interpreterRegisterOpcode(0x818C, op_get_kill_counter);
    interpreterRegisterOpcode(0x8190, op_get_perk_available);
    interpreterRegisterOpcode(0x8285, op_increment);
}

void mf_intface_hide(OpcodeContext& ctx)
{
    interfaceBarHide();
}

void mf_intface_show(OpcodeContext& ctx)
{
    interfaceBarShow();
}

void mf_intface_is_hidden(OpcodeContext& ctx)
{
    ctx.setReturn(static_cast<int>(interfaceBarIsHidden()));
}

void mf_get_current_inven_size(OpcodeContext& ctx)
{
    ctx.setReturn(containerGetTotalSize(ctx.arg(0).asObject()));
}

void mf_set_dude_obj(OpcodeContext& ctx)
{
    // Swapping gDude alone leaves player, combat and inventory state inconsistent.
    static bool logged = false;
    if (!logged) {
        debugPrint("sfall stub: set_dude_obj\n");
        logged = true;
    }
    ctx.setReturn(0);
}

void mf_interface_overlay(OpcodeContext& ctx)
{
    static bool logged = false;
    if (!logged) {
        debugPrint("sfall stub: interface_overlay\n");
        logged = true;
    }
    ctx.setReturn(0);
}

} // namespace fallout
