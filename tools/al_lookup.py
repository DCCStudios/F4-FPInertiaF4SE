"""Address Library lookup for F4SE version-*.bin files (format V0).

The F4SE loader path in Dear-Modding CommonLibF4 (REL::IDDB::load_v0)
memory-maps these bins as a flat table: a u64 entry count followed by
`count` pairs of (u64 id, u64 offset). This is NOT the Skyrim
versiondb v2 delta encoding.

Usage:
    python al_lookup.py <bin> <id> [<id> ...]      ID -> RVA
    python al_lookup.py <bin> --rva <rva>          RVA -> matching IDs
"""
import struct
import sys


def load(path):
    data = open(path, "rb").read()
    count, = struct.unpack_from("<Q", data, 0)
    return data, count


def main():
    data, count = load(sys.argv[1])
    if sys.argv[2] == "--rva":
        target = int(sys.argv[3], 0)
        hits = []
        for n in range(count):
            i, o = struct.unpack_from("<QQ", data, 8 + 16 * n)
            if o == target:
                hits.append(i)
        print(f"ids for rva {target:#x}: {hits}")
        return
    wanted = {int(a, 0) for a in sys.argv[2:]}
    found = {}
    for n in range(count):
        i, o = struct.unpack_from("<QQ", data, 8 + 16 * n)
        if i in wanted:
            found[i] = o
    for i in sorted(wanted):
        if i in found:
            print(f"id {i} -> {found[i]:#x}")
        else:
            print(f"id {i} -> MISSING")


if __name__ == "__main__":
    main()
