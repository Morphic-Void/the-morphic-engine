# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

"""Check Visual Studio filter correspondence without rewriting project XML.

Only local ItemGroups are compared: shared imports own their own filters and
build-time Target items are not Solution Explorer entries. C++ items require a
named filter; other resources may intentionally remain at the project root.
Virtual group names need not mirror physical directories, and unused groups are
allowed. This is a static consistency check, not an MSBuild evaluator.
"""

import argparse
from collections import defaultdict
import ntpath
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


NS = "{http://schemas.microsoft.com/developer/msbuild/2003}"
CPP_ITEMS = {"ClCompile", "ClInclude"}
NON_FILE_ITEMS = {"ProjectConfiguration", "ProjectReference", "ProjectCapability"}


def item_key(kind, include):
    # Both owners are in the same directory. Recognise the two local-directory
    # macros without expanding arbitrary MSBuild properties or touching files.
    path = include.replace("$(MSBuildThisFileDirectory)", "")
    path = path.replace("$(ProjectDir)", "")
    return kind, ntpath.normcase(ntpath.normpath(path))


def read_items(path):
    root = ET.parse(path).getroot()
    if root.tag != NS + "Project":
        raise ValueError(f"{path}: expected an MSBuild Project element")
    return [item for group in root.findall(NS + "ItemGroup") for item in group
            if "Include" in item.attrib]


def check_filters(project):
    project = Path(project)
    filters = project.with_name(project.name + ".filters")
    if not filters.exists():
        return []

    project_items = defaultdict(list)
    filter_items = defaultdict(list)
    declarations = defaultdict(list)
    errors = []

    for item in read_items(project):
        kind = item.tag.removeprefix(NS)
        if kind not in NON_FILE_ITEMS:
            project_items[item_key(kind, item.attrib["Include"])].append(item)

    for item in read_items(filters):
        kind = item.tag.removeprefix(NS)
        include = item.attrib["Include"]
        if kind == "Filter":
            name = include.strip().replace("/", "\\").casefold()
            if not name:
                errors.append(f"{filters}: empty filter declaration")
            declarations[name].append(item)
        else:
            filter_items[item_key(kind, include)].append(item)

    for name, items in declarations.items():
        if len(items) > 1:
            errors.append(f"{filters}: duplicate filter declaration: {items[0].attrib['Include']}")

    for key, items in project_items.items():
        kind, _ = key
        include = items[0].attrib["Include"]
        if len(items) > 1:
            errors.append(f"{project}: duplicate {kind} item: {include}")
        if kind in CPP_ITEMS and key not in filter_items:
            errors.append(f"{filters}: missing {kind} mapping: {include}")

    for key, items in filter_items.items():
        kind, _ = key
        include = items[0].attrib["Include"]
        if len(items) > 1:
            errors.append(f"{filters}: duplicate {kind} mapping: {include}")
        if key not in project_items:
            errors.append(f"{filters}: stale {kind} mapping: {include}")
        for item in items:
            values = item.findall(NS + "Filter")
            if len(values) > 1:
                errors.append(f"{filters}: multiple filters for {kind}: {include}")
            if kind in CPP_ITEMS and not values:
                errors.append(f"{filters}: missing filter value for {kind}: {include}")
            for value in values:
                name = (value.text or "").strip().replace("/", "\\").casefold()
                if not name:
                    if kind in CPP_ITEMS:
                        errors.append(f"{filters}: empty filter value for {kind}: {include}")
                elif name not in declarations:
                    errors.append(f"{filters}: undeclared filter '{value.text}' for {kind}: {include}")

    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("projects", nargs="*", type=Path,
                        help="project/shared-item files; defaults to visual_studio under --root")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1],
                        help="repository root for default project discovery")
    args = parser.parse_args(argv)
    projects = args.projects or sorted(
        path for path in (args.root / "visual_studio").glob("*")
        if path.suffix in {".vcxproj", ".vcxitems"}
    )
    if not projects:
        parser.error("no project/shared-item files found")

    errors = []
    checked = 0
    skipped = 0
    for project in projects:
        if not project.is_file() or project.suffix not in {".vcxproj", ".vcxitems"}:
            errors.append(f"{project}: expected an existing .vcxproj or .vcxitems file")
            continue
        if not project.with_name(project.name + ".filters").exists():
            skipped += 1
            continue
        checked += 1
        try:
            errors.extend(check_filters(project))
        except (ValueError, OSError, ET.ParseError) as error:
            errors.append(str(error))

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"Visual Studio filters: {checked} pairs checked; {skipped} projects without filters skipped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
