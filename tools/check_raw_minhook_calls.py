"""Fail when DLL code calls MinHook's create/remove directly.

Every hook site under src/dll calls MCCVR_CreateHookChecked /
MCCVR_RemoveHookChecked (src/dll/core/hook_install_check.h), which run the
install-time detour check before MinHook. hook_install_check.cpp is the one
file that calls the real entry points. Names inside comments and string
literals (log text) are not calls. Python stdlib only; --self-test exercises
the scanner on synthetic sources first, then checks the real tree.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ALLOWED = {'src/dll/core/hook_install_check.cpp'}
RAW = re.compile(r'\b(MH_CreateHook|MH_RemoveHook)\b')
RAW_STRING = re.compile(r'(?:u8|u|U|L)?R"([^(\s]*)\(')


def code_only(text):
    """The text with comments, string and character literals blanked (newlines kept)."""
    out, i, n = [], 0, len(text)
    def blank(a, b):
        out.append(re.sub(r'[^\n]', ' ', text[a:b]))
    while i < n:
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            blank(i, j); i = j; continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            blank(i, j); i = j; continue
        prev = text[i - 1] if i else ''
        m = RAW_STRING.match(text, i) if not (prev.isalnum() or prev == '_') else None
        if m:
            end = text.find(')' + m.group(1) + '"', m.end())
            j = n if end < 0 else end + len(m.group(1)) + 2
            blank(i, j); i = j; continue
        c = text[i]
        if c == '"' or (c == "'" and not prev.isalnum()):
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            blank(i, j + 1); i = j + 1; continue
        out.append(c); i += 1
    return ''.join(out)


def raw_calls(text):
    code = code_only(text)
    return [code.count('\n', 0, m.start()) + 1 for m in RAW.finditer(code)]


def self_test():
    cases = [
        ('MH_CreateHook(a, b, c);', [1]),
        ('x = MH_RemoveHook(t);', [1]),
        ('// MH_CreateHook(a, b, c);', []),
        ('/* MH_RemoveHook(t) */ y();', []),
        ('LOG("MH_CreateHook failed for %s", n);', []),
        ('auto s = R"(MH_CreateHook)";', []),
        ("char q = '\"'; MH_CreateHook(a, b, c);", [1]),
        ('MCCVR_CreateHookChecked(a, b, c); MCCVR_RemoveHookChecked(t);', []),
        ('#define MH_CreateHook Mine', [1]),
        ('ok();\nMH_CreateHookEx(a);\nMH_RemoveHook(t);', [3]),
    ]
    for source, expected in cases:
        got = raw_calls(source)
        if got != expected:
            print(f'self-test failed: {source!r} gave {got}, expected {expected}')
            return False
    return True


def main():
    if '--self-test' in sys.argv[1:] and not self_test():
        return 1
    found = []
    for path in sorted((ROOT / 'src' / 'dll').rglob('*')):
        if path.suffix not in ('.c', '.cpp', '.h', '.hpp', '.inl') or not path.is_file():
            continue
        rel = path.relative_to(ROOT).as_posix()
        if rel in ALLOWED:
            continue
        for line in raw_calls(path.read_text(encoding='utf-8', errors='replace')):
            found.append(f'{rel}:{line}')
    if found:
        print('Raw MinHook create/remove in DLL code; call MCCVR_CreateHookChecked / '
              'MCCVR_RemoveHookChecked (src/dll/core/hook_install_check.h) instead:')
        for site in found:
            print('  ' + site)
        return 1
    print('raw MinHook calls: none under src/dll')
    return 0


if __name__ == '__main__':
    sys.exit(main())
