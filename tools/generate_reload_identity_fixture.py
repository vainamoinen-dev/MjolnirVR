# Build-time fixture generator. CMake runs it to copy the reload model observers out of src/dll/core/game.cpp
# and src/dll/titles/h2/halo2_observer_6dof.cpp into a generated .inl that tests/reload_identity_tests.cpp
# compiles. It only reads the source tree and writes --output; it never edits source.
"""Compile the production optional model observers against bounded native fixtures."""
import argparse
from pathlib import Path
from generate_haptics_runtime_fixture import extract_function

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
definitions = [
    ('core/game.cpp', 'void LegacyObserveReloadModel(\n        GameTitle title, uint16_t renderModelTag, uint32_t generation,\n        const BoneMatrix* source, const int32_t* boneMap)'),
    ('titles/h2/halo2_observer_6dof.cpp', 'uint64_t Halo2ObserveReloadModel(uint32_t tag,uint32_t nodes,bool observePrimary=true) noexcept'),
]
output = ['// Extracted production bodies; never edit generated fixture.']
for name, signature in definitions:
    path = a.source_root.resolve()/'src/dll'/name
    body, line = extract_function(path.read_text(encoding='utf-8-sig'), signature)
    output.extend([f'#line {line} "{path.as_posix()}"', body])
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text('\n\n'.join(output)+'\n')
