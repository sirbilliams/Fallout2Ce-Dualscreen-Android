#include "sfall_compat.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "interpreter.h"
#include "opcode_context.h"
#include "proto.h"
#include "sfall_metarules.h"

using namespace fallout;
namespace fallout {
extern std::vector<std::string> compatTestLogs;
}

struct Contract {
    int opcode;
    const char* name;
    int arity;
    bool returnsValue;
    int result;
};

// Independent expected ABI, checked against sfall's documented signatures.
static const Contract contracts[] = {
    { 0x8157, "read_short", 1, true, 0 },
    { 0x8158, "read_int", 1, true, 0 },
    { 0x81CF, "write_byte", 2, false, 0 },
    { 0x81D0, "write_short", 2, false, 0 },
    { 0x81D1, "write_int", 2, false, 0 },
    { 0x81D2, "call_offset_v0", 1, false, 0 },
    { 0x81D3, "call_offset_v1", 2, false, 0 },
    { 0x81D4, "call_offset_v2", 3, false, 0 },
    { 0x81D5, "call_offset_v3", 4, false, 0 },
    { 0x81D6, "call_offset_v4", 5, false, 0 },
    { 0x81D7, "call_offset_r0", 1, true, 0 },
    { 0x81D8, "call_offset_r1", 2, true, 0 },
    { 0x81D9, "call_offset_r2", 3, true, 0 },
    { 0x81DA, "call_offset_r3", 4, true, 0 },
    { 0x81DB, "call_offset_r4", 5, true, 0 },
    { 0x8165, "graphics_funcs_available", 0, true, 0 },
    { 0x8166, "load_shader", 1, true, -1 },
    { 0x8167, "free_shader", 1, false, 0 },
    { 0x8168, "activate_shader", 1, false, 0 },
    { 0x8169, "deactivate_shader", 1, false, 0 },
    { 0x816D, "set_shader_int", 3, false, 0 },
    { 0x816E, "set_shader_float", 3, false, 0 },
    { 0x816F, "set_shader_vector", 6, false, 0 },
    { 0x81AD, "get_shader_version", 0, true, 0 },
    { 0x8284, "set_shader_type", 2, false, 0 },
    { 0x818B, "set_pipboy_available", 1, false, 0 },
    { 0x81BB, "set_fake_perk", 4, false, 0 },
    { 0x81BC, "set_fake_trait", 4, false, 0 },
    { 0x81BD, "set_selectable_perk", 4, false, 0 },
    { 0x81BE, "set_perkbox_title", 1, false, 0 },
    { 0x81BF, "hide_real_perks", 0, false, 0 },
    { 0x81C0, "show_real_perks", 0, false, 0 },
    { 0x81C1, "has_fake_perk", 1, true, 0 },
    { 0x81C2, "has_fake_trait", 1, true, 0 },
    { 0x81C3, "perk_add_mode", 1, false, 0 },
    { 0x81C4, "clear_selectable_perks", 0, false, 0 },
    { 0x81AA, "set_xp_mod", 1, false, 0 },
    { 0x81AB, "set_perk_level_mod", 1, false, 0 },
    { 0x81CE, "set_hp_per_level_mod", 1, false, 0 },
    { 0x81C5, "set_critter_hit_chance_mod", 3, false, 0 },
    { 0x81A5, "inc_npc_level", 1, false, 0 },
    { 0x81CC, "apply_heaveho_fix", 0, false, 0 },
    { 0x81EA, "init_hook", 0, true, 0 },
    { 0x8258, "array_key_exists", 2, true, 0 },
    { 0x8282, "get_config_setting", 3, true, 0 },
};

static void checkOpcode(Program& program, const Contract& contract)
{
    auto handler = gInterpreterOpcodeHandlers[contract.opcode & 0x3FFF];
    assert(handler != nullptr);
    const auto originalLogs = compatTestLogs.size();
    for (int repeat = 0; repeat < 2; repeat++) {
        programStackPushInteger(&program, 0x12345678);
        // Mixed argument types ensure stubs discard values without dereferencing
        // fake EXE pointers or treating strings/floats as integer arguments.
        for (int arg = 0; arg < contract.arity; arg++) {
            if (repeat == 1 && arg == 0) {
                programStackPushString(&program, "dynamic argument");
                continue;
            }
            ProgramValue value;
            switch (arg % 3) {
            case 0:
                value.opcode = VALUE_TYPE_STRING;
                value.integerValue = 0;
                break;
            case 1:
                value.opcode = VALUE_TYPE_FLOAT;
                value.floatValue = 3.5f;
                break;
            default:
                value.opcode = VALUE_TYPE_PTR;
                value.pointerValue = &program;
                break;
            }
            programStackPushValue(&program, value);
        }
        handler(&program);
        assert(program.stackValues->size() == (contract.returnsValue ? 2 : 1));
        if (contract.returnsValue) {
            auto result = programStackPopValue(&program);
            assert(result.opcode == VALUE_TYPE_INT);
            assert(result.integerValue == contract.result);
        }
        assert(programStackPopInteger(&program) == 0x12345678);
    }
    assert(compatTestLogs.size() == originalLogs + 1);
    assert(compatTestLogs.back() == std::string("sfall stub: ") + contract.name + "\n");
}

static int query(Program& program, int opcode, int argument)
{
    programStackPushInteger(&program, 99);
    programStackPushInteger(&program, argument);
    auto handler = gInterpreterOpcodeHandlers[opcode & 0x3FFF];
    assert(handler != nullptr);
    handler(&program);
    int result = programStackPopInteger(&program);
    assert(programStackPopInteger(&program) == 99);
    return result;
}

static void checkMetarules(Program& program)
{
    const MetaruleInfo rules[] = {
        { "intface_hide", mf_intface_hide, 0, 0 },
        { "intface_show", mf_intface_show, 0, 0 },
        { "intface_is_hidden", mf_intface_is_hidden, 0, 0 },
        { "get_current_inven_size", mf_get_current_inven_size, 1, 1 },
        { "set_dude_obj", mf_set_dude_obj, 1, 1 },
        { "interface_overlay", mf_interface_overlay, 2, 6 },
    };
    for (int index = 0; index < 6; index++) {
        const auto& rule = rules[index];
        for (int arity = rule.minArgs; arity <= rule.maxArgs; arity++) {
            for (int repeat = 0; repeat < 2; repeat++) {
                ProgramValue args[6];
                programStackPushInteger(&program, 99);
                OpcodeContext ctx(&program, &rule, arity, args);
                rule.handler(ctx);
                ctx.pushReturnValue();
                auto result = programStackPopValue(&program);
                assert(result.opcode == VALUE_TYPE_INT && result.integerValue == 0);
                assert(programStackPopInteger(&program) == 99);
            }
        }
    }
    OpcodeContext hidden(&program, &rules[2], 0, nullptr);
    mf_intface_hide(hidden);
    mf_intface_is_hidden(hidden);
    hidden.pushReturnValue();
    assert(programStackPopInteger(&program) == 1);
    for (const char* name : { "set_dude_obj", "interface_overlay" }) {
        assert(std::count(compatTestLogs.begin(), compatTestLogs.end(), std::string("sfall stub: ") + name + "\n") == 1);
    }
}

int main()
{
    Program program = {};
    ProgramStack stack;
    program.stackValues = &stack;
    sfallCompatibilityInit();
    for (const auto& contract : contracts) {
        checkOpcode(program, contract);
    }
    assert(query(program, 0x8156, 0x1234) == 0);
    assert(query(program, 0x8156, 0x1234) == 0);
    assert(query(program, 0x8156, 0x56D38C) == 2);
    assert(std::count(compatTestLogs.begin(), compatTestLogs.end(), "sfall stub: read_byte\n") == 1);
    assert(query(program, 0x818C, 0) == 37);
    assert(query(program, 0x818C, -1) == 0);
    assert(query(program, 0x8190, 5) == 0); // no player yet
    gDude = reinterpret_cast<Object*>(&program); // never dereferenced by boundary
    assert(query(program, 0x8190, 5) == 1);
    assert(query(program, 0x8190, 6) == 0);
    assert(query(program, 0x8190, -1) == 0);
    assert(query(program, 0x8190, INT_MAX) == 0);
    gDude = nullptr;
    assert(query(program, 0x8241, 0x1000001) == 3);
    assert(query(program, 0x8241, -1) == 0);
    programStackPushString(&program, "test npc");
    gInterpreterOpcodeHandlers[0x241](&program);
    assert(programStackPopInteger(&program) == 3);
    programStackPushString(&program, "absent npc");
    gInterpreterOpcodeHandlers[0x241](&program);
    assert(programStackPopInteger(&program) == 0);
    assert(query(program, 0x8285, 5) == 6);
    assert(query(program, 0x8285, INT_MAX) == INT_MIN);
    programStackPushFloat(&program, 1.5f);
    gInterpreterOpcodeHandlers[0x285](&program);
    auto value = programStackPopValue(&program);
    assert(value.opcode == VALUE_TYPE_FLOAT && value.floatValue == 2.5f);
    checkMetarules(program);
    assert(stack.empty());
    free(program.dynamicStrings);
    printf("ok: %zu stub contracts, real queries/increment, six metarules, once-only logs\n", sizeof(contracts) / sizeof(contracts[0]) + 1);
}
