
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    FilesystemImage_test_suite.cpp
//  Author:  OpenAI Codex
//  Date:    22 Sep 26
//
//  Filesystem image discovery, bindings, refresh and cache-association tests.

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "assets/internal/bundle_builder_io.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/test_suites/FilesystemImage_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "tests/environment/test_paths.hpp"
#include "filesystem/filesystem_image.hpp"
#include "platform/filesystem/directory.hpp"
#include "platform/filesystem/file.hpp"
#include "data_model/baked_document.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"
#include "data_model/document_parser.hpp"

namespace filesystem_image_tests
{

static bool write(const std::string& path, const char* const text)
{
    return platform::filesystem::saveFile(path.c_str(), CByteConstView{
        reinterpret_cast<const std::uint8_t*>(text), std::strlen(text) });
}

static bool count(void* const context, const char* const, const platform::filesystem::EDirectoryEntry) noexcept
{
    ++*static_cast<std::uint32_t*>(context);
    return true;
}

static bool stop(void* const, const char* const, const platform::filesystem::EDirectoryEntry) noexcept
{
    return false;
}

static CNodeKey member(const CLiveDocument& doc, const CNodeKey parent, const char* const name)
{
    return doc.object_child(parent, CStringView{ name });
}

static CNodeKey root(const CLiveDocument& doc, const char* const name)
{
    return member(doc, member(doc, doc.root(), "roots"), name);
}

static CNodeKey entry(const CLiveDocument& doc, const char* const root_name, const char* const name)
{
    return member(doc, member(doc, root(doc, root_name), "content"), name);
}

static void test_development_image(tests::TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, filesystem_image::scan_manifest("development/root-manifest.json", document) == filesystem_image::EScanStatus::success);
    if (!document.is_ready()) { return; }
    TEST_EXPECT(ctx, document.check_integrity());
    filesystem_image::CImage image;
    TEST_EXPECT(ctx, image.adopt(std::move(document)));
    CSimpleString physical;
    TEST_EXPECT(ctx, image.resolve("dev-source:/test_input.tga", false, physical));
    TEST_EXPECT(ctx, std::strcmp(physical.cstring(), "development/logical-roots/dev-source/test_input.tga") == 0);
    TEST_EXPECT(ctx, image.resolve("package:/bin/MorphicExecutive.dll", false, physical));
    CSimpleString executable;
    TEST_EXPECT(ctx, platform::filesystem::executable_directory(executable));
    const std::string executable_path = std::filesystem::path(executable.cstring()).generic_string();
    TEST_EXPECT(ctx, std::strncmp(physical.cstring(), executable_path.c_str(), executable_path.length()) == 0);
    TEST_EXPECT(ctx, std::strchr(physical.cstring(), '\\') == nullptr);
    TEST_EXPECT(ctx, !image.resolve("package:/bin/MorphicEngine.exe", false, physical));
    TEST_EXPECT(ctx, !image.resolve("package:/bin/MorphicExecutive.dll", true, physical));
    TEST_EXPECT(ctx, !image.resolve("dev-source:/.gitkeep", false, physical));
    TEST_EXPECT(ctx, !image.resolve("dev-source:/../root-manifest.json", false, physical));
    TEST_EXPECT(ctx, !image.resolve("dev-source:/./test_input.tga", false, physical));
    TEST_EXPECT(ctx, !image.resolve("D:/test_input.tga", false, physical));
    TEST_EXPECT(ctx, !image.resolve("dev-source:/test_input.tga:alternate", true, physical));
    TEST_EXPECT(ctx, !image.resolve("dev-source:/missing/new.json", true, physical));
    TEST_EXPECT(ctx, image.resolve("test-output:/new.json", true, physical));
    TEST_EXPECT(ctx, !image.resolve("ugc-installed:/new.json", true, physical));
    for (const char* name : { "logs:", "test-logs:" })
    {
        const CNodeKey log_root = root(image.document(), name);
        TEST_EXPECT(ctx, log_root.is_valid());
        TEST_EXPECT(ctx, !member(image.document(), log_root, "content").is_valid());
        bool inventory = true;
        TEST_EXPECT(ctx, image.document().boolean_value(member(image.document(), log_root, "inventory"), inventory) && !inventory);
    }
    TEST_EXPECT(ctx, member(image.document(), root(image.document(), "test-output:"), "content").is_valid());
    const CNodeKey input = entry(image.document(), "dev-source:", "test_input.tga");
    TEST_EXPECT(ctx, input.is_valid() && image.document().child_count(input) == 0u);
    TEST_EXPECT(ctx, !image.resolve("logs:/anything.log", false, physical));
    TEST_EXPECT(ctx, image.resolve("logs:/policy_validator/new.log", true, physical));

    CBakedDocumentBlock baked;
    TEST_EXPECT(ctx, document_translation::bake(image.document(), baked));
    CDocumentWriteOptions options;
    options.mode = EDocumentWriteMode::strict_json;
    CDocumentWriteResult written = document_writer::write(baked.document(), options);
    TEST_EXPECT(ctx, written.report.succeeded());
    TEST_EXPECT(ctx, written.output.set_size(written.report.logical_text_byte_size));
    const std::string output = test_environment::test_output_path("filesystem-image.json");
    TEST_EXPECT(ctx, platform::filesystem::saveFile(output.c_str(), written.output.const_view()));
    std::cout << "Filesystem image: " << output << '\n';
}

static void test_refresh(tests::TTestContext& ctx)
{

    namespace fs = std::filesystem;
    using platform::filesystem::EDirectoryQuery;
    using filesystem_image::EScanStatus;
    const std::string base = fs::path(test_environment::test_output_path("filesystem-fixture")).generic_string();
    std::error_code error;
    const bool created = fs::create_directory(base, error);
    TEST_EXPECT(ctx, created && !error);
    if (!created || error) { return; }
    TEST_EXPECT(ctx, fs::create_directory(base + "/content", error) && !error);
    TEST_EXPECT(ctx, fs::create_directory(base + "/content/empty", error) && !error);
    const std::string manifest = base + "/roots.json";
    TEST_EXPECT(ctx, write(manifest, R"({"schemaVersion":2,"roots":{"work:":{"source":"content","writable":true},"output:":{"source":"absent","writable":true,"inventory":false}}})"));
    TEST_EXPECT(ctx, write(base + "/content/a.json", "{\"value\":1}"));
    TEST_EXPECT(ctx, write(base + "/content/empty/content", "metadata-like filename"));
    std::uint32_t entries = 0u;
    TEST_EXPECT(ctx, platform::filesystem::query_directory((base + "/content/empty").c_str(), count, &entries) == EDirectoryQuery::complete);
    TEST_EXPECT(ctx, entries == 1u);
    TEST_EXPECT(ctx, platform::filesystem::query_directory((base + "/absent").c_str(), count, &entries) == EDirectoryQuery::open_failed);
    TEST_EXPECT(ctx, platform::filesystem::query_directory((base + "/content").c_str(), stop, nullptr) == EDirectoryQuery::visitor_failed);

    CLiveDocument document;
    TEST_EXPECT(ctx, filesystem_image::scan_manifest(manifest.c_str(), document) == EScanStatus::success);
    filesystem_image::CImage image;
    TEST_EXPECT(ctx, image.adopt(std::move(document)));
    TEST_EXPECT(ctx, image.document().child_count(entry(image.document(), "work:", "a.json")) == 0u);
    CSimpleString nested_path;
    TEST_EXPECT(ctx, image.resolve("work:/empty/content", false, nested_path));
    TEST_EXPECT(ctx, image.resolve("WORK:/A.json", true, nested_path));
    TEST_EXPECT(ctx, nested_path.view() == CStringView{ (base + "/content/a.json").c_str() });
    TEST_EXPECT(ctx, image.resolve("work:/EMPTY/new.json", true, nested_path));
    TEST_EXPECT(ctx, nested_path.view() == CStringView{ (base + "/content/empty/new.json").c_str() });

    //  Excluded roots neither require a physical directory nor gain entries
    //  from write completion or explicit refresh. They are write-only here.
    CSimpleString output_path;
    TEST_EXPECT(ctx, image.resolve("output:/nested/session.log", true, output_path));
    TEST_EXPECT(ctx, std::strcmp(output_path.cstring(), (base + "/absent/nested/session.log").c_str()) == 0);
    TEST_EXPECT(ctx, image.written("output:/nested/session.log", output_path.cstring(), 99u, 1u));
    TEST_EXPECT(ctx, !image.resolve("output:/nested/session.log", false, output_path));
    TEST_EXPECT(ctx, image.cached_asset("output:/nested/session.log", 1u) == 0u);
    TEST_EXPECT(ctx, image.write_serial() == 0u);
    filesystem_image::SRootScan output_scan;
    CLiveDocument output_observation;
    TEST_EXPECT(ctx, image.prepare_scan("output:", output_scan) && !output_scan.inventory);
    TEST_EXPECT(ctx, filesystem_image::scan_root(output_scan, output_observation) == EScanStatus::success);
    TEST_EXPECT(ctx, image.integrate(output_observation, 0u));
    TEST_EXPECT(ctx, !member(image.document(), root(image.document(), "output:"), "content").is_valid());
    CSimpleString borrowed_path;
    TEST_EXPECT(ctx, image.resolve("work:/a.json", false, borrowed_path));
    const char* const borrowed = borrowed_path.cstring();
    TEST_EXPECT(ctx, image.loaded("work:/a.json", 42u, 2u, 0u, 0u));
    TEST_EXPECT(ctx, image.cached_asset("work:/a.json", 2u) == 42u);
    TEST_EXPECT(ctx, image.cached_asset("work:/a.json", 1u) == 0u);
    TEST_EXPECT(ctx, image.cached_findings("work:/a.json") == 0u);
    TEST_EXPECT(ctx, image.document().child_count(entry(image.document(), "work:", "a.json")) == 1u);

    filesystem_image::SRootScan scan;
    TEST_EXPECT(ctx, !image.prepare_scan("unknown:", scan));
    TEST_EXPECT(ctx, image.prepare_scan("work:", scan));
    CLiveDocument observation;
    TEST_EXPECT(ctx, filesystem_image::scan_root(scan, observation) == EScanStatus::success);
    //  This successful save was completed after the scan began. Integration
    //  must preserve it even though this observation did not see the new file.
    TEST_EXPECT(ctx, image.written("work:/new.bin", (base + "/content/new.bin").c_str(), 43u, 1u));
    TEST_EXPECT(ctx, image.integrate(observation, 0u));
    TEST_EXPECT(ctx, image.document().check_integrity());
    TEST_EXPECT(ctx, borrowed == borrowed_path.cstring());
    TEST_EXPECT(ctx, std::strcmp(borrowed, (base + "/content/a.json").c_str()) == 0);
    TEST_EXPECT(ctx, image.cached_asset("work:/a.json", 2u) == 42u);
    TEST_EXPECT(ctx, image.cached_asset("work:/new.bin", 1u) == 43u);
    TEST_EXPECT(ctx, image.forget_asset(42u));
    TEST_EXPECT(ctx, image.cached_asset("work:/a.json", 2u) == 0u);
    TEST_EXPECT(ctx, image.document().child_count(entry(image.document(), "work:", "a.json")) == 0u);
    TEST_EXPECT(ctx, image.loaded("work:/a.json", 44u, 2u, 0u, image.write_serial()));
    TEST_EXPECT(ctx, image.written("work:/a.json", borrowed, 45u, 2u));
    TEST_EXPECT(ctx, image.loaded("work:/a.json", 46u, 2u, 0u, 0u));
    TEST_EXPECT(ctx, image.cached_asset("work:/a.json", 2u) == 45u);
    TEST_EXPECT(ctx, image.cached_findings("work:/a.json") == UINT32_MAX);

    TEST_EXPECT(ctx, fs::remove(base + "/content/a.json", error) && !error);
    TEST_EXPECT(ctx, filesystem_image::scan_root(scan, observation) == EScanStatus::success);
    TEST_EXPECT(ctx, image.integrate(observation, image.write_serial()));
    CSimpleString physical;
    TEST_EXPECT(ctx, !image.resolve("work:/a.json", false, physical));
    TEST_EXPECT(ctx, !entry(image.document(), "work:", "a.json").is_valid());
    TEST_EXPECT(ctx, image.loaded("work:/a.json", 98u, 2u, 0u, image.write_serial()));
    TEST_EXPECT(ctx, image.cached_asset("work:/a.json", 2u) == 0u);
    TEST_EXPECT(ctx, image.resolve("work:/a.json", true, physical));
    TEST_EXPECT(ctx, image.forget_asset(45u));
    TEST_EXPECT(ctx, fs::create_directory(base + "/content/a.json", error) && !error);
    TEST_EXPECT(ctx, filesystem_image::scan_root(scan, observation) == EScanStatus::success);
    TEST_EXPECT(ctx, image.integrate(observation, image.write_serial()));
    TEST_EXPECT(ctx, image.loaded("work:/a.json", 47u, 2u, 0u, image.write_serial()));
    TEST_EXPECT(ctx, image.cached_asset("work:/a.json", 2u) == 0u);
    TEST_EXPECT(ctx, !image.resolve("work:/a.json", false, physical));
    TEST_EXPECT(ctx, fs::remove(base + "/content/a.json", error) && !error);
    //  A successful in-flight write is authoritative over that older scan.
    TEST_EXPECT(ctx, image.written("work:/a.json", borrowed, 48u, 2u));
    TEST_EXPECT(ctx, image.resolve("work:/a.json", false, physical));
    TEST_EXPECT(ctx, image.cached_asset("work:/a.json", 2u) == 48u);
    TEST_EXPECT(ctx, image.resolve("work:/empty/new.json", true, physical));
    const std::uint32_t previous_count = observation.value_count();
    TEST_EXPECT(ctx, scan.physical_path.set((base + "/absent").c_str()));
    TEST_EXPECT(ctx, filesystem_image::scan_root(scan, observation) == EScanStatus::scan_failed);
    TEST_EXPECT(ctx, observation.value_count() == previous_count);
    TEST_EXPECT(ctx, image.resolve("work:/empty/new.json", true, physical));
    TEST_EXPECT(ctx, image.document().check_integrity());

    //  An older observation can omit a whole parent branch. Restore only the
    //  successfully written branch, not stale siblings, and keep its cache ID.
    TEST_EXPECT(ctx, fs::remove(base + "/content/empty/content", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/content/empty", error) && !error);
    TEST_EXPECT(ctx, image.prepare_scan("work:", scan));
    TEST_EXPECT(ctx, filesystem_image::scan_root(scan, observation) == EScanStatus::success);
    const std::uint64_t before_nested_write = image.write_serial();
    TEST_EXPECT(ctx, image.written("work:/empty/new.json", (base + "/content/empty/new.json").c_str(), 49u, 2u));
    TEST_EXPECT(ctx, image.integrate(observation, before_nested_write));
    TEST_EXPECT(ctx, image.resolve("work:/empty/new.json", false, physical));
    TEST_EXPECT(ctx, image.cached_asset("work:/empty/new.json", 2u) == 49u);
    TEST_EXPECT(ctx, !image.resolve("work:/empty/content", false, physical));
    TEST_EXPECT(ctx, image.document().check_integrity());

    //  The redirect discovers every DLL, including test fixtures, but neither
    //  executables nor symbols. An overlapping logical filename is an error.
    TEST_EXPECT(ctx, fs::create_directory(base + "/dlls", error) && !error);
    TEST_EXPECT(ctx, write(base + "/dlls/runtime.dll", "fixture"));
    TEST_EXPECT(ctx, write(base + "/dlls/test-only.dll", "fixture"));
    TEST_EXPECT(ctx, write(base + "/dlls/runtime.exe", "fixture"));
    TEST_EXPECT(ctx, write(base + "/dlls/runtime.pdb", "fixture"));
    TEST_EXPECT(ctx, image.prepare_scan("work:", scan));
    TEST_EXPECT(ctx, scan.redirect_directory.set((base + "/dlls").c_str()));
    TEST_EXPECT(ctx, filesystem_image::scan_root(scan, observation) == EScanStatus::success);
    TEST_EXPECT(ctx, image.integrate(observation, image.write_serial()));
    TEST_EXPECT(ctx, image.resolve("work:/bin/runtime.dll", false, physical));
    TEST_EXPECT(ctx, image.resolve("work:/bin/test-only.dll", false, physical));
    TEST_EXPECT(ctx, !image.resolve("work:/bin/runtime.exe", false, physical));
    TEST_EXPECT(ctx, !image.resolve("work:/bin/runtime.pdb", false, physical));
    TEST_EXPECT(ctx, !image.resolve("work:/bin/runtime.dll", true, physical));
    TEST_EXPECT(ctx, !image.resolve("work:/bin/new.dll", true, physical));
    TEST_EXPECT(ctx, fs::create_directory(base + "/content/bin", error) && !error);
    TEST_EXPECT(ctx, write(base + "/content/bin/runtime.dll", "collision"));
    const std::uint32_t before_collision = observation.value_count();
    TEST_EXPECT(ctx, filesystem_image::scan_root(scan, observation) == EScanStatus::name_collision);
    TEST_EXPECT(ctx, observation.value_count() == before_collision);
    TEST_EXPECT(ctx, image.resolve("work:/bin/test-only.dll", false, physical));
    TEST_EXPECT(ctx, fs::remove(base + "/content/bin/runtime.dll", error) && !error);
    TEST_EXPECT(ctx, write(base + "/content/bin/local.json", "overlay"));
    TEST_EXPECT(ctx, filesystem_image::scan_root(scan, observation) == EScanStatus::success);
    TEST_EXPECT(ctx, image.integrate(observation, image.write_serial()));
    TEST_EXPECT(ctx, image.resolve("work:/bin/local.json", false, physical));
    TEST_EXPECT(ctx, std::strcmp(physical.cstring(), (base + "/content/bin/local.json").c_str()) == 0);
    TEST_EXPECT(ctx, image.resolve("work:/bin/runtime.dll", false, physical));
    TEST_EXPECT(ctx, std::strcmp(physical.cstring(), (base + "/dlls/runtime.dll").c_str()) == 0);
    TEST_EXPECT(ctx, fs::remove(base + "/content/bin/local.json", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/content/bin", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/dlls/runtime.dll", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/dlls/test-only.dll", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/dlls/runtime.exe", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/dlls/runtime.pdb", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/dlls", error) && !error);

    TEST_EXPECT(ctx, write(manifest, R"({"schemaVersion":2,"roots":{"work:":{"source":"../content","writable":true}}})"));
    TEST_EXPECT(ctx, filesystem_image::scan_manifest(manifest.c_str(), document) == EScanStatus::invalid_manifest);
    TEST_EXPECT(ctx, !document.is_ready());

    //  Explicitly remove only the known fixture files and empty directories.
    TEST_EXPECT(ctx, fs::remove(manifest, error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/content", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base, error) && !error);
}

static void test_case_identity(tests::TTestContext& ctx)
{
    namespace fs = std::filesystem;
    using filesystem_image::EScanStatus;
    const std::string base = fs::path(test_environment::test_output_path("case-fixture")).generic_string();
    std::error_code error;
    TEST_EXPECT(ctx, fs::create_directories(base + "/Content/Textures", error) && !error);
    TEST_EXPECT(ctx, fs::create_directories(base + "/Content/Bin", error) && !error);
    TEST_EXPECT(ctx, fs::create_directory(base + "/Dlls", error) && !error);
    TEST_EXPECT(ctx, write(base + "/Content/Textures/Brick.PNG", "image"));
    TEST_EXPECT(ctx, write(base + "/Content/plain.txt", "plain"));
    TEST_EXPECT(ctx, write(base + "/Content/Bin/Local.JSON", "local"));
    TEST_EXPECT(ctx, write(base + "/Dlls/Runtime.DLL", "module"));
    filesystem_image::SRootScan request;
    TEST_EXPECT(ctx, request.logical_root.set("work:"));
    TEST_EXPECT(ctx, request.physical_path.set((base + "/Content").c_str()));
    TEST_EXPECT(ctx, request.redirect_directory.set((base + "/Dlls").c_str()));
    request.writable = true;
    CLiveDocument document;
    TEST_EXPECT(ctx, filesystem_image::scan_root(request, document) == EScanStatus::success);
    const CNodeKey textures = entry(document, "work:", "textures");
    TEST_EXPECT(ctx, document.string_value(member(document, textures, "physicalName")) == CStringView{ "Textures" });
    TEST_EXPECT(ctx, !member(document, entry(document, "work:", "plain.txt"), "physicalName").is_valid());
    filesystem_image::CImage image;
    TEST_EXPECT(ctx, image.adopt(std::move(document)));
    CSimpleString physical;
    TEST_EXPECT(ctx, image.resolve("WORK:/TEXTURES/brick.png", false, physical));
    TEST_EXPECT(ctx, physical.view() == CStringView{ (base + "/Content/Textures/Brick.PNG").c_str() });
    TEST_EXPECT(ctx, image.resolve("work:/BIN/local.json", false, physical));
    TEST_EXPECT(ctx, physical.view() == CStringView{ (base + "/Content/Bin/Local.JSON").c_str() });
    TEST_EXPECT(ctx, image.resolve("work:/bin/RUNTIME.dll", false, physical));
    TEST_EXPECT(ctx, physical.view() == CStringView{ (base + "/Dlls/Runtime.DLL").c_str() });
    TEST_EXPECT(ctx, !image.resolve("work:/BIN/runtime.dll", true, physical));
    TEST_EXPECT(ctx, image.loaded("WORK:/Textures/BRICK.png", 70u, 1u, 9u, 0u));
    TEST_EXPECT(ctx, image.cached_asset("work:/textures/brick.PNG", 1u) == 70u);
    TEST_EXPECT(ctx, image.cached_findings("work:/TEXTURES/brick.png") == 9u);

    CLiveDocument observation;
    TEST_EXPECT(ctx, image.prepare_scan("WORK:", request));
    TEST_EXPECT(ctx, request.logical_root.view() == CStringView{ "work:" });
    TEST_EXPECT(ctx, filesystem_image::scan_root(request, observation) == EScanStatus::success);
    TEST_EXPECT(ctx, image.resolve("work:/TEXTURES/New.JSON", true, physical));
    TEST_EXPECT(ctx, physical.view() == CStringView{ (base + "/Content/Textures/New.JSON").c_str() });
    TEST_EXPECT(ctx, image.written("WORK:/textures/New.JSON", physical.cstring(), 71u, 1u));
    TEST_EXPECT(ctx, image.integrate(observation, 0u));
    TEST_EXPECT(ctx, image.resolve("work:/textures/new.json", false, physical));
    TEST_EXPECT(ctx, physical.view() == CStringView{ (base + "/Content/Textures/New.JSON").c_str() });
    TEST_EXPECT(ctx, image.cached_asset("work:/TEXTURES/NEW.json", 1u) == 71u);
    TEST_EXPECT(ctx, image.written("work:/TEXTURES/new.json", physical.cstring(), 72u, 1u));
    TEST_EXPECT(ctx, image.loaded("work:/textures/NEW.JSON", 73u, 1u, 0u, 0u));
    TEST_EXPECT(ctx, image.cached_asset("WORK:/textures/New.Json", 1u) == 72u);
    TEST_EXPECT(ctx, image.forget_asset(72u));
    TEST_EXPECT(ctx, image.cached_asset("work:/textures/new.json", 1u) == 0u);

    //  Separate physical directories can contribute aliases even on Windows.
    TEST_EXPECT(ctx, write(base + "/Content/Bin/runtime.dll", "collision"));
    const std::uint32_t retained = observation.value_count();
    TEST_EXPECT(ctx, filesystem_image::scan_root(request, observation) == EScanStatus::name_collision);
    TEST_EXPECT(ctx, observation.value_count() == retained);
    TEST_EXPECT(ctx, fs::remove(base + "/Content/Bin/runtime.dll", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/Content/Bin/Local.JSON", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/Content/Bin", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/Content/Textures/Brick.PNG", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/Content/Textures", error) && !error);
    TEST_EXPECT(ctx, filesystem_image::scan_root(request, observation) == EScanStatus::success);
    TEST_EXPECT(ctx, image.integrate(observation, 0u));
    TEST_EXPECT(ctx, image.resolve("WORK:/TEXTURES/new.json", false, physical));
    TEST_EXPECT(ctx, physical.view() == CStringView{ (base + "/Content/Textures/New.JSON").c_str() });
    TEST_EXPECT(ctx, fs::remove(base + "/Content/plain.txt", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/Content", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/Dlls/Runtime.DLL", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base + "/Dlls", error) && !error);
    TEST_EXPECT(ctx, fs::remove(base, error) && !error);
}

static void test_manual_case_mapping(tests::TTestContext& ctx)
{
    const char* const valid = R"({"roots":{"work:":{"source":"Some/Prefix","writable":true,"content":{"plain.txt":{},"mixed.txt":{"physicalName":"Mixed.TXT"},"\u00c4.txt":{},"\u00e4.txt":{}}}}})";
    CLiveDocument document;
    TEST_EXPECT(ctx, document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(valid), std::strlen(valid) }, document).accepted());
    filesystem_image::CImage image;
    TEST_EXPECT(ctx, image.adopt(std::move(document)));
    CSimpleString physical;
    TEST_EXPECT(ctx, image.resolve("WORK:/PLAIN.TXT", false, physical) && (physical.view() == CStringView{ "Some/Prefix/plain.txt" }));
    TEST_EXPECT(ctx, image.resolve("work:/MIXED.txt", false, physical) && (physical.view() == CStringView{ "Some/Prefix/Mixed.TXT" }));
    TEST_EXPECT(ctx, image.resolve("work:/\xc3\x84.TXT", false, physical) && (physical.view() == CStringView{ "Some/Prefix/\xc3\x84.txt" }));
    TEST_EXPECT(ctx, image.resolve("work:/\xc3\xa4.TXT", false, physical) && (physical.view() == CStringView{ "Some/Prefix/\xc3\xa4.txt" }));
    const char* const invalid[]{
        R"({"roots":{"work:":{"content":{"Mixed.txt":{}}}}})",
        R"({"roots":{"work:":{"content":{"mixed.txt":{"physicalName":"other.txt"}}}}})",
        R"({"roots":{"work:":{"content":{"mixed.txt":{"physicalName":false}}}}})",
        R"({"roots":{"work:":{"content":{"mixed.txt":{"physicalName":"dir/Mixed.txt"}}}}})",
        R"({"roots":{"WORK:":{"content":{}}}})" };
    for (const char* const input : invalid)
    {
        TEST_EXPECT(ctx, document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(input), std::strlen(input) }, document).accepted());
        TEST_EXPECT(ctx, !image.integrate(document, 0u));
        TEST_EXPECT(ctx, !image.adopt(std::move(document)));
        TEST_EXPECT(ctx, image.resolve("work:/plain.txt", false, physical));
    }
}

static bool parse_image(const char* input, CLiveDocument& document)
{
    return document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(input), std::strlen(input) }, document).accepted();
}

static const char* const binding_image = R"({"roots":{"work:":{"source":"Some/Prefix","writable":true,"content":{"plain.txt":{}}}}})";

static bool make_bundle(CBakedDocumentBlock& block, bundle::CBundleView& view)
{
    CLiveDocument input;
    if (!parse_image(R"({"plain.txt":{},"nested":{"physicalName":"Nested","content":{"file.bin":{"physicalName":"File.BIN"}}}})", input)) { return false; }
    TPodVector<bundle::SBuildSource> sources;
    const CNodeKey file = member(input, member(input, member(input, input.root(), "nested"), "content"), "file.bin");
    if (!sources.push_back({ member(input, input.root(), "plain.txt"), "plain" }) || !sources.push_back({ file, "nested" })) { return false; }
    const bundle::detail::SBuildIo io{ nullptr,
        [](void*, const char*, std::size_t& size) noexcept { size = 3u; return true; },
        [](void*, const char*, const CByteView& bytes) noexcept { std::memcpy(bytes.data(), "abc", 3u); return true; } };
    return bundle::detail::build(input, input.root(), sources.const_view(), block, view, io).succeeded();
}

static void test_virtual_bindings(tests::TTestContext& ctx)
{
    using namespace filesystem_image;
    CBakedDocumentBlock block;
    bundle::CBundleView view;
    TEST_EXPECT(ctx, make_bundle(block, view));
    if (!view.is_ready()) { return; }
    const std::vector<std::uint8_t> original(view.bytes().data(), view.bytes().data() + view.bytes().size());
    CLiveDocument doc;
    TEST_EXPECT(ctx, parse_image(binding_image, doc));
    CImage image;
    TEST_EXPECT(ctx, image.adopt(std::move(doc)));
    std::uint64_t token = 0u, other = 0u;
    TEST_EXPECT(ctx, image.root_state("WORK:") == ERootState::directory);
    for (const char* invalid : { "", "bad", "bad/:", "../:", "work:", "WORK:" })
    {
        TEST_EXPECT(ctx, !image.reserve_bundle(invalid, token) && token == 0u);
    }
    TEST_EXPECT(ctx, !image.reserve_bundle(nullptr, token));
    TEST_EXPECT(ctx, image.reserve_bundle("PACK:", token) && token != 0u);
    TEST_EXPECT(ctx, image.root_state("pack:") == ERootState::reserved);
    TEST_EXPECT(ctx, !root(image.document(), "pack:").is_valid());
    TEST_EXPECT(ctx, !image.reserve_bundle("pack:", other) && other == 0u);
    TEST_EXPECT(ctx, !image.publish_bundle(token) && !image.close_bundle(token));
    TEST_EXPECT(ctx, !image.prepare_bundle(token, 0u, view));
    TEST_EXPECT(ctx, !image.prepare_bundle(token, 100u, {}));
    TEST_EXPECT(ctx, image.prepare_bundle(token, 100u, view));
    TEST_EXPECT(ctx, image.root_state("PACK:") == ERootState::prepared);
    TEST_EXPECT(ctx, !root(image.document(), "pack:").is_valid());
    TEST_EXPECT(ctx, image.reserve_bundle("other:", other));
    TEST_EXPECT(ctx, !image.prepare_bundle(other, 100u, view));
    TEST_EXPECT(ctx, image.remove_bundle(other));
    TEST_EXPECT(ctx, !image.remove_bundle(other));
    TEST_EXPECT(ctx, parse_image(binding_image, doc));
    TEST_EXPECT(ctx, !image.adopt(std::move(doc)) && doc.is_ready());
    //  Publication must incorporate ordinary changes made after preparation.
    TEST_EXPECT(ctx, image.written("work:/new.txt", "Some/Prefix/new.txt", 51u, 1u));
    TEST_EXPECT(ctx, image.publish_bundle(token));
    TEST_EXPECT(ctx, image.cached_asset("work:/new.txt", 1u) == 51u);
    TEST_EXPECT(ctx, image.root_state("PACK:") == ERootState::open);
    TEST_EXPECT(ctx, !image.publish_bundle(token) && !image.prepare_bundle(token, 100u, view));
    TEST_EXPECT(ctx, !image.remove_bundle(token));
    SResolvedSource source;
    TEST_EXPECT(ctx, image.resolve("work:/plain.txt", false, source));
    TEST_EXPECT(ctx, source.binding == 0u && source.bundle == 0u && source.physical_path.view() == CStringView{ "Some/Prefix/plain.txt" });
    TEST_EXPECT(ctx, image.resolve("PACK:/NESTED/FILE.bin", false, source));
    const auto direct = view.lookup(CStringView{ "nested/file.bin" });
    TEST_EXPECT(ctx, source.binding == token && source.bundle == 100u && source.size == 3u && source.physical_path.length() == 0u);
    TEST_EXPECT(ctx, view.bytes().data() + source.offset == direct.bytes.data());
    CSimpleString physical;
    TEST_EXPECT(ctx, !image.resolve("pack:/plain.txt", false, physical));
    for (const char* invalid : { "pack:/nested", "pack:/missing", "pack:/../plain.txt", "pack:/nested//file.bin", "pack:/plain.txt/", "pack:/nested\\file.bin" })
    {
        TEST_EXPECT(ctx, !image.resolve(invalid, false, source));
        TEST_EXPECT(ctx, source.binding == token && source.size == 3u);
    }
    TEST_EXPECT(ctx, !image.resolve("pack:/plain.txt", true, source));
    TEST_EXPECT(ctx, !image.written("pack:/new.txt", "ignored", 70u, 1u));
    TEST_EXPECT(ctx, image.loaded("pack:/plain.txt", 60u, 1u, 7u, 0u, token));
    TEST_EXPECT(ctx, image.cached_asset("PACK:/PLAIN.TXT", 1u) == 60u);
    TEST_EXPECT(ctx, image.cached_findings("pack:/plain.txt") == 7u);
    TEST_EXPECT(ctx, image.loaded("pack:/plain.txt", 61u, 1u, 8u, 0u, token));
    TEST_EXPECT(ctx, image.forget_asset(60u));
    TEST_EXPECT(ctx, image.cached_asset("pack:/plain.txt", 1u) == 61u);
    //  Physical loads and a different binding's results cannot seed this cache.
    TEST_EXPECT(ctx, image.loaded("pack:/plain.txt", 62u, 1u, 0u, 0u));
    TEST_EXPECT(ctx, image.loaded("pack:/plain.txt", 62u, 1u, 0u, 0u, other));
    TEST_EXPECT(ctx, image.cached_asset("pack:/plain.txt", 1u) == 61u);
    SRootScan scan;
    TEST_EXPECT(ctx, !image.prepare_scan("pack:", scan));
    TEST_EXPECT(ctx, image.integrate(doc, image.write_serial()));
    TEST_EXPECT(ctx, image.cached_asset("pack:/plain.txt", 1u) == 61u);
    TEST_EXPECT(ctx, image.resolve("pack:/nested/file.bin", false, source) && source.bundle == 100u);
    TEST_EXPECT(ctx, parse_image(R"({"roots":{"pack:":{"source":"bad","content":{}}}})", doc));
    TEST_EXPECT(ctx, !image.integrate(doc, 0u));
    CBakeInput capture;
    TEST_EXPECT(ctx, !image.capture_bake("pack:", "", capture));
    TEST_EXPECT(ctx, image.close_bundle(token));
    TEST_EXPECT(ctx, image.root_state("pack:") == ERootState::closed);
    TEST_EXPECT(ctx, !image.close_bundle(token));
    TEST_EXPECT(ctx, !image.resolve("pack:/plain.txt", false, source));
    TEST_EXPECT(ctx, !image.prepare_scan("pack:", scan));
    TEST_EXPECT(ctx, image.cached_asset("pack:/plain.txt", 1u) == 0u && image.cached_findings("pack:/plain.txt") == UINT32_MAX);
    TEST_EXPECT(ctx, image.loaded("pack:/plain.txt", 63u, 1u, 0u, 0u, token));
    TEST_EXPECT(ctx, !image.reserve_bundle("pack:", other));
    TEST_EXPECT(ctx, image.reserve_bundle("other:", other));
    TEST_EXPECT(ctx, !image.prepare_bundle(other, 100u, view));
    TEST_EXPECT(ctx, image.remove_bundle(other));
    TEST_EXPECT(ctx, image.remove_bundle(token));
    TEST_EXPECT(ctx, image.root_state("pack:") == ERootState::missing);
    TEST_EXPECT(ctx, !root(image.document(), "pack:").is_valid());
    TEST_EXPECT(ctx, image.reserve_bundle("pack:", other) && other != token);
    TEST_EXPECT(ctx, image.prepare_bundle(other, 101u, view) && image.publish_bundle(other));
    TEST_EXPECT(ctx, image.cached_asset("pack:/plain.txt", 1u) == 0u);
    TEST_EXPECT(ctx, image.loaded("pack:/plain.txt", 64u, 1u, 0u, 0u, token));
    TEST_EXPECT(ctx, image.cached_asset("pack:/plain.txt", 1u) == 0u);
    TEST_EXPECT(ctx, image.loaded("pack:/plain.txt", 65u, 1u, 0u, 0u, other));
    TEST_EXPECT(ctx, image.forget_asset(61u));
    TEST_EXPECT(ctx, image.cached_asset("pack:/plain.txt", 1u) == 65u);
    TEST_EXPECT(ctx, image.clear_cache() && image.cached_asset("pack:/plain.txt", 1u) == 0u);
    TEST_EXPECT(ctx, image.close_bundle(other) && image.remove_bundle(other));
    //  Prepared cancellation publishes nothing and releases its root/identity.
    TEST_EXPECT(ctx, image.reserve_bundle("cancel:", other));
    TEST_EXPECT(ctx, image.prepare_bundle(other, 102u, view) && image.remove_bundle(other));
    TEST_EXPECT(ctx, image.root_state("cancel:") == ERootState::missing);
    TEST_EXPECT(ctx, image.document().check_integrity());
    TEST_EXPECT(ctx, std::memcmp(original.data(), view.bytes().data(), original.size()) == 0);
}

struct SBindingAllocator { std::size_t calls{ 0u }; std::size_t fail_on{ SIZE_MAX }; };

static void* MV_STD_ABI_CALL allocate_binding_test(void* data, std::size_t alignment, std::size_t size) noexcept
{
    auto& state = *static_cast<SBindingAllocator*>(data);
    if (++state.calls == state.fail_on) { return nullptr; }
    return tests::allocate_test_memory(nullptr, alignment, size);
}

static void test_binding_allocation_failures(tests::TTestContext& ctx)
{
    using namespace filesystem_image;
    CBakedDocumentBlock block;
    bundle::CBundleView view;
    TEST_EXPECT(ctx, make_bundle(block, view));
    for (unsigned phase = 0u; phase != 5u; ++phase)
    {
        bool completed = false;
        std::size_t failures = 0u;
        for (std::size_t fail_on = 1u; fail_on < 1000u && !completed; ++fail_on)
        {
            SBindingAllocator state;
            memory::CMemoryAllocator allocator{ &state, &allocate_binding_test, &tests::deallocate_test_memory, system_ids::host };
            memory::CMemoryContext context{ allocator, system_ids::host };
            {
                const tests::TMemoryContextScope scope{ &context };
                CLiveDocument doc;
                CImage image;
                CBakeInput captured;
                TEST_EXPECT(ctx, parse_image(binding_image, doc) && image.adopt(std::move(doc)));
                TEST_EXPECT(ctx, image.capture_bake("work:", "", captured));
                const auto* retained_path = captured.sources()[0].physical_path;
                std::uint64_t token = 0u;
                if (phase != 0u && phase != 3u) { TEST_EXPECT(ctx, image.reserve_bundle("pack:", token)); }
                if (phase == 2u || phase == 4u) { TEST_EXPECT(ctx, image.prepare_bundle(token, 100u, view)); }
                if (phase == 4u)
                {
                    TEST_EXPECT(ctx, image.publish_bundle(token));
                    TEST_EXPECT(ctx, image.loaded("pack:/plain.txt", 200u, 1u, 0u, 0u, token));
                    TEST_EXPECT(ctx, parse_image(binding_image, doc));
                }
                CBakedDocumentBlock before;
                TEST_EXPECT(ctx, document_translation::bake(image.document(), before));
                state.fail_on = state.calls + fail_on;
                switch (phase)
                {
                    case 0u: completed = image.reserve_bundle("pack:", token); break;
                    case 1u: completed = image.prepare_bundle(token, 100u, view); break;
                    case 2u: completed = image.publish_bundle(token); break;
                    case 3u: completed = image.capture_bake("work:", "", captured); break;
                    case 4u: completed = image.integrate(doc, 0u); break;
                }
                state.fail_on = SIZE_MAX;
                if (!completed)
                {
                    ++failures;
                    CBakedDocumentBlock after;
                    TEST_EXPECT(ctx, document_translation::bake(image.document(), after));
                    TEST_EXPECT(ctx, before.bytes().size() == after.bytes().size() &&
                        std::memcmp(before.bytes().data(), after.bytes().data(), before.bytes().size()) == 0);
                    const ERootState expected[]{ ERootState::missing, ERootState::reserved, ERootState::prepared, ERootState::missing, ERootState::open };
                    TEST_EXPECT(ctx, image.root_state("pack:") == expected[phase]);
                    TEST_EXPECT(ctx, captured.sources()[0].physical_path == retained_path);
                    if (phase == 0u) { TEST_EXPECT(ctx, token == 0u); }
                }
                if (phase == 4u) { TEST_EXPECT(ctx, image.cached_asset("pack:/plain.txt", 1u) == 200u); }
                TEST_EXPECT(ctx, image.document().check_integrity());
                //  Closure/removal must not allocate, even under exhaustion.
                const ERootState root_status = image.root_state("pack:");
                state.fail_on = state.calls + 1u;
                if (root_status == ERootState::open) { TEST_EXPECT(ctx, image.close_bundle(token)); }
                if (root_status != ERootState::missing) { TEST_EXPECT(ctx, image.remove_bundle(token)); }
            }
            if (!context.is_attribution_empty())
            {
                std::cout << "Binding allocation failure: phase " << phase << ", allocation " << fail_on
                    << ", retained bytes " << context.get_live_allocated_bytes() << '\n';
            }
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }
        TEST_EXPECT(ctx, completed && failures != 0u);
    }
}

static void test_deep_bundle_binding(tests::TTestContext& ctx)
{
    CLiveDocument input;
    TEST_EXPECT(ctx, input.initialise());
    CNodeKey content = input.root();
    std::string logical = "pack:/";
    for (unsigned i = 0u; i != 260u; ++i)
    {
        const CNodeKey directory = input.create_object(CStringView{ "deep" });
        TEST_EXPECT(ctx, input.append_child(content, directory).succeeded());
        content = input.create_object(CStringView{ "content" });
        TEST_EXPECT(ctx, input.append_child(directory, content).succeeded());
        logical += "deep/";
    }
    const CNodeKey file = input.create_object(CStringView{ "file" });
    TEST_EXPECT(ctx, input.append_child(content, file).succeeded());
    const bundle::SBuildSource source{ file, "unused" };
    const bundle::detail::SBuildIo io{ nullptr,
        [](void*, const char*, std::size_t& size) noexcept { size = 1u; return true; },
        [](void*, const char*, const CByteView& bytes) noexcept { bytes.data()[0] = 'a'; return true; } };
    CBakedDocumentBlock block;
    bundle::CBundleView view;
    TEST_EXPECT(ctx, bundle::detail::build(input, input.root(), { &source, 1u }, block, view, io).succeeded());
    CLiveDocument doc;
    TEST_EXPECT(ctx, parse_image(binding_image, doc));
    filesystem_image::CImage image;
    TEST_EXPECT(ctx, image.adopt(std::move(doc)));
    std::uint64_t binding = 0u;
    TEST_EXPECT(ctx, image.reserve_bundle("pack:", binding));
    TEST_EXPECT(ctx, image.prepare_bundle(binding, 1u, view) && image.publish_bundle(binding));
    logical += "file";
    filesystem_image::SResolvedSource result;
    TEST_EXPECT(ctx, image.resolve(logical.c_str(), false, result) && result.size == 1u);
    TEST_EXPECT(ctx, parse_image(binding_image, doc) && image.integrate(doc, 0u));
    TEST_EXPECT(ctx, image.resolve(logical.c_str(), false, result) && result.bundle == 1u);
    TEST_EXPECT(ctx, image.close_bundle(binding) && image.remove_bundle(binding));
}

static void test_binding_owner_moves(tests::TTestContext& ctx)
{
    using namespace filesystem_image;
    CBakedDocumentBlock block;
    bundle::CBundleView view;
    TEST_EXPECT(ctx, make_bundle(block, view));
    memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_test_memory, &tests::deallocate_test_memory, system_ids::host };
    memory::CMemoryContext context{ allocator, system_ids::host };
    {
        const tests::TMemoryContextScope scope{ &context };
        CLiveDocument doc;
        CImage original, replaced;
        TEST_EXPECT(ctx, parse_image(binding_image, doc) && original.adopt(std::move(doc)));
        TEST_EXPECT(ctx, parse_image(binding_image, doc) && replaced.adopt(std::move(doc)));
        std::uint64_t binding = 0u, previous = 0u;
        TEST_EXPECT(ctx, original.reserve_bundle("pack:", binding) && original.prepare_bundle(binding, 1u, view) && original.publish_bundle(binding));
        TEST_EXPECT(ctx, replaced.reserve_bundle("old:", previous) && replaced.prepare_bundle(previous, 2u, view));
        TEST_EXPECT(ctx, original.loaded("pack:/plain.txt", 10u, 1u, 0u, 0u, binding));
        TEST_EXPECT(ctx, replaced.loaded("work:/plain.txt", 20u, 1u, 0u, 0u));
        CBakeInput captured, target;
        TEST_EXPECT(ctx, original.capture_bake("work:", "", captured) && replaced.capture_bake("work:", "", target));
        const char* const path = captured.sources()[0].physical_path;
        target = std::move(captured);
        target = std::move(target);
        TEST_EXPECT(ctx, target.sources()[0].physical_path == path);
        CImage moved{ std::move(original) };
        replaced = std::move(moved);
        replaced = std::move(replaced);
        TEST_EXPECT(ctx, replaced.cached_asset("pack:/plain.txt", 1u) == 10u);
        TEST_EXPECT(ctx, replaced.root_state("old:") == ERootState::missing);
        SResolvedSource source;
        TEST_EXPECT(ctx, replaced.resolve("pack:/plain.txt", false, source) && source.bundle == 1u);
        TEST_EXPECT(ctx, replaced.close_bundle(binding) && replaced.remove_bundle(binding));
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static void test_bake_capture(tests::TTestContext& ctx)
{
    using namespace filesystem_image;
    namespace fs = std::filesystem;
    const std::string base = fs::path(test_environment::test_output_path("capture-fixture")).generic_string();
    std::error_code error;
    TEST_EXPECT(ctx, fs::create_directories(base + "/Content/Nested/Empty", error) && !error);
    TEST_EXPECT(ctx, fs::create_directory(base + "/Dlls", error) && !error);
    TEST_EXPECT(ctx, write(base + "/Content/Nested/File.TXT", "captured"));
    TEST_EXPECT(ctx, write(base + "/Dlls/Runtime.DLL", "module"));
    SRootScan scan;
    TEST_EXPECT(ctx, scan.logical_root.set("work:") && scan.physical_path.set((base + "/Content").c_str()) &&
        scan.redirect_directory.set((base + "/Dlls").c_str()));
    CLiveDocument doc;
    TEST_EXPECT(ctx, scan_root(scan, doc) == EScanStatus::success);
    CImage image;
    TEST_EXPECT(ctx, image.adopt(std::move(doc)));
    CBakeInput full, nested;
    TEST_EXPECT(ctx, image.capture_bake("WORK:", "", full));
    TEST_EXPECT(ctx, full.sources().size() == 2u);
    TEST_EXPECT(ctx, image.capture_bake("work:", "NESTED", nested));
    TEST_EXPECT(ctx, nested.sources().size() == 1u);
    if (nested.sources().size() != 1u) { return; }
    const char* const path = nested.sources()[0].physical_path;
    TEST_EXPECT(ctx, CStringView{ path } == CStringView{ (base + "/Content/Nested/File.TXT").c_str() });
    TEST_EXPECT(ctx, member(nested.inventory(), nested.content(), "file.txt") == nested.sources()[0].entry);
    for (const char* invalid : { "/nested", "nested/", "nested//empty", "nested/../", "nested\\empty", "nested/file.txt", "missing" })
    {
        TEST_EXPECT(ctx, !image.capture_bake("work:", invalid, nested));
        TEST_EXPECT(ctx, nested.sources()[0].physical_path == path);
    }
    TEST_EXPECT(ctx, !image.capture_bake("missing:", "", nested));
    TEST_EXPECT(ctx, !image.capture_bake("work:", nullptr, nested));
    //  Replacing the live image cannot affect the already captured directory.
    TEST_EXPECT(ctx, parse_image(R"({"roots":{"work:":{"source":"replacement","content":{}}}})", doc));
    TEST_EXPECT(ctx, image.integrate(doc, 0u));
    CBakeInput moved = std::move(nested);
    TEST_EXPECT(ctx, moved.sources()[0].physical_path == path);
    CBakedDocumentBlock block;
    bundle::CBundleView view;
    TEST_EXPECT(ctx, bundle::build(moved.inventory(), moved.content(), moved.sources(), block, view).succeeded());
    const auto file = view.lookup(CStringView{ "FILE.txt" });
    TEST_EXPECT(ctx, file.status == bundle::ELookupStatus::success && file.bytes.bytes() == 8u && std::memcmp(file.bytes.data(), "captured", 8u) == 0);
    TEST_EXPECT(ctx, bundle::build(full.inventory(), full.content(), full.sources(), block, view).succeeded());
    TEST_EXPECT(ctx, view.lookup(CStringView{ "bin/runtime.dll" }).status == bundle::ELookupStatus::success);
    TEST_EXPECT(ctx, view.lookup(CStringView{ "nested/file.txt" }).status == bundle::ELookupStatus::success);
    for (const auto& suffix : { "/Content/Nested/File.TXT", "/Content/Nested/Empty", "/Content/Nested", "/Content", "/Dlls/Runtime.DLL", "/Dlls", "" })
    {
        TEST_EXPECT(ctx, fs::remove(base + suffix, error) && !error);
    }
}

}   //  namespace filesystem_image_tests

int run_filesystem_image_tests()
{
    tests::TTestContext ctx;
    filesystem_image_tests::test_development_image(ctx);
    filesystem_image_tests::test_refresh(ctx);
    filesystem_image_tests::test_case_identity(ctx);
    filesystem_image_tests::test_manual_case_mapping(ctx);
    filesystem_image_tests::test_virtual_bindings(ctx);
    filesystem_image_tests::test_bake_capture(ctx);
    filesystem_image_tests::test_binding_allocation_failures(ctx);
    filesystem_image_tests::test_deep_bundle_binding(ctx);
    filesystem_image_tests::test_binding_owner_moves(ctx);
    std::cout << "FilesystemImage: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
