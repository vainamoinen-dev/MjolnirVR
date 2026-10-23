#!/usr/bin/env python3
"""Source pin: ODST's tick must not force the scope off every frame.

AutoVrTickOdst (src/dll/titles/odst/odst_camera_core.cpp) runs on every Present while ODST owns
the camera. An unconditional VR_SetScopeActive(false) there flipped the scope
off/on every frame, reset the R3 zoom toggle and marked the glass image stale.
The clear is allowed only behind `if (!odstTitleActive)`.

Exit 0 when AutoVrTickOdst holds exactly one VR_SetScopeActive(false), guarded
by `if (!odstTitleActive)`; 1 otherwise. --self-test checks the checker on
synthetic sources first, then the real tree. Python stdlib only.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "src" / "dll" / "titles" / "odst" / "odst_camera_core.cpp"
FUNCTION = "bool AutoVrTickOdst()"
CLEAR = "VR_SetScopeActive(false);"
GUARD = re.compile(r"if\s*\(\s*!\s*odstTitleActive\s*\)\s*$")


def function_body(text):
    start = text.find(FUNCTION)
    if start < 0:
        return None
    brace = text.find("{", start)
    depth, i = 1, brace + 1
    while depth and i < len(text):
        depth += (text[i] == "{") - (text[i] == "}")
        i += 1
    return text[brace:i]


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def check(text):
    """Return a list of problems (empty when the pin holds)."""
    body = function_body(text)
    if body is None:
        return [f"{FUNCTION} not found"]
    body = strip_comments(body)
    problems, guarded = [], 0
    for m in re.finditer(re.escape(CLEAR), body):
        before = body[:m.start()].rstrip()
        line_start = before.rfind("\n") + 1
        if GUARD.search(before[line_start:]) or GUARD.search(before):
            guarded += 1
        else:
            line = body.count("\n", 0, m.start()) + 1
            problems.append(f"unguarded {CLEAR} at body line {line}")
    if guarded != 1:
        problems.append(f"expected exactly one guarded clear, found {guarded}")
    return problems


def self_test():
    good = FUNCTION + "\n{\n    const bool odstTitleActive = true;\n" \
        "    if (!odstTitleActive) VR_SetScopeActive(false);\n    return true;\n}\n"
    good_two_lines = FUNCTION + "\n{\n    if (!odstTitleActive)\n        VR_SetScopeActive(false);\n}\n"
    bad = FUNCTION + "\n{\n    VR_SetScopeActive(false);\n    return true;\n}\n"
    bad_extra = FUNCTION + "\n{\n    if (!odstTitleActive) VR_SetScopeActive(false);\n" \
        "    { VR_SetScopeActive(false); }\n}\n"
    commented = FUNCTION + "\n{\n    // VR_SetScopeActive(false);\n}\n"
    other_function = "static bool Other()\n{\n    VR_SetScopeActive(false);\n}\n" + good
    cases = [(good, True), (good_two_lines, True), (bad, False), (bad_extra, False),
             (commented, False), (other_function, True)]
    for i, (text, ok) in enumerate(cases):
        if (not check(text)) != ok:
            print(f"SELF-TEST FAIL case {i}: {check(text)}")
            return False
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--source", type=pathlib.Path, default=SOURCE)
    args = parser.parse_args()
    if args.self_test and not self_test():
        return 1
    problems = check(args.source.read_text(encoding="utf-8-sig", errors="replace"))
    for p in problems:
        print(f"FAIL {p}")
    if not problems:
        print(f"OK: {FUNCTION} clears the scope only when ODST is not the active title")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
