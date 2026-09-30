"""Every string the code asks to translate must exist as a key.

A missing key does not fail loudly: CTranslate returns the key itself, so the
string silently stays English in all 14 locales. This catches that.
"""

import io
import os
import json
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))

source = io.open(os.path.join(ROOT, "dllmain.cpp"), encoding="utf-8-sig").read()
table = json.load(io.open(os.path.join(ROOT, "translation.json"), encoding="utf-8"))
keys = set(table["en-US"])

LITERAL = re.compile(r'L"((?:[^"\\]|\\.)*)"')
CALL = re.compile(r'\b(?:tr|TR)\(\s*((?:L"(?:[^"\\]|\\.)*"\s*)+)')

used = set()
for blob in CALL.findall(source):
    used.add("".join(LITERAL.findall(blob)))

print("%d distinct translated strings referenced in code" % len(used))
print()

missing = sorted(u for u in used if u not in keys)
for text in sorted(used):
    print("  [%s] %s" % ("OK  " if text in keys else "MISS", text[:76]))

unused = sorted(keys - used)
print()
print("keys defined but not referenced:", unused if unused else "none")
print()

if missing:
    print("FAILED - missing keys:")
    for text in missing:
        print("   ", text)
    sys.exit(1)

print("PASSED - every referenced string has a key in all %d languages" % len(table))
