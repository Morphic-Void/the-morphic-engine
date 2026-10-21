
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    BundleBuilder_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    12 Oct 26
//
//  Deterministic construction, direct I/O, filtering and failure preservation.

#include "tests/test_suites/BundleBuilder_test_suite.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "assets/bundle_builder.hpp"
#include "assets/internal/bundle_builder_io.hpp"
#include "data_model/document_translation.hpp"
#include "platform/filesystem/file.hpp"
#include "platform/filesystem/internal/file_utils.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/test_scopes.hpp"

namespace
{

using tests::TTestContext;
using bundle::EBuildStatus;
namespace fs = platform::filesystem;

struct SInputs
{
    CLiveDocument inventory;
    CNodeKey content;
    std::vector<bundle::SBuildSource> sources;
    std::array<std::string, 7u> paths;
    std::array<std::size_t, 7u> sizes{ 1u, 127u, 128u, 129u, 255u, 256u, 0u };

    ~SInputs()
    {
        for (const auto& path : paths) { if (!path.empty()) { fs::removeFile(platform::path::makeNativePath(path.c_str())); } }
    }

    CNodeKey directory(TTestContext& ctx, CNodeKey parent, const char* name)
    {
        const auto entry = tests::add_document_child(ctx, inventory, parent, inventory.create_object(CStringView{ name }));
        return tests::add_document_child(ctx, inventory, entry, inventory.create_object(CStringView{ "content" }));
    }

    void initialise(TTestContext& ctx, bool reversed, bool real_files = true)
    {
        TEST_EXPECT(ctx, inventory.initialise());
        // The selected subtree is below an unrelated root property.
        content = directory(ctx, inventory.root(), "selected");
        tests::add_document_child(ctx, inventory, inventory.root(), inventory.create_boolean(true, CStringView{ "ignored" }));
        const auto nested = directory(ctx, content, "nested");
        const auto parent = inventory.parent(nested);
        tests::add_document_child(ctx, inventory, parent, inventory.create_string(CStringView{ "Nested" }, CStringView{ "physicalName" }));
        directory(ctx, directory(ctx, content, "emptytree"), "emptychild");
        const auto only_empty = directory(ctx, content, "onlyempty");
        const auto other = tests::add_document_child(ctx, inventory, content, inventory.create_object(CStringView{ "unsupported" }));
        tests::add_document_child(ctx, inventory, other, inventory.create_string(CStringView{ "other" }, CStringView{ "kind" }));
        const char* names[]{ "z", "a", "file", "b", "\xc3\xa9", "size", "empty" };
        for (std::size_t ordinal = 0u; ordinal < sizes.size(); ++ordinal)
        {
            const std::size_t i = reversed ? sizes.size() - 1u - ordinal : ordinal;
            const auto file = tests::add_document_child(ctx, inventory, (i == 2u) ? nested : ((i == 6u) ? only_empty : content),
                inventory.create_object(CStringView{ names[i] }));
            if (i == 2u) { tests::add_document_child(ctx, inventory, file, inventory.create_string(CStringView{ "File" }, CStringView{ "physicalName" })); }
            // Runtime/source metadata is deliberately varied and must not leak.
            tests::add_document_child(ctx, inventory, file, inventory.create_unsigned_integer(reversed ? 999u : 123u, CStringView{ "asset" }));
            tests::add_document_child(ctx, inventory, file, inventory.create_string(CStringView{ "not serialized" }, CStringView{ "physical" }));
            paths[i] = test_environment::test_output_path((std::string{ reversed ? "builder-reverse-" : "builder-forward-" } + std::to_string(i) + ".bin").c_str());
            sources.push_back({ file, paths[i].c_str() });
            if (!real_files) { continue; }
            if (sizes[i] == 0u)
            {
                auto* handle = fs::openFile(platform::path::makeNativePath(paths[i].c_str()), fs::EOpenMode::BinaryWrite);
                TEST_EXPECT(ctx, handle != nullptr);
                if (handle != nullptr) { TEST_EXPECT(ctx, std::fclose(handle) == 0); }
            }
            else
            {
                std::vector<std::uint8_t> bytes(sizes[i], static_cast<std::uint8_t>(i + 1u));
                bytes[0] = 0u; // Payload zeroes are content, not terminators.
                TEST_EXPECT(ctx, fs::saveFile(paths[i].c_str(), CByteConstView{ bytes.data(), bytes.size() }));
            }
        }
        if (reversed) { std::reverse(sources.begin(), sources.end()); }
    }

    TPodConstView<bundle::SBuildSource> files() const { return { sources.data(), sources.size() }; }
};

void check_order(TTestContext& ctx, const CBakedDocument& doc, CBakedValueIndex parent, std::initializer_list<const char*> names)
{
    TEST_EXPECT(ctx, doc.child_count(parent) == names.size());
    auto entry = doc.first_child(parent);
    for (const auto* name : names)
    {
        TEST_EXPECT(ctx, doc.name(entry) == CStringView{ name });
        entry = doc.next_sibling(entry);
    }
    TEST_EXPECT(ctx, !entry.is_valid());
}

void test_native_round_trip(TTestContext& ctx)
{
    SInputs forward;
    SInputs reverse;
    forward.initialise(ctx, false);
    reverse.initialise(ctx, true);
    CBakedDocumentBlock before;
    TEST_EXPECT(ctx, document_translation::bake(forward.inventory, before));
    CBakedDocumentBlock a;
    CBakedDocumentBlock b;
    bundle::CBundleView av;
    bundle::CBundleView bv;
    TEST_EXPECT(ctx, bundle::build(forward.inventory, forward.content, forward.files(), a, av).succeeded());
    TEST_EXPECT(ctx, bundle::build(reverse.inventory, reverse.content, reverse.files(), b, bv).succeeded());
    TEST_EXPECT(ctx, av.is_ready() && bv.is_ready());
    if (!av.is_ready() || !bv.is_ready()) { return; }
    TEST_EXPECT(ctx, av.bytes().size() == bv.bytes().size());
    if (av.bytes().size() == bv.bytes().size()) { TEST_EXPECT(ctx, std::memcmp(av.bytes().data(), bv.bytes().data(), av.bytes().size()) == 0); }
    CBakedDocumentBlock after;
    TEST_EXPECT(ctx, document_translation::bake(forward.inventory, after));
    TEST_EXPECT(ctx, before.bytes().size() == after.bytes().size());
    if (before.bytes().size() == after.bytes().size()) { TEST_EXPECT(ctx, std::memcmp(before.bytes().data(), after.bytes().data(), before.bytes().size()) == 0); }
    const auto doc = av.document();
    const auto content = tests::baked_document_child(doc, doc.root(), "content");
    check_order(ctx, doc, doc.root(), { "fileType", "version", "size", "content" });
    check_order(ctx, doc, content, { "a", "b", "nested", "size", "z", "\xc3\xa9" });
    const auto nested = tests::baked_document_child(doc, content, "nested");
    check_order(ctx, doc, nested, { "physicalName", "content" });
    const auto file = av.lookup(CStringView{ "NESTED/FILE" });
    check_order(ctx, doc, file.entry, { "physicalName", "offset", "size" });
    check_order(ctx, doc, tests::baked_document_child(doc, content, "a"), { "offset", "size" });
    TEST_EXPECT(ctx, av.lookup(CStringView{ "empty" }).status == bundle::ELookupStatus::not_found);
    TEST_EXPECT(ctx, av.lookup(CStringView{ "emptytree" }).status == bundle::ELookupStatus::not_found);
    TEST_EXPECT(ctx, av.lookup(CStringView{ "onlyempty" }).status == bundle::ELookupStatus::not_found);
    TEST_EXPECT(ctx, av.lookup(CStringView{ "unsupported" }).status == bundle::ELookupStatus::not_found);
    const char* ordered[]{ "a", "b", "nested/file", "size", "z", "\xc3\xa9" };
    const std::size_t source_indices[]{ 1u, 3u, 2u, 5u, 0u, 4u };
    std::size_t expected_offset = ((doc.byte_count() + 127u) / 128u) * 128u;
    for (std::size_t i = doc.byte_count(); i < expected_offset; ++i) { TEST_EXPECT(ctx, av.bytes().data()[i] == 0u); }
    for (std::size_t i = 0u; i < 6u; ++i)
    {
        const auto found = av.lookup(CStringView{ ordered[i] });
        const auto source_index = source_indices[i];
        TEST_EXPECT(ctx, found.status == bundle::ELookupStatus::success);
        TEST_EXPECT(ctx, found.bytes.bytes() == forward.sizes[source_index]);
        TEST_EXPECT(ctx, found.bytes.data() == av.bytes().data() + expected_offset && found.bytes.storage_alignment() == 128u);
        const auto* bytes = static_cast<const std::uint8_t*>(found.bytes.data());
        if (bytes != nullptr)
        {
            TEST_EXPECT(ctx, bytes[0] == 0u);
            for (std::size_t j = 1u; j < found.bytes.bytes(); ++j) { TEST_EXPECT(ctx, bytes[j] == source_index + 1u); }
        }
        const auto end = expected_offset + ((found.bytes.bytes() + 128u) / 128u) * 128u;
        for (auto j = expected_offset + found.bytes.bytes(); j < end; ++j) { TEST_EXPECT(ctx, av.bytes().data()[j] == 0u); }
        expected_offset = end;
    }
    TEST_EXPECT(ctx, expected_offset == av.bytes().size());
    const auto path = test_environment::test_output_path("builder-roundtrip.bdl");
    TEST_EXPECT(ctx, fs::saveFile(path.c_str(), av.bytes()));
    CByteBuffer loaded = fs::loadFile(path.c_str(), 0u, 128u);
    CBakedDocumentBlock adopted;
    bundle::CBundleView loaded_view;
    TEST_EXPECT(ctx, bundle::adopt(std::move(loaded), adopted, loaded_view).succeeded());
    TEST_EXPECT(ctx, loaded_view.bytes().size() == av.bytes().size());
    if (loaded_view.bytes().size() == av.bytes().size()) { TEST_EXPECT(ctx, std::memcmp(loaded_view.bytes().data(), av.bytes().data(), av.bytes().size()) == 0); }
    fs::removeFile(platform::path::makeNativePath(path.c_str()));

    // Selecting only an empty file's directory yields no bundle through real I/O.
    const auto empty_entry = forward.sources.back().entry;
    const bundle::SBuildSource empty_source{ empty_entry, forward.paths.back().c_str() };
    const auto* retained = a.buffer_bytes().data();
    const auto empty_result = bundle::build(forward.inventory, forward.inventory.parent(empty_entry),
        TPodConstView<bundle::SBuildSource>{ &empty_source, 1u }, a, av);
    TEST_EXPECT(ctx, empty_result.status == EBuildStatus::no_content);
    TEST_EXPECT(ctx, a.buffer_bytes().data() == retained && av.bytes().data() == retained);
}

struct SIo
{
    enum class EMode { normal, empty, query_failure, read_failure, oversized, total_overflow, remove_before_read };
    EMode mode{ EMode::normal };
    std::size_t queries{ 0u };
    std::size_t reads{ 0u };
    std::vector<const void*> destinations;
    static bool query(void* data, const char* path, std::size_t& size) noexcept
    {
        auto& io = *static_cast<SIo*>(data);
        ++io.queries;
        if (io.mode == EMode::query_failure) { return false; }
        if (io.mode == EMode::remove_before_read)
        {
            if (!fs::queryFileSize(path, size)) { return false; }
            fs::removeFile(platform::path::makeNativePath(path));
            return true;
        }
        size = (io.mode == EMode::empty) ? 0u : ((io.mode == EMode::oversized) ? SIZE_MAX :
            ((io.mode == EMode::total_overflow) ? memory::k_byte_size_ceiling - 128u : 128u));
        return true;
    }
    static bool read(void* data, const char* path, const CByteView& bytes) noexcept
    {
        auto& io = *static_cast<SIo*>(data);
        ++io.reads;
        io.destinations.push_back(bytes.data());
        if (io.mode == EMode::remove_before_read) { return fs::loadFileInto(path, bytes); }
        std::memset(bytes.data(), 0xa5, bytes.size());
        return io.mode != EMode::read_failure;
    }
    bundle::detail::SBuildIo operations() { return { this, &query, &read }; }
};

void test_failures_and_direct_reads(TTestContext& ctx)
{
    SInputs input;
    input.initialise(ctx, false, false);
    CBakedDocumentBlock owner;
    bundle::CBundleView view;
    SIo initial;
    TEST_EXPECT(ctx, bundle::detail::build(input.inventory, input.content, input.files(), owner, view, initial.operations()).succeeded());
    TEST_EXPECT(ctx, initial.queries == input.sources.size() && initial.reads == input.sources.size());
    for (const auto* pointer : initial.destinations)
    {
        TEST_EXPECT(ctx, (pointer >= view.bytes().data()) && (pointer < view.bytes().data() + view.bytes().size()));
    }
    const auto* original = owner.buffer_bytes().data();
    const auto original_size = view.bytes().size();
    const auto snapshot = std::vector<std::uint8_t>(view.bytes().data(), view.bytes().data() + view.bytes().size());
    const std::pair<SIo::EMode, EBuildStatus> cases[]{
        { SIo::EMode::empty, EBuildStatus::no_content }, { SIo::EMode::query_failure, EBuildStatus::size_query_failed },
        { SIo::EMode::read_failure, EBuildStatus::read_failed }, { SIo::EMode::oversized, EBuildStatus::too_large },
        { SIo::EMode::total_overflow, EBuildStatus::too_large } };
    for (const auto& test : cases)
    {
        SIo io;
        io.mode = test.first;
        const auto result = bundle::detail::build(input.inventory, input.content, input.files(), owner, view, io.operations());
        TEST_EXPECT(ctx, result.status == test.second && result.entry.is_valid());
        TEST_EXPECT(ctx, owner.buffer_bytes().data() == original && view.bytes().data() == original);
        TEST_EXPECT(ctx, view.bytes().size() == original_size && std::memcmp(original, snapshot.data(), snapshot.size()) == 0);
        TEST_EXPECT(ctx, io.reads == ((test.first == SIo::EMode::read_failure) ? 1u : 0u));
    }
    TEST_EXPECT(ctx, bundle::build(input.inventory, input.content, input.files(), owner, view).status == EBuildStatus::size_query_failed);
    // Real files disappear between the successful query phase and native reads.
    SInputs real;
    real.initialise(ctx, true);
    SIo removed;
    removed.mode = SIo::EMode::remove_before_read;
    const auto result = bundle::detail::build(real.inventory, real.content, real.files(), owner, view, removed.operations());
    TEST_EXPECT(ctx, result.status == EBuildStatus::read_failed && result.entry.is_valid());
    TEST_EXPECT(ctx, owner.buffer_bytes().data() == original && view.bytes().data() == original);
    TEST_EXPECT(ctx, std::memcmp(original, snapshot.data(), snapshot.size()) == 0);
}

void test_input_contract(TTestContext& ctx)
{
    SInputs input;
    input.initialise(ctx, false, false);
    CBakedDocumentBlock owner;
    bundle::CBundleView view;
    SIo io;
    const auto test = [&](TPodConstView<bundle::SBuildSource> sources)
    {
        const auto result = bundle::detail::build(input.inventory, input.content, sources, owner, view, io.operations());
        TEST_EXPECT(ctx, result.status == EBuildStatus::invalid_input);
        TEST_EXPECT(ctx, !owner.is_ready() && !view.is_ready() && io.queries == 0u);
    };
    test({});
    const auto first = input.sources.front();
    input.sources.front().physical_path = nullptr;
    test(input.files());
    input.sources.front() = first;
    input.sources.push_back(first);
    test(input.files());
    input.sources.pop_back();
    input.sources.push_back({ input.content, "extra" });
    test(input.files());
    input.sources.pop_back();
    TEST_EXPECT(ctx, input.inventory.set_name(first.entry, CStringView{ "Invalid" }));
    test(input.files());

    CLiveDocument empty;
    TEST_EXPECT(ctx, empty.initialise());
    TEST_EXPECT(ctx, bundle::build(empty, empty.root(), {}, owner, view).status == EBuildStatus::no_content);
    TEST_EXPECT(ctx, bundle::build(empty, {}, {}, owner, view).status == EBuildStatus::invalid_input);
}

void test_allocation_failures(TTestContext& ctx)
{
    SInputs input;
    input.initialise(ctx, false, false);
    CBakedDocumentBlock original;
    bundle::CBundleView original_view;
    SIo initial;
    TEST_EXPECT(ctx, bundle::detail::build(input.inventory, input.content, input.files(), original, original_view, initial.operations()).succeeded());
    bool succeeded = false;
    std::size_t failures = 0u;
    for (std::size_t fail_on = 1u; fail_on < 1000u && !succeeded; ++fail_on)
    {
        tests::TAllocationFailureFixture state;
        memory::CMemoryAllocator allocator{ &state, &tests::allocate_with_failure, &tests::deallocate_test_memory, system_ids::host };
        memory::CMemoryContext context{ allocator, system_ids::host };
        {
            const tests::TMemoryContextScope scope{ &context };
            CBakedDocumentBlock block;
            bundle::CBundleView view;
            CByteBuffer copy;
            TEST_EXPECT(ctx, copy.construct_and_copy_from(original_view.bytes()));
            TEST_EXPECT(ctx, bundle::adopt(std::move(copy), block, view).succeeded());
            const auto* retained = block.buffer_bytes().data();
            state.fail_on = state.attempts + fail_on - 1u;
            SIo io;
            const auto result = bundle::detail::build(input.inventory, input.content, input.files(), block, view, io.operations());
            succeeded = result.succeeded();
            if (!succeeded)
            {
                ++failures;
                TEST_EXPECT(ctx, block.buffer_bytes().data() == retained && view.bytes().data() == retained);
                TEST_EXPECT(ctx, view.bytes().size() == original_view.bytes().size());
                TEST_EXPECT(ctx, std::memcmp(view.bytes().data(), original_view.bytes().data(), original_view.bytes().size()) == 0);
            }
            else { TEST_EXPECT(ctx, block.is_ready() && view.is_ready()); }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, succeeded && failures > 10u);
}

}   // namespace

int run_bundle_builder_tests()
{
    TTestContext ctx;
    test_native_round_trip(ctx);
    test_failures_and_direct_reads(ctx);
    test_input_contract(ctx);
    test_allocation_failures(ctx);
    return ctx.report("BundleBuilder");
}
