# Build-time fixture generator. CMake runs it to copy the aim structs and stock/controller solvers out of
# src/dll/core/vr.cpp and virtual_stock_aim.inl into a generated .inl that the virtual stock aim tests compile.
# It only reads the source tree and writes --output; it never edits source.
"""Extract the shipping stock/controller aim solvers for geometry regression tests."""
import argparse
import re
from pathlib import Path
from generate_haptics_runtime_fixture import extract_function


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.source_root.resolve()
    source_path = root / "src/dll/core/vr.cpp"
    source = source_path.read_text(encoding="utf-8-sig")
    stock_path = root / "src/dll/core/virtual_stock_aim.inl"
    stock = stock_path.read_text(encoding="utf-8-sig")
    internal_path = root / "src/dll/core/vr_internal.h"
    internal = internal_path.read_text(encoding="utf-8-sig")
    controllers_path = root / "src/dll/core/vr_controllers.cpp"
    controllers = controllers_path.read_text(encoding="utf-8-sig")
    helpers_path = root / "src/dll/core/vr_format_helpers.h"
    # vr_format_helpers.h marks its helpers `inline`; the fixture compiles them as plain functions, as before.
    helpers = re.sub(r"(?m)^(\s*)inline ", r"\1", helpers_path.read_text(encoding="utf-8-sig"))
    output = ["// Generated from current production source. Do not edit."]
    for text, path, signature in (
        (internal, internal_path, "struct AimPoseInputs"), (internal, internal_path, "struct AimPoseResult"),
        (helpers, helpers_path, "XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v)"),
    ):
        body, line = extract_function(text, signature)
        output += [f'#line {line} "{path.as_posix()}"', body + (";" if signature.startswith("struct ") else "")]
    for text, path, signature in (
        (stock, stock_path,
         "template <bool CaptureTrace>\n"
         "    AimPoseResult ComputeAimPoseImpl(\n"
         "        const AimPoseInputs& inputs, AimPoseTrace* trace) noexcept"),
        (stock, stock_path, "AimPoseResult ComputeStockAimPose(const AimPoseInputs& inputs) noexcept"),
        (controllers, controllers_path, "AimPoseResult ComputeAimPose(const AimPoseInputs& inputs) noexcept"),
    ):
        body, line = extract_function(text, signature)
        output += [f'#line {line} "{path.as_posix()}"', body]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n\n".join(output) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
