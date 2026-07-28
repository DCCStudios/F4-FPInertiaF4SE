"""Dump all NUL-terminated 'WeaponType*' / 'WeapType*' strings in a plugin.

EDID subrecords store editor IDs as plaintext, so a byte scan reveals every
weapon-type keyword spelling that actually exists in the master.
"""
import re
import sys
from collections import Counter

def main():
    data = open(sys.argv[1], "rb").read()
    print(f"scanned {len(data)} bytes")
    hits = Counter()
    for m in re.finditer(rb"Weap(?:on)?Type[A-Za-z0-9_]*\x00", data):
        hits[m.group()[:-1].decode()] += 1
    for name, n in sorted(hits.items()):
        print(f"{name:36s} x{n}")

if __name__ == "__main__":
    main()
