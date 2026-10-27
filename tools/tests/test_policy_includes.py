# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

"""Exercise project include resolution against the built policy validator."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def check(binary, root, source, operand, errors=0, warnings=0, rule=None, opening='"', raw=False):
    path = root / source
    path.parent.mkdir(parents=True, exist_ok=True)
    closing = '>' if opening == '<' else '"'
    directive = operand if raw else f"{opening}{operand}{closing}"
    path.write_text(f'\n#include {directive}\n', encoding="utf-8")
    try:
        result = subprocess.run(
            [str(binary), "--root", str(root), "--no-report"],
            capture_output=True, text=True, timeout=30,
        )
        summary = f"MorphicPolicyValidator: {errors} error(s), {warnings} warning(s), 0 suppressed diagnostic(s)"
        if (result.returncode != (1 if errors else 0) or summary not in result.stdout.splitlines()
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
        policy += "include|cstdint|all|any|*|engine-wide\n"
        # A system-header permission must not override the core boundary.
        policy += "include|rendering/runtime/only_rendering.hpp|all|any|*|fixture\n"
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
            "tests/helper.hpp", "tools/helper.hpp", "audio/runtime/helper.hpp",
            "external/other/header.hpp", "external/SuiteUTF_extra/header.hpp",
            "core/shared.hpp", "shared.hpp",
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

        for operand in (
            "host/runtime/local.hpp", "executive/module/types/local_type_ids.hpp",
            "rendering/runtime/only_rendering.hpp", "tests/helper.hpp", "tools/helper.hpp",
            "audio/runtime/helper.hpp", "external/other/header.hpp",
            "external/SuiteUTF_extra/header.hpp", "../host/runtime/local.hpp",
            "../core/../host/runtime/local.hpp", "external/SuiteUTF/../other/header.hpp",
            r"host\runtime\local.hpp", str(root / "host/runtime/local.hpp"),
        ):
            check(binary, root, "core/probe.cpp", operand, errors=1, rule="INC002")
        for operand in ("debug/macros.hpp", "core/debug/macros.hpp",
                        "external/SuiteUTF/include/../include/utf_toolkit.h",
                        str(root / "core/debug/macros.hpp")):
            check(binary, root, "core/probe.cpp", operand)
        check(binary, root, "core/probe.def", "host/runtime/local.hpp", errors=1, rule="INC002")
        check(binary, root, "core/probe.cpp", "shared.hpp", errors=1, rule="INC002")
        check(binary, root, "core/probe.cpp", "cstdint", opening='<')
        check(binary, root, "core/probe.cpp", "rendering/runtime/only_rendering.hpp",
              errors=1, rule="INC002", opening='<')
        check(binary, root, "core/probe.cpp", "H", errors=1, rule="INC002", raw=True)
        check(binary, root, "host/runtime/probe.cpp", "HEADER_PATH", warnings=1, rule="INC102", raw=True)

        # Resolve aliases before admitting either core or the SuiteUTF exception.
        for alias in ("core/alias.hpp", "external/SuiteUTF/include/alias.hpp"):
            link = root / alias
            try:
                link.symlink_to(root / "host/runtime/local.hpp")
            except OSError as error:
                if sys.platform != "win32" or error.winerror != 1314:
                    raise
                print(f"Symlink fixture unavailable: {error}")
                continue
            try:
                check(binary, root, "core/probe.cpp", alias, errors=1, rule="INC002")
            finally:
                link.unlink()
    print("Policy include resolution and core boundary checks passed")


if __name__ == "__main__":
    main()
