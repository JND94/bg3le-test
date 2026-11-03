#!/usr/bin/env python3
"""Compile the vendored InputText methods against real headless ImGui.

Run after fetching externals: python3 tools/test-imgui-input-reload.py
Only small adapters around the actual callback/StyledRender/SetText are faked;
the test activates a real ImGui input before replacing its external text.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=root / "vendor/bg3se/BG3Extender/Extender/Client/IMGUI/IMGUI.cpp")
parser.add_argument("--imgui-dir", type=Path, default=root / "external/third_party/imgui")
parser.add_argument("--case", action="append", help="Run only the named scenario (repeatable)")
args = parser.parse_args()
source = args.source.read_text()
callback = source[source.index("int InputTextCallback("):source.index("STDString InputText::GetText()")]
setter_start = source.index("void InputText::SetText(")
setter = source[setter_start:source.index("void Combo::StyledRender(", setter_start)]
assert callback.count("void InputText::StyledRender(") == 1
build = root / "build"
build.mkdir(exist_ok=True)
cases = args.case or (
    "readonly-same", "readonly-other", "readonly-inherited-same", "readonly-inherited-other",
    "readonly-selection", "readonly-selection-other", "readonly-selection-empty",
    "readonly-selection-utf8-copy", "readonly-selection-nul-copy",
    "editable-same", "editable-other", "readonly-no-state", "editable-no-state",
    "readonly-no-reload", "editable-no-reload", "editable-resize",
    "editable-to-readonly", "readonly-to-editable-other",
)
with tempfile.TemporaryDirectory(prefix="input-reload-", dir=build) as tmp:
    work = Path(tmp)
    (work / "input-text-under-test.inc").write_text("namespace bg3se::extui {\n" + callback + setter + "\n}\n")
    command = shlex.split(os.environ.get("CXX", "c++")) + [
        "-std=c++17", "-O1", "-UNDEBUG", "-I" + str(args.imgui_dir), "-I" + str(work),
        str(root / "tools/imgui-input-reload-test.cpp"),
    ]
    command += [str(args.imgui_dir / name) for name in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp")]
    executable = work / "input-reload"
    subprocess.run(command + ["-o", str(executable)], check=True)
    failed = 0
    for case in cases:
        result = subprocess.run([str(executable), case], capture_output=True, text=True, timeout=15)
        ok = result.returncode == 0
        print(f"{'PASS' if ok else 'FAIL'} {case} (exit {result.returncode})", flush=True)
        if not ok:
            failed += 1
            print(result.stderr.strip(), flush=True)
    print(f"{len(cases) - failed}/{len(cases)} headless ImGui cases passed")
    raise SystemExit(1 if failed else 0)
