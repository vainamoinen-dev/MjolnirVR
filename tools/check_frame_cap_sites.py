#!/usr/bin/env python3
"""Offline check of the six frame-cap bindings.

On MCC patch day, run it against the installed game files to see which
titles still match the frame-cap signatures, before anyone launches MCC:

    python tools/check_frame_cap_sites.py --mcc "C:/Program Files (x86)/Steam/steamapps/common/Halo The Master Chief Collection"

For each binding it prints `match` or `NOT matched: <first failing check>`.
It repeats the runtime resolvers in src/dll/core/game.cpp
(ResolveReachFrameCap, ResolveHalo4FrameCap, ResolveHalo3EngineFrameCap,
ResolveHalo2FrameCap, ResolveHalo2SecondLimiter): the image size, every
signature at its exact RVA, the rip-relative references that must agree,
and the constants those references must hold. The H2 option table must
hold the stock immediates (the mod patches them only in memory).

    python tools/check_frame_cap_sites.py --self-check

checks that every signature and RVA in this file appears in game.cpp (the
three retail image sizes in their render_logic.h headers), so one cannot be
edited without the other (ctest: halomccvr_frame_cap_sites_pin).
Exit 0 when everything holds, 1 otherwise.
"""
import argparse
import os
import re
import struct
import sys

REACH_SETTER = '83 25 ?? ?? ?? ?? 00 0F 57 C0 F3 0F 11 05 ?? ?? ?? ?? 48 8B 8C 24 90 01 00 00'
REACH_TABLE = ('B8 F0 00 00 00 89 05 ?? ?? ?? ?? EB 1D B8 B4 00 00 00 EB F1 B8 78 00 00 00 EB EA '
               'B8 3C 00 00 00 89 05 ?? ?? ?? ?? 85 C0 74 B2 F3 0F 10 0D ?? ?? ?? ?? 66 0F 6E C0 '
               '0F 5B C0 F3 0F 5E C8 F3 0F 11 0D ?? ?? ?? ??')
REACH_GATE = ('48 83 EC 28 45 33 D2 41 BB 01 00 00 00 44 39 15 ?? ?? ?? ?? 41 0F 94 C1 E8 ?? ?? ?? ?? '
              '84 C0 41 0F B6 C9 41 0F 44 CB 84 C9 75 12')
REACH_LIMITER = 'F3 0F 10 3D ?? ?? ?? ?? EB 08 F3 0F 10 3D ?? ?? ?? ?? E8'
H4_LIMITER = '84 C0 74 0B F3 44 0F 10 0D ?? ?? ?? ?? EB 09 F3 44 0F 10 0D ?? ?? ?? ?? E8'
H3_SETTER = ('83 25 ?? ?? ?? ?? 00 EB 4C B8 F0 00 00 00 89 05 ?? ?? ?? ?? F3 0F 10 0D ?? ?? ?? ?? '
             '66 0F 6E C0 0F 5B C0 F3 0F 5E C8 F3 0F 11 0D ?? ?? ?? ??')
H3_PACKET = ('44 89 25 ?? ?? ?? ?? EB 48 B8 F0 00 00 00 89 05 ?? ?? ?? ?? 66 0F 6E C0 0F 5B C0 '
             'F3 0F 10 0D ?? ?? ?? ?? F3 0F 5E C8 F3 0F 11 0D ?? ?? ?? ??')
H3_LIMITER = ('84 C0 74 0A F3 0F 10 3D ?? ?? ?? ?? EB 08 F3 0F 10 3D ?? ?? ?? ?? 83 3D ?? ?? ?? ?? 00 '
              '41 0F 94 C1 E8')
H2_UNPACK = '40 55 53 48 8D AC 24 F8 FE FF FF 48 81 EC 08 02 00 00 48 8B D9 48 8D 4C 24 30 E8'
H2_TABLE = ('85 C9 74 2B 83 E9 01 74 1F 83 E9 01 74 13 83 E9 01 74 07 83 E9 01 33 C0 EB 1A '
            'B8 F0 00 00 00 EB 13 B8 B4 00 00 00 EB 0C B8 78 00 00 00 EB 05 B8 3C 00 00 00 '
            '0F B6 4C 24 46 84 C9 89 43 54')
H2_POLL = ('E8 ?? ?? ?? ?? 4C 8B 05 ?? ?? ?? ?? 41 0F 10 80 C0 00 00 00 41 0F 10 88 D0 00 00 00 '
           '41 0F 10 90 B0 00 00 00')
H2_LIMITER = ('48 8B 05 ?? ?? ?? ?? F3 44 0F 10 15 ?? ?? ?? ?? F2 41 0F 5E C0 8B 88 04 01 00 00 '
              '41 0F 28 D2 66 0F 5A C8 85 C9 7E 07 66 0F 6E D1 0F 5B D2')
H2_SETTER2 = ('0F B6 44 24 45 3C 04 8B C8 0F 95 05 ?? ?? ?? ?? 84 C0 74 32 83 E9 01 74 23 83 E9 01 '
              '74 14 83 E9 01 74 05 0F 57 C0 EB 26 F3 0F 10 05 ?? ?? ?? ?? EB 1C F3 0F 10 05 ?? ?? ?? ?? '
              'EB 12 F3 0F 10 05 ?? ?? ?? ?? EB 08 F3 0F 10 05 ?? ?? ?? ?? 80 7C 24 39 02 '
              'F3 0F 11 05 ?? ?? ?? ??')
H2_LIMITER2 = ('80 3D ?? ?? ?? ?? 00 0F 84 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? '
               'E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? 0F 2F C6 0F 86')

SIGNATURES = (REACH_SETTER, REACH_TABLE, REACH_GATE, REACH_LIMITER, H4_LIMITER, H3_SETTER, H3_PACKET,
              H3_LIMITER, H2_UNPACK, H2_TABLE, H2_POLL, H2_LIMITER, H2_SETTER2, H2_LIMITER2)
# Image sizes and RVAs, spelled as game.cpp spells them (the self-check looks for these).
REACH_SIZE, H4_SIZE, H2_SIZE = 0x04EDA000, 0x04A3F000, 0x02A38000
H3_SITES = ('Halo 3', 'halo3', '0x04768000', '0x9C23', '0xB70B', '0xB0F5D')
ODST_SITES = ('ODST', 'halo3odst', '0x04797000', '0x9F43', '0xBD31', '0xBE5A1')
RVA_SPELLINGS = ('0xD5D5', '0xD600', '0x328CC', '0x322F5', '0x3DAC5', '0x3DAF0', '0x5F3C4', '0x5EF10',
                 '0x53090', '0x53238', '0x53BAA', '0x2B634D', '0x3CD05', '0x6BFA53',
                 '0x04EDA000', '0x04A3F000', '0x02A38000') + H3_SITES[2:] + ODST_SITES[2:]


class Image:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        pe = struct.unpack_from('<I', self.data, 0x3C)[0]
        count = struct.unpack_from('<H', self.data, pe + 6)[0]
        optional = struct.unpack_from('<H', self.data, pe + 20)[0]
        self.size = struct.unpack_from('<I', self.data, pe + 24 + 56)[0]
        self.sections = []
        for i in range(count):
            o = pe + 24 + optional + 40 * i
            vsize, va, rsize, raw = struct.unpack_from('<IIII', self.data, o + 8)
            chars = struct.unpack_from('<I', self.data, o + 36)[0]
            self.sections.append((va, max(vsize, rsize), raw, rsize, chars))

    def offset(self, rva, length=1):
        for va, vsize, raw, rsize, _ in self.sections:
            if va <= rva and rva + length <= va + min(vsize, rsize):
                return raw + rva - va
        return None

    def code(self, rva):
        return any(va <= rva < va + vsize and chars & 0x20000000 for va, vsize, _, _, chars in self.sections)

    def at(self, rva, signature):
        tokens = signature.split()
        f = self.offset(rva, len(tokens))
        if f is None or not self.code(rva):
            return False
        return all(t == '??' or self.data[f + i] == int(t, 16) for i, t in enumerate(tokens))

    def rip(self, disp_rva, next_rva):
        f = self.offset(disp_rva, 4)
        return None if f is None else next_rva + struct.unpack_from('<i', self.data, f)[0]

    def f32(self, rva):
        f = self.offset(rva, 4)
        return None if f is None else struct.unpack_from('<f', self.data, f)[0]

    def u8(self, rva):
        f = self.offset(rva, 1)
        return None if f is None else self.data[f]


class Fail(Exception):
    pass


def need(ok, what):
    if not ok:
        raise Fail(what)


def sig(img, rva, signature, name):
    need(img.at(rva, signature), f'{name} signature at +{rva:X}')


def reach_like(img, size, setter, table, gate, limiter, limiter_sig, limiter_disp, adjacent):
    need(img.size == size, f'image size {img.size:#x} != {size:#x}')
    sig(img, setter, REACH_SETTER, 'option setter')
    sig(img, table, REACH_TABLE, 'option table')
    sig(img, gate, REACH_GATE, 'limiter gate')
    sig(img, limiter, limiter_sig, 'limiter target load')
    cap = img.rip(setter + 2, setter + 7)
    target = img.rip(setter + 14, setter + 18)
    need(cap and cap == img.rip(gate + 16, gate + 20) == img.rip(table + 0x22, table + 0x26),
         'setter, table and gate disagree on the cap')
    need(target and target == img.rip(limiter + limiter_disp, limiter + limiter_disp + 4) ==
         img.rip(table + 0x41, table + 0x45), 'setter, table and limiter disagree on the target')
    if adjacent:
        need(target + 4 == cap, 'target is not cap - 4')
    need(0 <= cap and cap + 4 <= img.size and 0 <= target and target + 4 <= img.size, 'pair outside the module')


def halo3_engine(img, size, s, b, l):
    need(img.size == size, f'image size {img.size:#x} != {size:#x}')
    sig(img, s, H3_SETTER, 'option setter')
    sig(img, b, H3_PACKET, 'settings packet')
    sig(img, l, H3_LIMITER, 'limiter')
    caps = [img.rip(s + 2, s + 7), img.rip(s + 16, s + 20), img.rip(b + 3, b + 7), img.rip(b + 16, b + 20),
            img.rip(l + 24, l + 29)]
    targets = [img.rip(s + 43, s + 47), img.rip(b + 43, b + 47), img.rip(l + 8, l + 12)]
    need(caps[0] and len(set(caps)) == 1, 'the five cap references disagree')
    need(all(t == caps[0] + 4 for t in targets), 'a target reference is not cap + 4')
    need(caps[0] + 8 <= img.size, 'pair outside the module')
    for disp in (img.rip(s + 24, s + 28), img.rip(b + 31, b + 35)):
        need(img.f32(disp) == 1.0, 'a setter does not divide 1.0')


def halo2_first(img):
    need(img.size == H2_SIZE, f'image size {img.size:#x} != {H2_SIZE:#x}')
    sig(img, 0x53090, H2_UNPACK, 'option unpack')
    sig(img, 0x53238, H2_TABLE, 'option table')
    sig(img, 0x53BAA, H2_POLL, 'settings poller call')
    sig(img, 0x2B634D, H2_LIMITER, 'limiter')
    need(img.rip(0x53BAA + 1, 0x53BAA + 5) == 0x53090, 'the poller does not call the unpack')
    g = img.rip(0x53BAA + 8, 0x53BAA + 12)
    need(g and g == img.rip(0x2B634D + 3, 0x2B634D + 7), 'poller and limiter disagree on the settings global')
    need(img.f32(img.rip(0x2B634D + 12, 0x2B634D + 16)) == 1000.0, 'the "unlimited" constant is not 1000.0')
    low = [img.u8(0x53238 + i) for i in (27, 34, 41, 48)]
    need(low == [0xF0, 0xB4, 0x78, 0x3C], f'option table immediates {low} are not stock')


def halo2_second(img):
    s, l = 0x3CD05, 0x6BFA53
    need(img.size == H2_SIZE, f'image size {img.size:#x} != {H2_SIZE:#x}')
    sig(img, s, H2_SETTER2, 'second setter')
    sig(img, l, H2_LIMITER2, 'second limiter')
    flag, time = img.rip(s + 12, s + 16), img.rip(s + 87, s + 91)
    need(flag and flag == img.rip(l + 2, l + 7), 'setter and limiter disagree on the flag')
    need(time and time == img.rip(l + 43, l + 47), 'setter and limiter disagree on the frame time')
    for disp, fps in zip((44, 54, 64, 74), (240, 180, 120, 60)):
        value = img.f32(img.rip(s + disp, s + disp + 4))
        need(value is not None and abs(value - 1.0 / fps) <= 1e-9, f'frame-time constant for {fps} fps')


CHECKS = (
    ('Reach', 'haloreach', lambda i: reach_like(i, REACH_SIZE, 0xD5D5, 0xD600, 0x328CC, 0x322F5, REACH_LIMITER, 4, True)),
    ('Halo 4', 'halo4', lambda i: reach_like(i, H4_SIZE, 0x3DAC5, 0x3DAF0, 0x5F3C4, 0x5EF10, H4_LIMITER, 9, False)),
    ('Halo 3', 'halo3', lambda i: halo3_engine(i, 0x04768000, 0x9C23, 0xB70B, 0xB0F5D)),
    ('ODST', 'halo3odst', lambda i: halo3_engine(i, 0x04797000, 0x9F43, 0xBD31, 0xBE5A1)),
    ('Halo 2 limiter 1', 'halo2', halo2_first),
    ('Halo 2 limiter 2', 'halo2', halo2_second),
)


def run(mcc):
    failures = 0
    images = {}
    for name, folder, check in CHECKS:
        path = os.path.join(mcc, folder, folder + '.dll')
        try:
            if folder not in images:
                images[folder] = Image(path)
            check(images[folder])
            print(f'{name}: match')
        except Fail as e:
            failures += 1
            print(f'{name}: NOT matched: {e}')
        except OSError as e:
            failures += 1
            print(f'{name}: NOT matched: cannot read {path} ({e.strerror})')
    return failures


def self_check(root):
    with open(os.path.join(root, 'src', 'dll', 'core', 'game.cpp'), encoding='utf-8', errors='replace') as f:
        text = f.read()
    for title in ('reach', 'h4', 'h2'):
        name = {'reach': 'reach', 'h4': 'halo4', 'h2': 'halo2'}[title] + '_render_logic.h'
        with open(os.path.join(root, 'src', 'common', 'titles', title, name), encoding='utf-8', errors='replace') as f:
            text += f.read()
    # Join adjacent string literals ("a " "b"), as the compiler does.
    literals = ' '.join(re.sub(r'"\s*"', '', m) for m in re.findall(r'"(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")*', text))
    normalized = ' '.join(literals.replace('"', ' ').split())
    missing = [s for s in SIGNATURES if ' '.join(s.split()) not in normalized]
    missing += [r for r in RVA_SPELLINGS if not re.search(r'\b' + re.escape(r) + r'\b', text)]
    for item in missing:
        print('NOT in game.cpp:', item[:80])
    print(f'{len(SIGNATURES) - sum(m in SIGNATURES for m in missing)}/{len(SIGNATURES)} signatures and '
          f'{len(RVA_SPELLINGS) - sum(m in RVA_SPELLINGS for m in missing)}/{len(RVA_SPELLINGS)} RVAs/sizes found in game.cpp')
    return len(missing)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--mcc', help='MCC install directory')
    parser.add_argument('--self-check', action='store_true', help='check this file against game.cpp')
    args = parser.parse_args()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if args.self_check:
        return 1 if self_check(root) else 0
    if not args.mcc:
        parser.error('--mcc or --self-check is required')
    return 1 if run(args.mcc) else 0


if __name__ == '__main__':
    sys.exit(main())
