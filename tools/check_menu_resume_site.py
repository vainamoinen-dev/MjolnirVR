#!/usr/bin/env python3
"""Source pin: the Menu resume is decided before the exclusive filter.

While the laser owns an in-game pause menu, vr.cpp's exclusive filter
(consumedControls.ApplyVr) clears every VR button except A. An earlier build
decided the Menu resume after that filter (in input.cpp's MergeVrPad), where
Menu is always released, so it never fired (seen in Reach and ODST). The
decision must:
- call Input_RequestPauseResume() exactly once, in src/dll/core/vr.cpp;
- guard it with game_menu_pointer::MenuResumesPause(..., rawMenuEdge);
- sit above consumedControls.ApplyVr(...), the one exclusive filter in vr.cpp.
input.cpp may define Input_RequestPauseResume but must not call it.

Exit 0 when the pin holds, 1 otherwise. --self-test checks the checker on
synthetic sources first. Python stdlib only.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CALL = re.compile(r"\bInput_RequestPauseResume\s*\(\s*\)\s*;")
DEFINITION = re.compile(r"\bvoid\s+Input_RequestPauseResume\s*\(\s*\)\s*\{")
GUARD = re.compile(r"\bMenuResumesPause\s*\((?P<args>[^;{}]*?\brawMenuEdge\b[^;{}]*?)\)\s*\)\s*$")
FILTER = re.compile(r"\bconsumedControls\s*\.\s*ApplyVr\s*\(")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def check(vr, inp):
    problems = []
    vr, inp = strip_comments(vr), strip_comments(inp)
    inp_calls = [m for m in CALL.finditer(inp) if not DEFINITION.search(inp, max(0, m.start() - 40), m.end() + 2)]
    if inp_calls:
        problems.append("input.cpp calls Input_RequestPauseResume() (after the filter, Menu is always released)")
    calls = list(CALL.finditer(vr))
    if len(calls) != 1:
        problems.append(f"vr.cpp calls Input_RequestPauseResume() {len(calls)} times, expected 1")
        return problems
    call = calls[0]
    # The guarding if-condition is the text between the previous statement end and the call.
    head = vr[:call.start()]
    start = max(head.rfind(";"), head.rfind("}"), head.rfind("{"))
    condition = head[start + 1:].strip()
    if not condition.startswith("if") or not GUARD.search(condition):
        problems.append("Input_RequestPauseResume() is not guarded by MenuResumesPause(..., rawMenuEdge)")
    filters = list(FILTER.finditer(vr))
    if len(filters) != 1:
        problems.append(f"vr.cpp has {len(filters)} consumedControls.ApplyVr(...) calls, expected 1 (re-pin)")
    elif filters[0].start() < call.start():
        problems.append("Input_RequestPauseResume() is below consumedControls.ApplyVr: Menu is cleared there")
    return problems


def self_test():
    good_vr = ("void Poll(){\n    const bool rawMenuEdge=pad.menu&&!previous;\n"
               "    if(game_menu_pointer::MenuResumesPause(pointerMode,\n        paused,rawMenuEdge))\n"
               "        Input_RequestPauseResume();\n"
               "    consumedControls.ApplyVr(pad,pad.exclusiveInput,true);\n}\n")
    good_in = "void Input_RequestPauseResume()\n{\n    pulse=1;\n}\n"
    cases = [
        (good_vr, good_in, True),
        (good_vr, good_in + "void Merge(){ Input_RequestPauseResume(); }", False),
        (good_vr.replace("rawMenuEdge))", "pad.menu))"), good_in, False),
        ("void Poll(){\n    consumedControls.ApplyVr(pad,x,true);\n"
         "    if(game_menu_pointer::MenuResumesPause(p,q,rawMenuEdge))\n        Input_RequestPauseResume();\n}\n",
         good_in, False),
        (good_vr + "void X(){ Input_RequestPauseResume(); }", good_in, False),
        (good_vr.replace("consumedControls.ApplyVr", "other.ApplyVr"), good_in, False),
        ("void Early(){ consumedControls.ApplyVr(pad,x,true); }\n" + good_vr, good_in, False),
        ("// Input_RequestPauseResume();\n" + good_vr, good_in + "// Input_RequestPauseResume();", True),
    ]
    for i, (vr, inp, ok) in enumerate(cases):
        if (not check(vr, inp)) != ok:
            print(f"SELF-TEST FAIL case {i}: {check(vr, inp)}")
            return False
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test and not self_test():
        return 1
    read = lambda p: (ROOT / p).read_text(encoding="utf-8-sig", errors="replace")
    # E2-VR 7b moved the controller code, this site with it, into vr_controllers.cpp: the "vr.cpp" text is
    # vr.cpp followed by that unit.
    vr = read("src/dll/core/vr.cpp") + "\n" + read("src/dll/core/vr_controllers.cpp")
    problems = check(vr, read("src/dll/core/input.cpp"))
    for p in problems:
        print(f"FAIL {p}")
    if not problems:
        print("OK: the Menu resume is decided on the raw edge in vr.cpp, above the exclusive filter")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
