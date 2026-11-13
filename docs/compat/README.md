# Nevada / Yesterday compatibility census (2026-11-13)

Read-first inputs: `/var/mnt/Elements/f2ds-work/{nevada-audit,yesterday-audit}/AUDIT.md`.
Yesterday evidence is its `.ssl` audit, not a successful binary scan.

Shipping Nevada: `/var/mnt/Elements/f2ds-work/nevada/pkg_hog/Fallout-Nevada-English/`.
[Supplied scanner output](nevada-audit-scanner.txt) comes from running the exact
`nevada-audit/tools/scan.py` against its `sfall/scripts` with this lane's baseline
registered opcodes. [Full census](nevada-shipping-scan.txt) also lists all users
and inspects the shipping `sfall.dat` using the audit's DAT2 format/layout.
The [repeatable scanner](scan_nevada.py) corrects the terminator issue below;
its [current output](nevada-current-scan.txt) reports zero missing opcodes.
The DAT contains 15 entries and **zero `.int` files**; all 13 loose globals parse.
No game executable or DLL was executed, and no game files were changed.

The original scanner reports 201 distinct words and 14 missing entries.
`0xffff` is a false positive: in 12 files the scanner starts at the four-byte
`ffffffff` string-table terminator, immediately before code (e.g. gl_ammoMod
at file offset 0x2f8; code starts at 0x2fc). Do not register it as an opcode.
There are 13 actual missing executable opcodes, now registered:

| Opcode | Name | Shipping script users |
| --- | --- | --- |
| 0x8158 | read_int | gl_z_sfall-mods |
| 0x8165/66/68/69/6d | graphics availability, load/activate/deactivate/set_shader_int | gl_dialog_fx |
| 0x81ad | get_shader_version | gl_dialog_fx |
| 0x81cc | apply_heaveho_fix | gl_z_sfall-mods |
| 0x81ea | init_hook | gl_dialog_fx |
| 0x8258 | array_key_exists | gl_ammoMod |
| 0x8282 | get_config_setting | gl_ammoMod, gl_z_partycontrol, gl_z_sfall-mods |
| 0x8284 | set_shader_type | gl_dialog_fx |
| 0x8285 | numeric increment | gl_npcArmor, gl_z_highlighting, gl_z_partycontrol, gl_z_sfall-mods |

The shipping globals also contain the metarule strings set_dude_obj and
intface_hide/show/is_hidden. The Extended audit additionally uses read_short,
call_offset_v2 and get_current_inven_size. Yesterday supplies the remaining
fake perk/trait, perk UI, modifier, memory call, NPC and interface_overlay gaps.
The engine compatibility change covers their union (including unused adjacent
call_offset forms and shader free/float/vector setters).

## Signature verification

Ordinary contracts come from the [sfall opcode list](https://github.com/sfall-team/sfall/blob/master/artifacts/scripting/sfall%20opcode%20list.txt)
and [Opcodes.cpp registration table](https://github.com/sfall-team/sfall/blob/master/sfall/Modules/Scripting/Opcodes.cpp).
Memory calls have an address argument in addition to the suffix count;
`v` calls and writes are void, `r` calls return integer zero.
`set_critter_hit_chance_mod` consumes three values (object, max, modifier).
The packaged Yesterday `HEADERS/sfall.h` confirms the six metarule call forms.

The shipping repack's `!docs/sfall-readme.txt` identifies the separate
[sfall 5.x project](https://gitverse.ru/sfall_projects/sfall); its online headers
were unavailable. Signatures of its extra opcodes were verified by **static
inspection** of the packaged `ddraw.dll` PE registration data and handlers:

- Opcode function array base: VA 0x101cbd60 (written to engine address 0x46e390).
- 0x8258 registration at 0x1019b8d0: inline name `array_key_exists`, count 2 at
  0x1019b8f8, returning flag 1 at 0x1019b8fc.
- 0x8282 registration at 0x1019c870: inline name `get_config_setting`, count 3
  at 0x1019c898, returning flag 1 at 0x1019c89c.
- 0x8284 array slot 0x101cc770 -> handler 0x10128d60: two integer argument
  descriptors, no result push; sets the shader record's type field.
- 0x8285 array slot 0x101cc774 -> handler 0x10126220: one pop, integer increment
  or float add 1.0, result pushed preserving numeric type. Implemented, because
  returning zero would break the globals' loops. Integer wraparound is defined.
- 0x81cc -> 0x1012e980: changes two engine settings, no argument pop or result.

No sfall implementation code was copied. Host regression tests exercise the
registered CE handlers through `gInterpreterOpcodeHandlers` with real VM stack
operations. Engine services are isolated by test boundaries. Full gameplay,
missing hooks, custom perk/trait semantics, unavailable EXE patches and shader
rendering remain unverified/unsupported; registering stubs only prevents these
particular undefined-opcode aborts.

Host build used `build-host` inside this worktree (the prescribed sibling build
path is outside the writable root), with `FALLOUT_VENDORED=OFF` after the default
configure could not download zlib. No install/catalog files were edited.
