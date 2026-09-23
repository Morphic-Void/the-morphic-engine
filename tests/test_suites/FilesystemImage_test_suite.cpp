
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
#include "tests/test_suites/FilesystemImage_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "tests/environment/test_paths.hpp"
#include "filesystem/filesystem_image.hpp"
#include "platform/filesystem/directory.hpp"
#include "platform/filesystem/file.hpp"
#include "data_model/baked_document.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"

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
    image.adopt(std::move(document));
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
    image.adopt(std::move(document));
    TEST_EXPECT(ctx, image.document().child_count(entry(image.document(), "work:", "a.json")) == 0u);
    CSimpleString nested_path;
    TEST_EXPECT(ctx, image.resolve("work:/empty/content", false, nested_path));
    TEST_EXPECT(ctx, !image.resolve("work:/A.json", true, nested_path));
    TEST_EXPECT(ctx, !image.resolve("work:/EMPTY/new.json", true, nested_path));

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

}   //  namespace filesystem_image_tests

int run_filesystem_image_tests()
{
    tests::TTestContext ctx;
    filesystem_image_tests::test_development_image(ctx);
    filesystem_image_tests::test_refresh(ctx);
    std::cout << "FilesystemImage: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
