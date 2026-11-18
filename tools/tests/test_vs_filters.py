# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

from contextlib import redirect_stderr, redirect_stdout
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from check_vs_filters import check_filters, main


class VisualStudioFilterTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def write_xml(self, name, body):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">'
            + body + '</Project>', encoding="utf-8"
        )
        return path

    def pair(self, project_items, filter_items, declarations='<Filter Include="Virtual" />',
             name="project.vcxproj"):
        project = self.write_xml(name, '<ItemGroup>' + project_items + '</ItemGroup>')
        self.write_xml(name + ".filters", '<ItemGroup>' + declarations + filter_items + '</ItemGroup>')
        return project

    def test_virtual_groups_resources_and_unused_groups_are_preserved(self):
        project = self.pair(
            '<ClCompile Include="src/code.cpp" /><None Include="root.cfg" />'
            '<Manifest Include="windows.manifest" />',
            '<ClCompile Include="src/code.cpp"><Filter>Virtual</Filter></ClCompile>'
            '<None Include="root.cfg" />',
            '<Filter Include="Virtual" /><Filter Include="Reserved" />',
        )
        before = {path: path.read_bytes() for path in self.root.iterdir()}
        self.assertEqual(check_filters(project), [])
        self.assertEqual(before, {path: path.read_bytes() for path in self.root.iterdir()})

    def test_shared_item_directory_macros_and_windows_path_spelling(self):
        project = self.pair(
            '<ClInclude Include="$(MSBuildThisFileDirectory)..\\src\\header.hpp" />',
            '<ClInclude Include="$(ProjectDir)../src/./HEADER.hpp"><Filter>Virtual</Filter></ClInclude>',
            name="shared.vcxitems",
        )
        self.assertEqual(check_filters(project), [])

    def test_missing_cpp_mapping(self):
        for kind in ("ClCompile", "ClInclude"):
            with self.subTest(kind=kind):
                errors = check_filters(self.pair(f'<{kind} Include="code" />', ""))
                self.assertEqual(len(errors), 1)
                self.assertIn(f"missing {kind} mapping", errors[0])

    def test_missing_and_empty_cpp_filter_values(self):
        for kind in ("ClCompile", "ClInclude"):
            for metadata, message in (("", "missing"), ("<Filter />", "empty"),
                                      ("<Filter>  </Filter>", "empty")):
                with self.subTest(kind=kind, metadata=metadata):
                    errors = check_filters(self.pair(
                        f'<{kind} Include="code" />', f'<{kind} Include="code">{metadata}</{kind}>'))
                    self.assertEqual(len(errors), 1)
                    self.assertIn(f"{message} filter value", errors[0])

    def test_stale_mapping_and_item_type_mismatch(self):
        errors = check_filters(self.pair(
            '<ClInclude Include="code.hpp" />',
            '<ClCompile Include="code.hpp"><Filter>Virtual</Filter></ClCompile>',
        ))
        self.assertEqual(len(errors), 2)
        self.assertTrue(any("missing ClInclude mapping" in error for error in errors))
        self.assertTrue(any("stale ClCompile mapping" in error for error in errors))
        errors = check_filters(self.pair("", '<None Include="removed.cfg" />'))
        self.assertEqual(len(errors), 1)
        self.assertIn("stale None mapping", errors[0])

    def test_duplicate_project_items_and_filter_mappings(self):
        source = '<ClCompile Include="src/code.cpp" />'
        mapping = '<ClCompile Include="src/code.cpp"><Filter>Virtual</Filter></ClCompile>'
        errors = check_filters(self.pair(
            source + source.replace("src/code", "src/./CODE"),
            mapping + mapping.replace("src/code", "src/./CODE"),
        ))
        self.assertEqual(len(errors), 2)
        self.assertIn("duplicate ClCompile item", errors[0])
        self.assertIn("duplicate ClCompile mapping", errors[1])

    def test_filter_declarations_and_values(self):
        for declarations, metadata, message in (
            ('<Filter Include="Virtual" /><Filter Include="virtual" />', "Virtual", "duplicate filter declaration"),
            ('<Filter Include="" />', "", "empty filter declaration"),
            ('<Filter Include="Virtual" />', "Missing", "undeclared filter"),
        ):
            with self.subTest(message=message):
                errors = check_filters(self.pair(
                    '<None Include="root.cfg" />',
                    f'<None Include="root.cfg"><Filter>{metadata}</Filter></None>', declarations,
                ))
                self.assertEqual(len(errors), 1)
                self.assertIn(message, errors[0])

    def test_multiple_filter_values(self):
        errors = check_filters(self.pair(
            '<ClCompile Include="code.cpp" />',
            '<ClCompile Include="code.cpp"><Filter>Virtual</Filter><Filter>Virtual</Filter></ClCompile>',
        ))
        self.assertEqual(len(errors), 1)
        self.assertIn("multiple filters", errors[0])

    def test_imports_references_and_build_time_items_are_not_local_file_items(self):
        project = self.write_xml("project.vcxproj",
            '<ImportGroup><Import Project="shared.vcxitems" /></ImportGroup>'
            '<ItemGroup><ProjectReference Include="other.vcxproj" />'
            '<ProjectConfiguration Include="Debug|x64" /><ProjectCapability Include="Shared" /></ItemGroup>'
            '<Target Name="Stage"><ItemGroup><ClCompile Include="generated.cpp" /></ItemGroup></Target>')
        self.write_xml("project.vcxproj.filters", "")
        self.assertEqual(check_filters(project), [])

    def test_projects_without_filters_are_intentional(self):
        project = self.write_xml("fixture.vcxproj", '<ItemGroup><ClCompile Include="code.cpp" /></ItemGroup>')
        self.assertEqual(check_filters(project), [])
        with redirect_stdout(io.StringIO()) as output:
            self.assertEqual(main([str(project)]), 0)
        self.assertIn("0 pairs checked; 1 projects without filters skipped", output.getvalue())

    def test_default_discovery_and_cli_failure(self):
        self.pair('<ClCompile Include="code.cpp" />', "", name="visual_studio/project.vcxproj")
        with redirect_stderr(io.StringIO()) as errors:
            self.assertEqual(main(["--root", str(self.root)]), 1)
        self.assertIn("missing ClCompile mapping", errors.getvalue())

    def test_invalid_xml_and_missing_explicit_project_fail(self):
        project = self.pair("", "")
        project.write_text("<Project>", encoding="utf-8")
        with redirect_stderr(io.StringIO()) as errors:
            self.assertEqual(main([str(project)]), 1)
        self.assertIn("no element found", errors.getvalue())
        with redirect_stderr(io.StringIO()) as errors:
            self.assertEqual(main([str(self.root / "missing.vcxproj")]), 1)
        self.assertIn("expected an existing", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
