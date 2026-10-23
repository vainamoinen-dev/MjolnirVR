#!/usr/bin/env python3
"""Refactor guard: every LOG format string at a base commit still exists.

For each source file changed between BASE and the working tree (under src/),
collect the LOG(...) format strings it had at BASE (adjacent literals joined).
Every one must still appear, byte for byte, as a string literal somewhere in
src/ now (moved into a per-title row table is fine; reworded is not).

    python tools/check_log_literals.py --base <commit> [--paths GLOB ...]
    python tools/check_log_literals.py --base <commit> --whole-tree

--whole-tree compares the multiset of every LOG format string in src/ at BASE with the working
tree: same strings, same number of times, wherever they now live. A move of code between files
(E2) passes; a reworded, dropped or duplicated LOG line fails. It is run by hand for a refactor
round (its base is the round's start), not by ctest.

ctest runs it twice (muzzle/contact and weapon-haptic LOG lines), each with a fixed --base.
--map names a file of intended rewordings (build 31's docs/audit/LOG-STRING-MAP.txt): an old format
counts as preserved where its mapped new format is, and --whole-tree then holds only if the tree changed by
exactly the map's rows.

It also type-checks the muzzle log rows that reach LOG through a template
(and so lose the compiler's printf checking): MuzzleLogText binding/edge
lines take at most one %X (an RVA), the other five lines take no argument
(except the disable line, which takes exactly one %d when the table ends in
`true`, disableTakesStatus), and ReportMuzzleTrajectory formats are %d %d %llu %llu %llu %llu %d.
HookSetLogText rows (weapon-haptic hook sets) are five lines with no argument.

Exit 0 when all hold; 1 lists what failed; 77 (skip) when git, BASE or the
--map file is unavailable, e.g. a source export without history.
"""
import argparse
import collections
import fnmatch
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRING = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
SOURCE_SUFFIXES = {'.cpp', '.h', '.inl'}


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True,
                          text=True, encoding='utf-8', errors='replace').stdout


def strip_comments(text):
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif text[i] == '"':
            m = STRING.match(text, i)
            if not m:
                out.append(text[i]); i += 1; continue
            out.append(m.group(0)); i = m.end()
        elif text[i] == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1]); i = j + 1
        else:
            out.append(text[i]); i += 1
    return ''.join(out)


def joined_literals(text, start):
    """Adjacent string literals beginning at `start` (whitespace between)."""
    parts, i = [], start
    while True:
        while i < len(text) and text[i] in ' \t\r\n':
            i += 1
        m = STRING.match(text, i)
        if not m:
            return ''.join(parts), i
        parts.append(m.group(1)); i = m.end()


def log_formats(text):
    text = strip_comments(text)
    found = []
    for m in re.finditer(r'\bLOG\s*\(', text):
        literal, _ = joined_literals(text, m.end())
        if literal:
            found.append(literal)
    return found


def all_literals(text):
    text = strip_comments(text)
    found, i = set(), 0
    while True:
        m = STRING.search(text, i)
        if not m:
            return found
        literal, i = joined_literals(text, m.start())
        found.add(literal)


SPEC = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t|I64)?[diouxXeEfgGcspn]')
LOG_TEXT_FIELDS = ('binding', 'edge', 'create', 'enable', 'disable', 'busy', 'removal')
HOOK_SET_FIELDS = ('create', 'enable', 'disable', 'busy', 'removal')
TRAJECTORY_SPECS = ['%d', '%d', '%llu', '%llu', '%llu', '%llu', '%d']


def specs(fmt):
    return SPEC.findall(fmt.replace('%%', ''))


def check_muzzle_formats(text, path):
    """Type rules for formats routed through MuzzleLogText / HookSetLogText / ReportMuzzleTrajectory."""
    errors = []
    text = strip_comments(text)
    for m in re.finditer(r'\bHookSetLogText\s+\w+\s*\{', text):
        end = text.find('}', m.end())
        block = text[m.end():end]
        rows, i = [], 0
        while True:
            n = STRING.search(block, i)
            if not n:
                break
            literal, i = joined_literals(block, n.start())
            rows.append(literal)
        if len(rows) != len(HOOK_SET_FIELDS):
            errors.append(f'{path}: HookSetLogText has {len(rows)} lines, expected {len(HOOK_SET_FIELDS)}')
            continue
        # A trailing `true` (disableTakesStatus) makes the disable line take
        # the MinHook status: exactly one %d there, none elsewhere.
        status = re.search(r'\btrue\b', block[i:]) is not None
        for field, fmt in zip(HOOK_SET_FIELDS, rows):
            allowed = ['%d'] if status and field == 'disable' else []
            if specs(fmt) != allowed:
                errors.append(f'{path}: HookSetLogText.{field} "{fmt}" has {specs(fmt)}')
    for m in re.finditer(r'\bMuzzleLogText\s+\w+\s*\{', text):
        end = text.find('}', m.end())
        block = text[m.end():end]
        rows, i = [], 0
        while True:
            n = STRING.search(block, i)
            if not n:
                break
            literal, i = joined_literals(block, n.start())
            rows.append(literal)
        if len(rows) != len(LOG_TEXT_FIELDS):
            errors.append(f'{path}: MuzzleLogText has {len(rows)} lines, expected {len(LOG_TEXT_FIELDS)}')
            continue
        # A trailing `true` (disableTakesStatus) makes the disable line take
        # the MinHook status, as HookSetLogText's does.
        status = re.search(r'\btrue\b', block[i:]) is not None
        for field, fmt in zip(LOG_TEXT_FIELDS, rows):
            if field == 'disable':
                allowed = [['%d']] if status else [[]]
            else:
                allowed = [[], ['%X']] if field in ('binding', 'edge') else [[]]
            if specs(fmt) not in allowed:
                errors.append(f'{path}: MuzzleLogText.{field} "{fmt}" has {specs(fmt)}')
    for m in re.finditer(r'\bReportMuzzleTrajectory\s*<[^>]*>\s*\(', text):
        fmt, _ = joined_literals(text, m.end())
        if specs(fmt) != TRAJECTORY_SPECS:
            errors.append(f'{path}: trajectory format "{fmt}" has {specs(fmt)}')
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--base', required=True)
    parser.add_argument('--paths', nargs='*', default=[], help='only check changed files matching these globs')
    parser.add_argument('--whole-tree', action='store_true',
                        help='compare the multiset of LOG formats in all of src/ at --base with the working tree')
    parser.add_argument('--map', help='an old<TAB>new file of intended rewordings (docs/audit/LOG-STRING-MAP.txt; '
                                      'lines starting with "# " are notes): an old format counts as preserved when '
                                      'its new format is there instead')
    args = parser.parse_args()
    try:
        git('rev-parse', '--verify', f'{args.base}^{{commit}}')
    except (OSError, subprocess.CalledProcessError):
        print(f'SKIP: git or base {args.base} unavailable')
        return 77
    if args.map and not pathlib.Path(args.map).is_file():
        print(f'SKIP: map {args.map} unavailable')
        return 77
    rewording = load_map(args.map) if args.map else {}
    if args.whole_tree:
        return whole_tree(args.base, rewording)
    # Follow renames. A moved file has no path of its own at --base; read
    # its base text from the old path, and match the globs against either.
    changed = []
    for row in git('diff', '--name-status', '-M', '-l2000', args.base, '--', 'src').splitlines():
        fields = row.split('	')
        path, old_path = fields[-1], fields[1] if fields[0].startswith('R') else fields[-1]
        if pathlib.PurePosixPath(path).suffix in SOURCE_SUFFIXES and (
                not args.paths or any(fnmatch.fnmatch(p, g) for p in (path, old_path) for g in args.paths)):
            changed.append((path, old_path))
    expected = []
    for path, old_path in changed:
        try:
            old = git('show', f'{args.base}:{old_path}')
        except subprocess.CalledProcessError:
            continue  # new file
        expected += [(path, f) for f in log_formats(old)]
    present = set()
    for file in (ROOT / 'src').rglob('*'):
        if file.suffix in SOURCE_SUFFIXES:
            present |= all_literals(file.read_text(encoding='utf-8', errors='replace'))
    missing = [(p, f) for p, f in expected if f not in present and rewording.get(f) not in present]
    for path, fmt in missing:
        print(f'MISSING {path}: "{fmt}"')
    type_errors = []
    for file in (ROOT / 'src').rglob('*'):
        if file.suffix in SOURCE_SUFFIXES:
            type_errors += check_muzzle_formats(file.read_text(encoding='utf-8', errors='replace'),
                                                file.relative_to(ROOT).as_posix())
    for error in type_errors:
        print(f'FORMAT {error}')
    print(f'{len(expected) - len(missing)}/{len(expected)} LOG formats from {len(changed)} changed files preserved; '
          f'{len(type_errors)} muzzle format type errors')
    return 1 if missing or type_errors else 0


def load_map(path):
    """old -> new from an old<TAB>new[<TAB>flag] file; lines starting with '# ' are notes."""
    rewording = {}
    for line in pathlib.Path(path).read_text(encoding='utf-8').splitlines():
        if line and not line.startswith('# '):
            old, new = line.split('\t')[:2]
            assert old not in rewording, f'{path}: "{old}" mapped twice'
            rewording[old] = new
    return rewording


def whole_tree(base, rewording=None):
    """With a map, each base format is first replaced by its mapped rewording, so the multisets match only when
    the tree changed by exactly the map's rows."""
    rewording = rewording or {}
    before, after = collections.Counter(), collections.Counter()
    for path in git('ls-tree', '-r', '--name-only', base, '--', 'src').splitlines():
        if pathlib.PurePosixPath(path).suffix in SOURCE_SUFFIXES:
            before.update(rewording.get(f, f) for f in log_formats(git('show', f'{base}:{path}')))
    for file in (ROOT / 'src').rglob('*'):
        if file.suffix in SOURCE_SUFFIXES:
            after.update(log_formats(file.read_text(encoding='utf-8', errors='replace')))
    for fmt, n in sorted((before - after).items()):
        print(f'MISSING x{n}: "{fmt}"')
    for fmt, n in sorted((after - before).items()):
        print(f'NEW x{n}: "{fmt}"')
    same = not (before - after) and not (after - before)
    print(f'whole tree: {sum(before.values())} LOG formats at {base}, {sum(after.values())} now; '
          f'{"identical multiset" if same else "DIFFERENT"}')
    return 0 if same else 1


if __name__ == '__main__':
    sys.exit(main())
