# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cmake_sources import collect_sources


class SourceManifestTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        (self.root / "source.cpp").touch()

    def project(self, body, name="project.vcxproj"):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">'
            + body + '</Project>', encoding="utf-8"
        )
        return path

    def test_shared_sources_macros_and_deduplication(self):
        shared = self.project(
            '<ItemGroup><ClCompile Include="$(MSBuildThisFileDirectory)..\\source.cpp" /></ItemGroup>',
            "vs/core.vcxitems",
        )
        project = self.project(
            '<ImportGroup Label="Shared"><Import Project="core.vcxitems" Label="Shared" /></ImportGroup>'
            '<ItemGroup><ClCompile Include="$(ProjectDir)..\\source.cpp" /></ItemGroup>',
            "vs/app.vcxproj",
        )
        sources, projects = collect_sources(project, self.root)
        self.assertEqual(sources, [self.root / "source.cpp"])
        self.assertEqual(projects, [project, shared])

    def test_unsupported_source_selection_fails(self):
        cases = [
            '<ItemGroup Condition="false"><ClCompile Include="source.cpp" /></ItemGroup>',
            '<ItemGroup><ClCompile Include="source.cpp" Condition="false" /></ItemGroup>',
            '<ItemGroup><ClCompile Include="source.cpp"><ExcludedFromBuild>true</ExcludedFromBuild></ClCompile></ItemGroup>',
            '<ItemGroup><ClCompile Remove="source.cpp" /></ItemGroup>',
            '<ItemGroup><ClCompile Include="*.cpp" /></ItemGroup>',
            '<ItemGroup><ClCompile Include="$(Unknown)source.cpp" /></ItemGroup>',
            '<ItemGroup><ClCompile Include="missing.cpp" /></ItemGroup>',
            '<ItemGroup><ClCompile Include="../outside.cpp" /></ItemGroup>',
            '<Import Project="custom.props" />',
        ]
        for body in cases:
            with self.subTest(body=body), self.assertRaises(ValueError):
                collect_sources(self.project(body), self.root)

    def test_conditional_shared_import_fails(self):
        self.project('<ItemGroup><ClCompile Include="source.cpp" /></ItemGroup>', "core.vcxitems")
        project = self.project(
            '<ImportGroup Condition="false"><Import Project="core.vcxitems" Label="Shared" /></ImportGroup>'
        )
        with self.assertRaisesRegex(ValueError, "conditional shared import"):
            collect_sources(project, self.root)

    def test_shared_import_cycle_fails(self):
        project = self.project('<Import Project="core.vcxitems" Label="Shared" />')
        self.project('<Import Project="project.vcxproj" Label="Shared" />', "core.vcxitems")
        with self.assertRaisesRegex(ValueError, "Cyclic"):
            collect_sources(project, self.root)

    def test_empty_project_fails(self):
        with self.assertRaisesRegex(ValueError, r"no C\+\+ sources"):
            collect_sources(self.project(""), self.root)


if __name__ == "__main__":
    unittest.main()
