"""Read-only companion to nevada-audit/tools/{scan.py,dat2.py}.

Run from the repo root: python3 docs/compat/scan_nevada.py <package-root>
Archive members stay in memory; no extraction or game code execution.
"""

import collections
import pathlib
import re
import struct
import sys
import zlib

MAX_SCRIPT_SIZE = 16 * 1024 * 1024
root = pathlib.Path(sys.argv[1])
source = "".join(
    path.read_text()
    for pattern in ["sfall*.cc", "interpreter*.cc"]
    for path in pathlib.Path("src").glob(pattern)
)
registered = {
    int(value, 16)
    for value in re.findall(r"interpreterRegisterOpcode\((0x[0-9a-fA-F]+)", source)
}
scripts = [
    (str(path.relative_to(root)), path.read_bytes())
    for path in sorted((root / "sfall/scripts").glob("gl_*.int"))
]
loose_count = len(scripts)
entries = []
with (root / "sfall.dat").open("rb") as archive:
    archive.seek(0, 2)
    archive_size = archive.tell()
    archive.seek(-8, 2)
    tree_size, declared_size = struct.unpack("<II", archive.read(8))
    assert declared_size == archive_size and tree_size <= archive_size - 8
    archive.seek(-8 - tree_size, 2)
    count = struct.unpack("<I", archive.read(4))[0]
    assert count <= tree_size // 17
    for _ in range(count):
        length = struct.unpack("<I", archive.read(4))[0]
        assert length <= tree_size
        name = archive.read(length).decode("latin1")
        compressed, raw_size, packed_size, offset = struct.unpack("<BIII", archive.read(13))
        entries.append((name, compressed, raw_size, packed_size, offset))
    for name, compressed, raw_size, packed_size, offset in entries:
        if not name.lower().endswith(".int"):
            continue
        assert raw_size <= MAX_SCRIPT_SIZE and packed_size <= MAX_SCRIPT_SIZE
        assert offset + packed_size <= archive_size - tree_size - 8
        archive.seek(offset)
        data = archive.read(packed_size)
        if compressed:
            inflater = zlib.decompressobj()
            data = inflater.decompress(data, raw_size + 1)
            assert inflater.eof and not inflater.unconsumed_tail
        assert len(data) == raw_size
        scripts.append(("sfall.dat:" + name, data))

print("Shipping Nevada scan (audit tools/scan.py stream layout; DAT2 tools/dat2.py format).")
print("Package:", root)
print("sfall.dat entries:", count, "; .int:", len(scripts) - loose_count)
print("Loose globals:", loose_count)
opcodes = collections.defaultdict(set)
strings = collections.defaultdict(set)
for name, data in scripts:
    assert len(data) <= MAX_SCRIPT_SIZE
    procedures = struct.unpack_from(">I", data, 42)[0]
    offset = 46 + procedures * 24
    identifiers_size = struct.unpack_from(">I", data, offset)[0]
    offset += 4 + identifiers_size + 4
    strings_size = struct.unpack_from(">I", data, offset)[0]
    offset += 4
    if strings_size != 0xFFFFFFFF:
        assert offset + strings_size <= len(data)
        for string in re.findall(rb"[\x20-\x7e]{3,}", data[offset:offset + strings_size]):
            strings[string.decode()].add(name)
        offset += strings_size
    # The supplied scanner includes this table terminator as two opcodes.
    if data[offset:offset + 4] == b"\xff" * 4:
        offset += 4
    while offset + 2 <= len(data):
        opcode = struct.unpack_from(">H", data, offset)[0]
        offset += 2
        if opcode in (0xC001, 0xA001, 0x9001):
            offset += 4
            assert offset <= len(data)
        else:
            assert opcode & 0x8000, (name, hex(opcode), offset)
            opcodes[opcode].add(name)
    assert offset == len(data)

missing = sorted(opcode for opcode in opcodes if 0x8155 < opcode < 0x9000 and opcode not in registered)
print("Parsed scripts:", len(scripts), "; malformed: 0; distinct opcodes:", len(opcodes))
print("Used-but-missing in current source:", len(missing))
for opcode in missing:
    print(hex(opcode), ", ".join(sorted(opcodes[opcode])))
print("All sfall-range opcodes:")
for opcode, names in sorted(opcodes.items()):
    if 0x8155 < opcode < 0x9000:
        print(hex(opcode), len(names), ", ".join(sorted(names)))
print("Metarule strings of interest:")
for string in ["set_dude_obj", "intface_hide", "intface_show", "intface_is_hidden",
               "get_current_inven_size", "interface_overlay"]:
    if string in strings:
        print(string, ", ".join(sorted(strings[string])))
if missing:
    sys.exit(1)
