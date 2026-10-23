#!/usr/bin/env python3
"""Source pin: Reach contact melee retires as its own hook set.

Reach contact melee installs through muzzle_hook_set.h, so it also retires as
its own hook set rather than as rows of the Reach camera table, as Halo 2 and
Halo 4 already do. In src/dll/titles/reach/reach_camera_core.cpp (game.cpp until E2):
- kReachHookRemovalOrder and DisableAndRemoveReachHooks name no g_reachContact;
- RemoveReachCameraCore calls RemoveReachContactMelee() exactly once, as
  `if (!RemoveReachContactMelee()) return false;`, after RemoveReachMuzzle()
  and before DisableAndRemoveReachHooks() (so the camera table never runs
  while contact is busy);
- no other file calls RemoveReachContactMelee() except its own hook-set
  retire in reach_contact_melee_runtime.inl.

Exit 0 when the pin holds, 1 otherwise. --self-test checks the checker on
synthetic sources first. Python stdlib only.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CALL = re.compile(r"(?<!bool )\bRemoveReachContactMelee\s*\(\s*\)")
GUARDED = re.compile(r"if\s*\(\s*!\s*RemoveReachContactMelee\s*\(\s*\)\s*\)\s*return\s+false\s*;")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def body(text, head):
    """The brace-matched body that follows the first match of `head`, or None."""
    m = re.search(head, text)
    if not m:
        return None
    start = text.find("{", m.end())
    if start < 0:
        return None
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return None


def check(game, others):
    problems = []
    game = strip_comments(game)
    table = body(game, r"\bkReachHookRemovalOrder\s*\[\s*\]\s*=")
    if table is None:
        problems.append("kReachHookRemovalOrder not found (re-pin)")
    elif "g_reachContact" in table:
        problems.append("kReachHookRemovalOrder still lists a Reach contact hook")
    disable = body(game, r"\bbool\s+DisableAndRemoveReachHooks\s*\(\s*\)")
    if disable is None:
        problems.append("DisableAndRemoveReachHooks not found (re-pin)")
    elif "g_reachContact" in disable:
        problems.append("DisableAndRemoveReachHooks still touches g_reachContact")
    core = body(game, r"\bbool\s+RemoveReachCameraCore\s*\(\s*\)")
    if core is None:
        problems.append("RemoveReachCameraCore not found (re-pin)")
        return problems
    calls = list(CALL.finditer(core))
    if len(calls) != 1:
        problems.append(f"RemoveReachCameraCore calls RemoveReachContactMelee() {len(calls)} times, expected 1")
        return problems
    if len(GUARDED.findall(core)) != 1:
        problems.append("RemoveReachContactMelee() failure does not return false from RemoveReachCameraCore")
    muzzle = core.find("RemoveReachMuzzle()")
    table_run = core.find("DisableAndRemoveReachHooks()")
    at = calls[0].start()
    if muzzle < 0 or muzzle > at:
        problems.append("RemoveReachContactMelee() is not after RemoveReachMuzzle()")
    if table_run < 0 or table_run < at:
        problems.append("RemoveReachContactMelee() is not before DisableAndRemoveReachHooks()")
    if "kReachHookRemovalOrder" in core[:at]:
        problems.append("the camera table is used before RemoveReachContactMelee()")
    outside = sum(len(CALL.findall(strip_comments(t))) for t in others)
    if CALL.search(game.replace(core, "")):
        problems.append("reach_camera_core.cpp calls RemoveReachContactMelee() outside RemoveReachCameraCore")
    if outside:
        problems.append(f"{outside} other call(s) of RemoveReachContactMelee() outside reach_camera_core.cpp")
    return problems


def self_test():
    good = ("const ReachHookSlot kReachHookRemovalOrder[] = {\n    {&g_reachHudAnchorBasisTarget, [] { x = nullptr; }},\n};\n"
            "bool DisableAndRemoveReachHooks()\n{\n    void* const targets[] = { g_reachHudAnchorBasisTarget };\n    return true;\n}\n"
            "bool RemoveReachCameraCore()\n{\n    if (!RemoveReachMuzzle()) return false;\n"
            "    if (!RemoveReachContactMelee()) return false;\n    g_reachCamera.armed = false;\n"
            "    if (!DisableAndRemoveReachHooks())\n        return false;\n    return true;\n}\n")
    cases = [
        (good, [], True),
        (good.replace("{&g_reachHudAnchorBasisTarget", "{&g_reachContact.targets[1], [] {}},\n    {&g_reachHudAnchorBasisTarget"), [], False),
        (good.replace("void* const targets[] = { g_reachHudAnchorBasisTarget };",
                      "g_reachContact.enabled.store(false);"), [], False),
        (good.replace("    if (!RemoveReachContactMelee()) return false;\n", ""), [], False),
        (good.replace("if (!RemoveReachContactMelee()) return false;", "(void)RemoveReachContactMelee();"), [], False),
        (good.replace("    if (!RemoveReachMuzzle()) return false;\n    if (!RemoveReachContactMelee()) return false;\n",
                      "    if (!RemoveReachContactMelee()) return false;\n    if (!RemoveReachMuzzle()) return false;\n"), [], False),
        (good.replace("    if (!DisableAndRemoveReachHooks())\n        return false;\n", "")
             .replace("    g_reachCamera.armed = false;\n",
                      "    if (!DisableAndRemoveReachHooks())\n        return false;\n    g_reachCamera.armed = false;\n")
             .replace("    if (!RemoveReachContactMelee()) return false;\n", "")
             .replace("    return true;\n}\n", "    if (!RemoveReachContactMelee()) return false;\n    return true;\n}\n", 1)
             , [], False),
        (good + "void Other(){ RemoveReachContactMelee(); }\n", [], False),
        (good, ["void X(){ (void)RemoveReachContactMelee(); }"], False),
        (good, ["bool RemoveReachContactMelee();\nbool RemoveReachContactMelee()\n{ return true; }"], True),
        ("// if (!RemoveReachContactMelee()) return false;\n" + good, [], True),
    ]
    for i, (game, others, ok) in enumerate(cases):
        if (not check(game, others)) != ok:
            print(f"SELF-TEST FAIL case {i}: {check(game, others)}")
            return False
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test and not self_test():
        return 1
    read = lambda p: p.read_text(encoding="utf-8-sig", errors="replace")
    game_path = ROOT / "src/dll/titles/reach/reach_camera_core.cpp"
    others = [read(p) for p in sorted((ROOT / "src").rglob("*"))
              if p.suffix in (".cpp", ".h", ".inl") and p != game_path]
    problems = check(read(game_path), others)
    for p in problems:
        print(f"FAIL {p}")
    if not problems:
        print("OK: Reach contact melee retires on its own, from RemoveReachCameraCore, before the camera table")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
