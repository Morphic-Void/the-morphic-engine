# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

"""Exercise project include resolution against the built policy validator."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def check(binary, root, source, operand, errors=0, warnings=0, rule=None):
    path = root / source
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f'\n#include "{operand}"\n', encoding="utf-8")
    try:
        result = subprocess.run(
            [str(binary), "--root", str(root), "--no-report"],
            capture_output=True, text=True, timeout=30,
        )
        summary = f"{errors} error(s), {warnings} warning(s)"
        if (result.returncode != (1 if errors else 0) or summary not in result.stdout
                or (rule is not None and rule not in result.stdout)):
            raise AssertionError(f"{source}: {operand}\n{result.stdout}\n{result.stderr}")
    finally:
        path.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="morphic-policy-includes-") as directory:
        root = Path(directory)
        (root / "MorphicEngine.sln").touch()
        (root / "policy").mkdir()
        roots = ("core", "host", "executive", "rendering", "tests")
        policy = "version|1\n"
        for component in roots:
            (root / component).mkdir()
            scope = "tests" if component == "tests" else "engine"
            policy += f"scan|{component}|{scope}\n"
        policy += "project|fixture.vcxproj|engine\n"
        (root / "fixture.vcxproj").write_text(
            '<Project><ItemGroup><ProjectConfiguration Include="Debug|x64" />'
            '</ItemGroup><ItemDefinitionGroup><ClCompile>'
            '<LanguageStandard>stdcpp17</LanguageStandard>'
            '<ExceptionHandling>false</ExceptionHandling>'
            '</ClCompile></ItemDefinitionGroup></Project>\n', encoding="utf-8",
        )
        (root / "policy/morphic_policy.cfg").write_text(policy, encoding="utf-8")
        for header in (
            "core/debug/macros.hpp", "external/SuiteUTF/include/utf_toolkit.h",
            "host/module/types/local_type_ids.hpp",
            "executive/module/types/local_type_ids.hpp",
            "rendering/module/types/local_type_ids.hpp",
            "rendering/runtime/only_rendering.hpp",
            "host/runtime/local.hpp", "host/debug/macros.hpp",
        ):
            path = root / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()

        # Identically named catalogues in other modules must not be candidates.
        for component in ("host", "executive", "rendering"):
            check(binary, root, f"{component}/module/binding/probe.cpp",
                  "module/types/local_type_ids.hpp")
        check(binary, root, "rendering/runtime/probe.cpp", "runtime/only_rendering.hpp")
        check(binary, root, "host/runtime/probe.cpp", "local.hpp")
        check(binary, root, "rendering/runtime/probe.cpp", "debug/macros.hpp")
        check(binary, root, "host/runtime/probe.cpp", "debug/macros.hpp",
              warnings=1, rule="INC005")
        check(binary, root, "host/runtime/probe.cpp", "runtime/only_rendering.hpp",
              errors=1, rule="INC004")
        check(binary, root, "host/runtime/probe.cpp", "rendering/runtime/only_rendering.hpp")
        check(binary, root, "core/probe.cpp", "external/SuiteUTF/include/utf_toolkit.h")
        for component in ("core", "tests"):
            check(binary, root, f"{component}/probe.cpp", "module/types/local_type_ids.hpp",
                  errors=1, rule="INC004")
        check(binary, root, "rendering/runtime/probe.cpp", "runtime/missing.hpp",
              errors=1, rule="INC004")
    print("Policy include resolution: all 13 cases passed")


if __name__ == "__main__":
    main()
