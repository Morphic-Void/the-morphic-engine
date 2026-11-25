# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

"""Regression fixtures for policy configuration, vendor boundaries and macro allocation."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


PROJECT = """<Project>
<ItemGroup><ProjectConfiguration Include="Debug|x64" /></ItemGroup>
<ItemDefinitionGroup><ClCompile><LanguageStandard>stdcpp17</LanguageStandard>
<ExceptionHandling>false</ExceptionHandling></ClCompile></ItemDefinitionGroup>
</Project>
"""
POLICY = """version|1
scan|foundation|engine
scan|framework|engine
scan|host|engine
scan|tests|tests
project|fixture.vcxproj|engine
"""


def write(root, relative, text):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    return path


def check(binary, label, source="int value;\n", extra="", *, path="host/probe.cpp",
          policy=POLICY, files=None, errors=0, warnings=0, suppressed=0, exit_code=None,
          rules=(), setup=None, root_suffix=""):
    with tempfile.TemporaryDirectory(prefix="morphic-policy-audit-") as directory:
        root = Path(directory)
        for component in ("foundation", "framework", "host", "tests"):
            (root / component).mkdir()
        write(root, "MorphicEngine.sln", "")
        write(root, "fixture.vcxproj", PROJECT)
        write(root, "policy/morphic_policy.cfg", policy + extra)
        write(root, path, source)
        for name, text in (files or {}).items():
            write(root, name, text)
        if setup:
            try:
                setup(root)
            except OSError as error:
                if sys.platform != "win32" or error.winerror != 1314:
                    raise
                print(f"SKIP {label}: symbolic-link privilege unavailable")
                return
        result = subprocess.run([str(binary), "--root", str(root) + root_suffix, "--no-report"],
                                capture_output=True, text=True, timeout=30)
        expected_exit = exit_code if exit_code is not None else int(errors > 0)
        summary = (f"MorphicPolicyValidator: {errors} error(s), {warnings} warning(s), "
                   f"{suppressed} suppressed diagnostic(s)")
        output = result.stdout + result.stderr
        if (result.returncode != expected_exit
                or (expected_exit < 2 and summary not in result.stdout.splitlines())
                or any(rule not in output for rule in rules)):
            raise AssertionError(f"{label}: exit {result.returncode}, expected {expected_exit}\n{output}")
        print(f"PASS {label}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    binary = parser.parse_args().binary.resolve(strict=True)

    # Token ownership is confined to the two implementation layers. Permissions
    # are exact test files, not general application or fixture grandfathering.
    for source in (
        "memory::CMemoryToken owner;\n",
        "using Owner = memory::CMemoryToken;\n",
        "using memory::CMemoryToken;\n",
        "void inspect(const memory::CMemoryToken&);\n",
        "TSlots<memory::CMemoryToken> owners;\n",
        "#define OWNER memory::CMemoryToken\n",
        "#define OWNER() memory::CMemoryToken{}\n",
        "#define OWNER() memory::\\\nCMemoryToken{}\n",
        "#if 0\nmemory::CMemoryToken owner;\n#endif\n",
    ):
        check(binary, "token use outside implementation layers", source, errors=1, rules=("MEM004",))
    for path in ("foundation/probe.hpp", "framework/probe.hpp"):
        check(binary, "token implementation layer", "memory::CMemoryToken owner;\n", path=path)
    for path in ("tests/probe.cpp", "host/framework/probe.cpp", "framework_extra/probe.cpp"):
        extra_scan = "scan|framework_extra|engine\n" if path.startswith("framework_extra/") else ""
        check(binary, "token layer prefix boundary", "CMemoryToken owner;\n", extra=extra_scan,
              path=path, errors=1, rules=("MEM004",))
    check(binary, "token literals and longer names",
          '// CMemoryToken\nconst char* name = "CMemoryToken";\n'
          'const char* raw = R"(CMemoryToken)";\nstruct CMemoryTokenProbe {};\n')
    permission = "memory-token|tests/probe.cpp|token-ownership-test\n"
    check(binary, "exact token test permission", "CMemoryToken owner;\n", permission, path="tests/probe.cpp")
    check(binary, "token macro uses test permission", "#define OWNER CMemoryToken\n", permission, path="tests/probe.cpp")
    check(binary, "unused token permission", 'const char* name = "CMemoryToken";\n', permission,
          path="tests/probe.cpp", warnings=1, rules=("CFG101",))
    check(binary, "token permission is not a directory exemption", "CMemoryToken owner;\n", permission,
          path="tests/other.cpp", files={"tests/probe.cpp": "CMemoryToken owner;\n"}, errors=1, rules=("MEM004",))
    check(binary, "token permission cannot exempt application code", "CMemoryToken owner;\n",
          "memory-token|host/probe.cpp|not-a-test\n", errors=1, rules=("CFG001",))
    check(binary, "token permission requires a scanned test", extra="memory-token|tools/probe.cpp|test\n",
          files={"tools/probe.cpp": "CMemoryToken owner;\n"}, errors=1, rules=("CFG001",))
    check(binary, "token permission requires an existing file", extra="memory-token|tests/missing.cpp|test\n",
          errors=1, rules=("CFG001",))
    for record in ("memory-token|tests/probe.cpp", "memory-token|tests/probe.cpp|",
                   "memory-token|../probe.cpp|test", "memory-token|tests/|test", "memory-token|*|test"):
        check(binary, "invalid token permission", extra=record + "\n", exit_code=2)
    check(binary, "duplicate token permission reasons", extra=permission +
          "memory-token|tests/probe.cpp|another-reason\n", exit_code=2)
    for source in ("CMemoryToken owner;\n", "#define OWNER CMemoryToken\n"):
        check(binary, "narrow token suppression",
              '// morphic-policy: suppress-next-line MEM004 reason="negative ownership test"\n' + source,
              suppressed=1, rules=("MEM004",))
    check(binary, "stale token suppression",
          '// morphic-policy: suppress-next-line MEM004 reason="negative ownership test"\nint value;\n',
          errors=1, rules=("SUP001",))

    # Schema errors are policy-loading failures, distinct from source violations.
    invalid_records = (
        "version|1", "version|2", "scan|host|engnie", "scan|host/|engine", "project|fixture.vcxproj|toool",
        "include|vector|engnie|any|*|reason", "include|vector|engine|excat|host/probe.cpp|reason",
        "include|vector|engine|any|host/|reason", "include|vector|engine|prefix|host|reason",
        "include|vector|engine|exact|../escape.cpp|reason",
        "include|vector|engine|exact|host/../host/probe.cpp|reason",
        "include|vector|engine|exact|host\\probe.cpp|reason",
        "include|vector|engine|exact|host/probe.cpp|", "include|vector",
        "macro|BAD-NAME|GID002|exact|host/probe.cpp",
        "macro|BIND|GID999|exact|host/probe.cpp",
        "placement|/absolute.cpp", "allocation|C:/absolute.cpp",
        "project|fixture.vcxproj|tool", "unknown|record",
    )
    for record in invalid_records:
        check(binary, "invalid " + record, extra=record + "\n", exit_code=2,
              rules=("MorphicPolicyValidator:",))
    check(binary, "short malformed duplicate", extra="include|vector\ninclude|vector\n", exit_code=2)
    check(binary, "duplicate permission with different reason", extra=(
        "include|vector|engine|exact|host/probe.cpp|first\n"
        "include|vector|engine|exact|host/probe.cpp|second\n"), exit_code=2)
    check(binary, "duplicate allocation permission", extra="allocation|host/probe.cpp\nallocation|host/probe.cpp\n", exit_code=2)
    check(binary, "conflicting macro rule", extra=(
        "macro|BIND|GID001|exact|host/probe.cpp\n"
        "macro|BIND|GID002|exact|host/probe.cpp\n"), exit_code=2)

    for record in ("placement|host/missing.cpp", "allocation|host/missing.cpp",
                   "include|vector|engine|exact|host/missing.cpp|reason",
                   "include|vector|engine|prefix|host/missing/|reason",
                   "macro|BIND|GID002|exact|host/missing.cpp",
                   "project|missing.vcxproj|engine"):
        check(binary, "missing " + record, extra=record + "\n", errors=1, rules=("CFG001",))
    check(binary, "missing source root", extra="scan|missing|engine\n", errors=1, rules=("SRC001",))
    check(binary, "overlapping source roots", extra="scan|host/runtime|tests\n",
          files={"host/runtime/helper.hpp": ""}, errors=1, rules=("CFG001",))
    check(binary, "wrong permission scope", extra="include|vector|tests|exact|host/probe.cpp|reason\n",
          errors=1, rules=("CFG001",))
    check(binary, "unscanned permission target", extra="placement|tools/helper.cpp\n",
          files={"tools/helper.cpp": ""}, errors=1, rules=("CFG001",))
    check(binary, "non-source permission target", extra="allocation|host/helper.txt\n",
          files={"host/helper.txt": ""}, errors=1, rules=("CFG001",))

    for record in ("include|vector|engine|exact|host/probe.cpp|reason",
                   "include|vector|engine|prefix|host/|reason", "placement|host/probe.cpp",
                   "allocation|host/probe.cpp", "macro|BIND|GID002|exact|host/probe.cpp"):
        check(binary, "unused " + record, extra=record + "\n", warnings=1, rules=("CFG101",))
    check(binary, "unused standing header permission", extra="include|tuple|all|any|*|engine-wide\n")
    check(binary, "repository root trailing separator", root_suffix="/")
    check(binary, "inactive include counts as use", "#if 0\n#include <vector>\n#endif\n",
          "include|vector|engine|exact|host/probe.cpp|reason\n")
    check(binary, "redundant include", "#include <vector>\n", extra=(
        "include|vector|all|any|*|standing\n"
        "include|vector|engine|exact|host/probe.cpp|narrow\n"), warnings=1, rules=("CFG102",))
    check(binary, "redundant macro", "BIND(Value);\n", extra=(
        "macro|BIND|GID002|prefix|host/\nmacro|BIND|GID002|exact|host/probe.cpp\n"),
        warnings=1, rules=("CFG102",))

    vendor = {"external/vendor/header.hpp": ""}
    permit = "include|external/vendor/header.hpp|engine|exact|host/probe.cpp|vendor-adapter\n"
    for operand in ('"external/vendor/header.hpp"', '<external/vendor/header.hpp>',
                    '"../external/vendor/header.hpp"', '"external/vendor/../vendor/header.hpp"',
                    '"external\\vendor\\header.hpp"'):
        check(binary, "unapproved vendor " + operand, f"#include {operand}\n",
              files=vendor, errors=1, rules=("INC001",))
        check(binary, "approved vendor " + operand, f"#include {operand}\n", permit, files=vendor)
    check(binary, "vendor cannot enter Framework", '#include "external/vendor/header.hpp"\n',
          permit.replace("host/probe.cpp", "framework/probe.cpp"), path="framework/probe.cpp",
          files=vendor, errors=1, warnings=1, rules=("INC006", "CFG101"))
    check(binary, "SuiteUTF remains allowed", '#include "external/SuiteUTF/include/header.hpp"\n',
          files={"external/SuiteUTF/include/header.hpp": ""})
    check(binary, "SuiteUTF sibling not exempt", '#include "external/SuiteUTF_extra/header.hpp"\n',
          files={"external/SuiteUTF_extra/header.hpp": ""}, errors=1, rules=("INC001",))
    check(binary, "vendor alias", '#include "host/alias.hpp"\n', files=vendor,
          setup=lambda root: (root / "host/alias.hpp").symlink_to(root / "external/vendor/header.hpp"),
          errors=1, rules=("INC001",))
    check(binary, "approved vendor alias", '#include "host/alias.hpp"\n', permit, files=vendor,
          setup=lambda root: (root / "host/alias.hpp").symlink_to(root / "external/vendor/header.hpp"))
    check(binary, "configured target escape", extra="allocation|host/alias.cpp\n",
          setup=lambda root: (root / "host/alias.cpp").symlink_to(binary), errors=1, rules=("CFG001",))

    check(binary, "unclassified root project", files={"other.vcxproj": PROJECT}, errors=1, rules=("PRJ004",))
    check(binary, "unclassified Visual Studio project", files={"visual_studio/Other.vcxproj": PROJECT},
          errors=1, rules=("PRJ004",))
    solution = 'Project("{GUID}") = "Other", "components\\Other.vcxproj", "{GUID}"\nEndProject\n'
    check(binary, "unclassified solution project", files={"MorphicEngine.sln": solution,
          "components/Other.vcxproj": PROJECT}, errors=1, rules=("PRJ004",))
    check(binary, "solution path cannot hide behind external prefix", files={"MorphicEngine.sln": solution.replace(
          "components\\Other", "external\\..\\components\\Other"), "components/Other.vcxproj": PROJECT},
          errors=1, rules=("PRJ004",))
    check(binary, "classified project settings checked", extra="project|visual_studio/Other.vcxproj|engine\n",
          files={"visual_studio/Other.vcxproj": PROJECT.replace("false", "Sync")}, errors=1, rules=("PRJ002",))
    check(binary, "external solution project exempt", files={"MorphicEngine.sln": solution.replace(
          "components\\Other", "external\\vendor\\Other"), "external/vendor/Other.vcxproj": PROJECT})

    for name, source in (
        ("direct", "int* value = new int;\n"),
        ("object macro", "#define MAKE new int\n"),
        ("parenthesized object macro", "#define MAKE (new int)\n"),
        ("function macro", "#define MAKE() new int\n"),
        ("continued macro", "#define MAKE() \\\n  new int\n"),
        ("inactive macro", "#if 0\n#define MAKE() new int\n#endif\n"),
        ("nothrow macro", "#define MAKE() new (std::nothrow) int\n"),
    ):
        check(binary, name, source, errors=1, rules=("MEM001",))
    check(binary, "macro locations", "\n  #define MAKE() \\\n    new int\n", errors=1, rules=("probe.cpp(3,5): error MEM001",))
    check(binary, "macro comments and literals", '#define TEXT "new int malloc"\n#define RAW R"(new int)"\n'
          '#define MORE 1 /* new int */\n#define LAST 2 // malloc(10)\n')
    check(binary, "allocator redirection is not a call", "#define malloc engine_allocate\n#define calloc(n, s) engine_zero(n, s)\n")
    check(binary, "macro raw allocation warning", "#define MAKE(n) malloc(n)\n", warnings=1, rules=("MEM003",))
    check(binary, "approved macro raw allocation", "#define MAKE(n) malloc(n)\n", "allocation|host/probe.cpp\n")
    check(binary, "macro operator new warning", "#define MAKE(n) ::operator new(n)\n", warnings=1, rules=("MEM003",))
    check(binary, "continued macro operator new", "#define MAKE(n) ::operator \\\nnew(n)\n", warnings=1, rules=("MEM003",))
    check(binary, "placement macro warning", "#define MAKE(p) new (p) Value\n", warnings=1, rules=("MEM002",))
    for source in ("#include <new>\n#define MAKE(p) new (p) Value\n",
                   "#define MAKE(p) new (p) Value\n#include <new>\n"):
        check(binary, "placement macro supports new header", source,
              "placement|host/probe.cpp\ninclude|new|engine|exact|host/probe.cpp|placement\n")
    check(binary, "macro suppression", '// morphic-policy: suppress-next-line MEM001 reason="fixture"\n'
          '#define MAKE() new int\n', suppressed=1, rules=("suppressed MEM001",))
    check(binary, "stale macro suppression", '// morphic-policy: suppress-next-line MEM001 reason="fixture"\n'
          '#define MAKE() 42\n', errors=1, rules=("SUP001",))
    check(binary, "include suppression", '// morphic-policy: suppress-next-line INC001 reason="fixture"\n'
          '#include <vector>\n', suppressed=1, rules=("suppressed INC001",))

    # Review regressions: preprocessing spelling must neither bypass checks nor
    # turn comments and literal contents into code or evidence of permission use.
    for source in ('#/**/include "external/vendor/header.hpp"\n',
                   '/* comment */ #include "external/vendor/header.hpp"\n',
                   '#include/**/ "external/vendor/header.hpp"\n',
                   '#inc\\\nlude "external/vendor/header.hpp"\n',
                   '#include /* multi\nline */ "external/vendor/header.hpp"\n'):
        check(binary, "directive spelling denied", source, files=vendor, errors=1, rules=("INC001",))
        check(binary, "directive spelling approved", source, permit, files=vendor)
    for source in ('#de\\\nfine MAKE() new int\n', '#define MAKE() ne\\\nw int\n',
                   '#define MAKE() ne\\\r\nw int\r\n', '#define MAKE() /* multi\nline */ new int\n'):
        check(binary, "continued allocation tokens", source, errors=1, rules=("MEM001",))
    check(binary, "continued comment", '#define VALUE 1 // comment \\\n new int\n')
    check(binary, "raw literal preserves splices", '#define RAW R"(raw )\\\n" new int )"\n')
    check(binary, "multiline raw macro", '#define RAW R"(raw\n new int\n)"\n')
    check(binary, "raw text in comment", '// R"( \\\n new int\n#define MAKE() new int\n',
          errors=1, rules=("probe.cpp(3,16): error MEM001",))
    check(binary, "macro string does not use permission", '#define TEXT "BIND"\n',
          'macro|BIND|GID002|exact|host/probe.cpp\n', warnings=1, rules=("CFG101",))
    check(binary, "macro comment does not use permission", '#define VALUE 1 /* BIND */\n',
          'macro|BIND|GID002|exact|host/probe.cpp\n', warnings=1, rules=("CFG101",))
    check(binary, "new literal is not an include", '#define TEXT "<new>"\n')
    check(binary, "locations after splicing", '#define VALUE \\\n1\nint* value = new int;\n',
          errors=1, rules=("probe.cpp(3,14): error MEM001",))
    check(binary, "suppression after splicing", '#define VALUE \\\n1\n'
          '// morphic-policy: suppress-next-line MEM001 reason="fixture"\nint* p = new int;\n',
          suppressed=1, rules=("probe.cpp(4,10): suppressed MEM001",))
    check(binary, "project display name is not its path", files={"MorphicEngine.sln":
          'Project("{GUID}") = "Display.vcxproj", "fixture.vcxproj", "{GUID}"\nEndProject\n'})
    check(binary, "project aliases cannot conflict", extra="project|alias.vcxproj|tool\n",
          setup=lambda root: (root / "alias.vcxproj").hardlink_to(root / "fixture.vcxproj"),
          errors=1, rules=("CFG001",))
    for flags in ('/std:c++17 /std:c++20', '/std:c++20 /std:c++17', '/std:c++17oops', '-std:c++20', '-EHsc'):
        check(binary, "compiler override " + flags, files={"fixture.vcxproj": PROJECT.replace(
              '</ClCompile>', f'<AdditionalOptions>{flags}</AdditionalOptions></ClCompile>')},
              errors=1, rules=("PRJ003",))
    check(binary, "include path is not an exception override", files={"fixture.vcxproj": PROJECT.replace(
          '</ClCompile>', '<AdditionalOptions>/Ivendor/eh /std:c++17</AdditionalOptions></ClCompile>')})
    print("Policy auditing, vendor boundary and macro allocation checks passed")


if __name__ == "__main__":
    main()
