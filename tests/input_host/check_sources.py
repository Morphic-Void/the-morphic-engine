# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

"""Check that the input fixture mirrors Platform's project-owned runtime files."""

import argparse
from pathlib import Path
import xml.etree.ElementTree as ET

NS = "{http://schemas.microsoft.com/developer/msbuild/2003}"
ITEM_TYPES = ("ClCompile", "ClInclude")


def local_items(path, repository):
    tree = ET.parse(path).getroot()
    items = {}
    for group in tree.findall(NS + "ItemGroup"):
        for item in group:
            kind = item.tag.removeprefix(NS)
            if kind not in ITEM_TYPES:
                continue
            include = item.get("Include", "")
            if not include or any(token in include for token in ("$", "%", "*", "?", ";")):
                raise ValueError(f"{path.name}: expected an explicit local item path: {include!r}")
            if "Condition" in group.attrib or "Condition" in item.attrib:
                raise ValueError(f"{path.name}: conditional runtime item: {include}")
            source = (path.parent / include.replace("\\", "/")).resolve()
            if not source.is_relative_to(repository) or not source.is_file():
                raise ValueError(f"{path.name}: missing or external item: {include}")
            key = (kind, source.relative_to(repository).as_posix())
            if key in items:
                raise ValueError(f"{path.name}: duplicate {kind} item: {include}")
            items[key] = item
    return tree, items


def project_filters(project, repository):
    _, items = local_items(project, repository)
    filters = Path(str(project) + ".filters")
    tree, mappings = local_items(filters, repository)
    if items.keys() != mappings.keys():
        raise ValueError(f"{filters.name}: file mappings differ from its project: "
                         f"missing={sorted(items.keys() - mappings.keys())}, "
                         f"extra={sorted(mappings.keys() - items.keys())}")
    declared = {item.get("Include") for item in tree.findall(f"{NS}ItemGroup/{NS}Filter")}
    result = {}
    for key, item in mappings.items():
        targets = item.findall(NS + "Filter")
        if len(targets) != 1 or targets[0].text not in declared:
            raise ValueError(f"{filters.name}: missing, duplicate or undefined filter for {key[1]}")
        result[key] = targets[0].text
    return result


def check_sources(repository):
    repository = repository.resolve()
    projects = repository / "visual_studio"
    production = project_filters(projects / "MorphicPlatform.vcxproj", repository)
    acceptance = project_filters(projects / "MorphicInputPlatform.vcxproj", repository)
    expected_extra = {
        ("ClCompile", "tests/input_host/platform_fixture.cpp"),
        ("ClInclude", "tests/input_host/control.hpp"),
    }
    if not production or not all(name.startswith("platform/") for _, name in production):
        raise ValueError("MorphicPlatform must own its Platform source/header entries directly")
    missing = production.keys() - acceptance.keys()
    extra = acceptance.keys() - production.keys()
    if missing or extra != expected_extra:
        raise ValueError(f"Input Platform runtime files differ: missing={sorted(missing)}, "
                         f"unexpected={sorted(extra - expected_extra)}, "
                         f"missing fixture files={sorted(expected_extra - extra)}")
    for key, folder in production.items():
        if acceptance[key] != folder:
            raise ValueError(f"Input Platform filter differs for {key[1]}")
    return len(production)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    try:
        count = check_sources(args.root)
    except (ValueError, OSError, ET.ParseError) as error:
        parser.exit(1, f"Input Platform source check failed: {error}\n")
    print(f"Input Platform: {count} runtime source/header entries and filters match.")


if __name__ == "__main__":
    main()
