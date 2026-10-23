#!/usr/bin/env python3
"""Source pin: the held gun's profile is applied on the render thread.

VR_UpdateWeaponAlignment (which applies the held weapon's profile and lens row)
must be called exactly once: in src/dll/core/vr.cpp, immediately before
SubmitPreparedFrame(sc) in the Present path, so the frame that gates the scope
glass on the applied profile always runs after the apply. The title supervisor
(src/dll/core/game.cpp) must not call it: from there the glass waited up to a
supervisor tick for its lens row.

Exit 0 when the pin holds, 1 otherwise. --self-test checks the checker on
synthetic sources first. Python stdlib only.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CALL = re.compile(r"\bVR_UpdateWeaponAlignment\s*\(\s*\)\s*;")
SUBMIT = re.compile(r"\bSubmitPreparedFrame\s*\(\s*sc\s*\)\s*;")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def check(vr, game):
    problems = []
    vr, game = strip_comments(vr), strip_comments(game)
    if CALL.search(game):
        problems.append("game.cpp (the title supervisor) still calls VR_UpdateWeaponAlignment()")
    calls = list(CALL.finditer(vr))
    if len(calls) != 1:
        problems.append(f"vr.cpp calls VR_UpdateWeaponAlignment() {len(calls)} times, expected 1")
        return problems
    submit = SUBMIT.search(vr, calls[0].end())
    if not submit or vr[calls[0].end():submit.start()].strip():
        problems.append("VR_UpdateWeaponAlignment() is not immediately before SubmitPreparedFrame(sc)")
    return problems


def self_test():
    good_vr = "void Present(){\n    VR_UpdateWeaponAlignment();\n    SubmitPreparedFrame(sc);\n}\n"
    cases = [
        (good_vr, "void Tick(){ Config_ApplyTitleProfile(p); }", True),
        (good_vr, "void Tick(){ VR_UpdateWeaponAlignment(); }", False),
        ("void Present(){ SubmitPreparedFrame(sc); VR_UpdateWeaponAlignment(); }", "", False),
        ("void Present(){ VR_UpdateWeaponAlignment(); Other(); SubmitPreparedFrame(sc); }", "", False),
        (good_vr + "void X(){ VR_UpdateWeaponAlignment(); }", "", False),
        ("// VR_UpdateWeaponAlignment();\n" + good_vr, "// VR_UpdateWeaponAlignment();", True),
    ]
    for i, (vr, game, ok) in enumerate(cases):
        if (not check(vr, game)) != ok:
            print(f"SELF-TEST FAIL case {i}: {check(vr, game)}")
            return False
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test and not self_test():
        return 1
    read = lambda p: (ROOT / p).read_text(encoding="utf-8-sig", errors="replace")
    problems = check(read("src/dll/core/vr.cpp"), read("src/dll/core/game.cpp"))
    for p in problems:
        print(f"FAIL {p}")
    if not problems:
        print("OK: the held gun's profile is applied on the render thread, just before SubmitPreparedFrame")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
