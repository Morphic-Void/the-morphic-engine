
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    Bundle_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    12 Oct 26
//
//  Bundle validation, lookup, adoption and concurrent-read tests.

#include "tests/test_suites/Bundle_test_suite.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "assets/bundle.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/live_document.hpp"
#include "filesystem/logical_path.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "assets/asset_messages.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/test_scopes.hpp"

namespace
{

using tests::TTestContext;
using bundle::CBundleView;
using bundle::EValidationStatus;
using bundle::ELookupStatus;

static_assert(sizeof(CBundleView) <= sizeof(AssetViews));
static_assert(alignof(CBundleView) <= alignof(AssetViews));

struct SFixture
{
    CLiveDocument live;
    CNodeKey content;
    CNodeKey directory;
    CNodeKey nested;
    std::vector<CNodeKey> files;
    CBakedDocumentBlock block;
    std::vector<CBakedValueIndex> entries;
    std::vector<std::size_t> offsets;
    std::vector<std::size_t> sizes;

    void initialise(TTestContext& ctx)
    {
        TEST_EXPECT(ctx, live.initialise());
        tests::add_document_child(ctx, live, live.root(), live.create_string(CStringView{ "morphic-bundle" }, CStringView{ "fileType" }));
        tests::add_document_child(ctx, live, live.root(), live.create_unsigned_integer(1u, CStringView{ "version" }));
        tests::add_document_child(ctx, live, live.root(), live.create_unsigned_integer(0u, CStringView{ "size" }));
        content = tests::add_document_child(ctx, live, live.root(), live.create_object(CStringView{ "content" }));
        directory = tests::add_document_child(ctx, live, content, live.create_object(CStringView{ "shaders" }));
        tests::add_document_child(ctx, live, directory, live.create_string(CStringView{ "Shaders" }, CStringView{ "physicalName" }));
        nested = tests::add_document_child(ctx, live, directory, live.create_object(CStringView{ "content" }));
        const std::size_t lengths[]{ 1u, 127u, 128u, 129u, 255u, 256u };
        for (std::size_t i = 0u; i < 6u; ++i)
        {
            const std::string name = "file" + std::to_string(i);
            const CNodeKey file = add_file(ctx, nested, name.c_str(), lengths[i]);
            if (i == 0u) { tests::add_document_child(ctx, live, file, live.create_string(CStringView{ "File0" }, CStringView{ "physicalName" })); }
        }
        add_file(ctx, content, "size", 3u); // A filename matching a metadata key.
        add_file(ctx, content, "\xc3\xa9", 2u);
    }

    CNodeKey add_file(TTestContext& ctx, CNodeKey parent, const char* name, std::size_t size)
    {
        const CNodeKey file = tests::add_document_child(ctx, live, parent, live.create_object(CStringView{ name }));
        tests::add_document_child(ctx, live, file, live.create_unsigned_integer(0u, CStringView{ "offset" }));
        tests::add_document_child(ctx, live, file, live.create_unsigned_integer(size, CStringView{ "size" }));
        files.push_back(file);
        return file;
    }

    void collect(const CBakedDocument& doc, CBakedValueIndex node)
    {
        std::uint64_t size = 0u;
        if (tests::baked_document_child(doc, node, "offset").is_valid() && doc.unsigned_integer_value(tests::baked_document_child(doc, node, "size"), size))
        {
            entries.push_back(node);
            sizes.push_back(static_cast<std::size_t>(size));
        }
        for (auto next = doc.first_child(node); next.is_valid(); next = doc.next_sibling(next)) { collect(doc, next); }
    }

    void pack(TTestContext& ctx, bool reverse = true)
    {
        // Test-only assembler: uses document baking, never the bundle validator
        // or future production builder to decide layout or content.
        TEST_EXPECT(ctx, document_translation::bake(live, block));
        if (!block.is_ready()) { return; }
        entries.clear(); sizes.clear(); offsets.clear();
        collect(block.document(), block.document().root());
        if (reverse) { std::reverse(entries.begin(), entries.end()); std::reverse(sizes.begin(), sizes.end()); }
        std::size_t total = ((block.bytes().size() + 127u) / 128u) * 128u;
        {
            CMutableBakedDocument editor{ block };
            for (std::size_t i = 0u; i < entries.size(); ++i)
            {
                offsets.push_back(total);
                const auto offset = tests::baked_document_child(editor.baked(), entries[i], "offset");
                std::uint64_t unused = 0u;
                if (editor.baked().unsigned_integer_value(offset, unused))
                {
                    TEST_EXPECT(ctx, editor.set_unsigned_integer_value(offset, total));
                }
                total += ((sizes[i] + 128u) / 128u) * 128u;
            }
            const auto size = tests::baked_document_child(editor.baked(), editor.baked().root(), "size");
            std::uint64_t unused = 0u;
            if (editor.baked().unsigned_integer_value(size, unused)) { TEST_EXPECT(ctx, editor.set_unsigned_integer_value(size, total)); }
        }
        TEST_EXPECT(ctx, block.resize_buffer(total, 128u));
        for (std::size_t i = 0u; i < entries.size(); ++i)
        {
            std::memset(block.buffer_bytes().data() + offsets[i], static_cast<int>(i + 1u), sizes[i]);
        }
    }

    bundle::SValidationResult check(CBundleView& view) const
    {
        return view.reset(block.buffer_bytes().data(), block.buffer_bytes().size());
    }

    void set(TTestContext& ctx, CBakedValueIndex entry, const char* property, std::uint64_t value)
    {
        CMutableBakedDocument editor{ block };
        TEST_EXPECT(ctx, editor.set_unsigned_integer_value(tests::baked_document_child(editor.baked(), entry, property), value));
    }
};

void test_views_and_lookup(TTestContext& ctx)
{
    for (const bool reverse : { false, true })
    {
        SFixture fixture;
        fixture.initialise(ctx);
        fixture.pack(ctx, reverse);
        CBundleView view;
        TEST_EXPECT(ctx, fixture.check(view).succeeded());
        TEST_EXPECT(ctx, view.is_ready() && view.document().byte_count() == fixture.block.bytes().size());
        TEST_EXPECT(ctx, view.bytes().size() == fixture.block.buffer_bytes().size() && view.bytes().align() == 128u);
        for (std::size_t i = 0u; i < fixture.entries.size(); ++i)
        {
            const auto name = view.document().name(fixture.entries[i]);
            const bool at_root = view.document().parent(fixture.entries[i]) == tests::baked_document_child(view.document(), view.document().root(), "content");
            const std::string path = at_root ? name.cstring() : std::string{ "SHADERS/" } + name.cstring();
            const auto found = view.lookup(CStringView{ path.c_str() });
            TEST_EXPECT(ctx, found.status == ELookupStatus::success);
            TEST_EXPECT(ctx, found.entry == fixture.entries[i]);
            TEST_EXPECT(ctx, found.bytes.data() == view.bytes().data() + fixture.offsets[i]);
            TEST_EXPECT(ctx, found.bytes.bytes() == fixture.sizes[i] && found.bytes.storage_alignment() == 128u);
        }
        const auto found = view.lookup(CStringView{ "ShAdErS/FILE0" });
        TEST_EXPECT(ctx, found.status == ELookupStatus::success && found.bytes.bytes() == 1u);
        TEST_EXPECT(ctx, view.document().string_value(tests::baked_document_child(view.document(), found.entry, "physicalName")) == CStringView{ "File0" });
        TEST_EXPECT(ctx, view.lookup(CStringView{ "shaders" }).status == ELookupStatus::not_file);
        TEST_EXPECT(ctx, view.lookup(CStringView{ "missing" }).status == ELookupStatus::not_found);
        TEST_EXPECT(ctx, view.lookup(CStringView{ "size/child" }).status == ELookupStatus::not_found);
        TEST_EXPECT(ctx, view.lookup(CStringView{ "\xc3\x89" }).status == ELookupStatus::not_found); // No Unicode folding.
        const char* invalid[]{ "", "/shaders/file0", "root:/shaders/file0", "shaders\\file0", "shaders//file0",
            "shaders/./file0", "shaders/../file0", "shaders/", "missing/../file0", "*", "?" };
        for (const char* path : invalid) { TEST_EXPECT(ctx, view.lookup(CStringView{ path }).status == ELookupStatus::invalid_path); }
        const char embedded[]{ 's', 'i', 'z', 'e', '\0', 'x' };
        TEST_EXPECT(ctx, view.lookup(CStringView{ embedded, sizeof(embedded) }).status == ELookupStatus::invalid_path);

        // Optional padding is deliberately nonzero, including directory padding.
        const auto bytes = fixture.block.buffer_bytes();
        std::memset(bytes.data() + fixture.block.bytes().size(), 0xa5, fixture.offsets.front() - fixture.block.bytes().size());
        for (std::size_t i = 0u; i < fixture.entries.size(); ++i)
        {
            const std::size_t begin = fixture.offsets[i] + fixture.sizes[i] + 1u;
            const std::size_t end = (i + 1u == fixture.entries.size()) ? bytes.size() : fixture.offsets[i + 1u];
            std::memset(bytes.data() + begin, 0xa5, end - begin);
        }
        TEST_EXPECT(ctx, fixture.check(view).succeeded());
        view.clear();
        TEST_EXPECT(ctx, !view.is_ready() && !view.document().is_ready() && view.bytes().size() == 0u);
        TEST_EXPECT(ctx, view.lookup(CStringView{ "size" }).status == ELookupStatus::not_ready);
    }
}

void test_layout_rejections(TTestContext& ctx)
{
    SFixture fixture;
    fixture.initialise(ctx);
    fixture.pack(ctx);
    CBundleView view;
    const auto bytes = fixture.block.buffer_bytes();
    TEST_EXPECT(ctx, fixture.check(view).succeeded());
    TEST_EXPECT(ctx, view.reset(nullptr, bytes.size()).status == EValidationStatus::invalid_buffer);
    TEST_EXPECT(ctx, !view.is_ready());
    TEST_EXPECT(ctx, view.reset(bytes.data() + 1u, bytes.size()).status == EValidationStatus::invalid_buffer);
    TEST_EXPECT(ctx, view.reset(bytes.data(), bytes.size() - 1u).status == EValidationStatus::invalid_buffer);
    TEST_EXPECT(ctx, view.reset(bytes.data(), memory::k_byte_size_ceiling + std::size_t{ 1u }).status == EValidationStatus::invalid_buffer);
    TEST_EXPECT(ctx, view.reset(bytes.data(), 0u).status == EValidationStatus::invalid_buffer);
    auto* header = reinterpret_cast<SBakedDocumentHeader*>(bytes.data());
    const auto original_header = *header;
    for (const std::uint32_t length : { 0u, 64u, static_cast<std::uint32_t>(bytes.size() + 1u), 0xffffffffu })
    {
        header->total_size = length;
        TEST_EXPECT(ctx, fixture.check(view).status == EValidationStatus::document_validation_failed);
    }
    *header = original_header;
    header->version = 0xffffu;
    TEST_EXPECT(ctx, fixture.check(view).status == EValidationStatus::document_validation_failed);
    *header = original_header;
    const auto first = fixture.entries.front();
    const auto root = fixture.block.document().root();
    for (const std::uint64_t size : { 0ull, 0x80000000ull, 0xffffffffffffffffull })
    {
        fixture.set(ctx, first, "size", size);
        TEST_EXPECT(ctx, fixture.check(view).status == EValidationStatus::invalid_extent);
    }
    fixture.set(ctx, first, "size", fixture.sizes.front());
    for (const std::uint64_t offset : { 0ull, 1ull, 0x80000000ull, 0xffffffffffffffffull })
    {
        fixture.set(ctx, first, "offset", offset);
        TEST_EXPECT(ctx, fixture.check(view).status == EValidationStatus::invalid_extent);
    }
    fixture.set(ctx, first, "offset", fixture.offsets.front());
    fixture.set(ctx, fixture.entries[1], "offset", fixture.offsets[0]); // Duplicate/overlap.
    TEST_EXPECT(ctx, fixture.check(view).status == EValidationStatus::invalid_extent);
    fixture.set(ctx, fixture.entries[1], "offset", fixture.offsets[1]);
    fixture.set(ctx, root, "version", 2u);
    TEST_EXPECT(ctx, fixture.check(view).status == EValidationStatus::unsupported_version);
    fixture.set(ctx, root, "version", 1u);
    fixture.set(ctx, root, "size", bytes.size() + 128u);
    TEST_EXPECT(ctx, fixture.check(view).status == EValidationStatus::invalid_extent);
    fixture.set(ctx, root, "size", bytes.size());
    for (std::size_t i = 0u; i < fixture.entries.size(); ++i)
    {
        bytes.data()[fixture.offsets[i] + fixture.sizes[i]] = 1u;
        const auto result = fixture.check(view);
        TEST_EXPECT(ctx, result.status == EValidationStatus::missing_terminator && result.value == fixture.entries[i]);
        bytes.data()[fixture.offsets[i] + fixture.sizes[i]] = 0u;
    }
    // Grow without files covering the extra reserved storage.
    const auto total = bytes.size() + 128u;
    TEST_EXPECT(ctx, fixture.block.resize_buffer(total, 128u));
    fixture.set(ctx, root, "size", total);
    TEST_EXPECT(ctx, fixture.check(view).status == EValidationStatus::invalid_extent);
    // Shift the first small file into the added tail: a gap remains at the start.
    fixture.set(ctx, first, "offset", total - 128u);
    TEST_EXPECT(ctx, fixture.check(view).status == EValidationStatus::invalid_extent);
}

void test_schema_rejections(TTestContext& ctx)
{
    for (unsigned scenario = 0u; scenario < 22u; ++scenario)
    {
        SFixture f;
        f.initialise(ctx);
        const auto first = f.files[0];
        auto property = [&](CNodeKey entry, const char* name) { return f.live.object_child(entry, CStringView{ name }); };
        switch (scenario)
        {
        case 0: tests::add_document_child(ctx, f.live, f.live.root(), f.live.create_boolean(true, CStringView{ "unknown" })); break;
        case 1: tests::add_document_child(ctx, f.live, first, f.live.create_boolean(true, CStringView{ "unknown" })); break;
        case 2: tests::add_document_child(ctx, f.live, f.directory, f.live.create_unsigned_integer(1u, CStringView{ "size" })); break;
        case 3: TEST_EXPECT(ctx, f.live.erase(property(f.live.root(), "fileType"))); break;
        case 4:
            TEST_EXPECT(ctx, f.live.erase(property(f.live.root(), "version")));
            tests::add_document_child(ctx, f.live, f.live.root(), f.live.create_signed_integer(1, CStringView{ "version" })); break;
        case 5:
            TEST_EXPECT(ctx, f.live.erase(property(first, "size")));
            tests::add_document_child(ctx, f.live, first, f.live.create_signed_integer(1, CStringView{ "size" })); break;
        case 6: TEST_EXPECT(ctx, f.live.erase_payload(f.content)); break;
        case 7: TEST_EXPECT(ctx, f.live.erase_payload(f.nested)); break;
        case 8: tests::add_document_child(ctx, f.live, first, f.live.create_object(CStringView{ "content" })); break;
        case 9: TEST_EXPECT(ctx, f.live.set_name(first, CStringView{ "UPPER" })); break;
        case 10: TEST_EXPECT(ctx, f.live.set_name(first, CStringView{ "a/b" })); break;
        case 11: TEST_EXPECT(ctx, f.live.set_name(first, CStringView{ ".." })); break;
        case 12:
            TEST_EXPECT(ctx, f.live.erase(property(first, "physicalName")));
            tests::add_document_child(ctx, f.live, first, f.live.create_string(CStringView{ "file0" }, CStringView{ "physicalName" })); break;
        case 13:
            TEST_EXPECT(ctx, f.live.erase(property(first, "physicalName")));
            tests::add_document_child(ctx, f.live, first, f.live.create_string(CStringView{ "Other" }, CStringView{ "physicalName" })); break;
        case 14:
            TEST_EXPECT(ctx, f.live.erase(property(first, "physicalName")));
            tests::add_document_child(ctx, f.live, first, f.live.create_unsigned_integer(1u, CStringView{ "physicalName" })); break;
        case 15: TEST_EXPECT(ctx, f.live.erase(property(first, "offset"))); break;
        case 16:
            TEST_EXPECT(ctx, f.live.erase(property(f.live.root(), "size")));
            tests::add_document_child(ctx, f.live, f.live.root(), f.live.create_floating_point(1.0, CStringView{ "size" })); break;
        case 17:
            TEST_EXPECT(ctx, f.live.erase(property(f.live.root(), "fileType")));
            tests::add_document_child(ctx, f.live, f.live.root(), f.live.create_string(CStringView{ "filesystem" }, CStringView{ "fileType" })); break;
        case 18: tests::add_document_child(ctx, f.live, f.content, f.live.create_boolean(true, CStringView{ "scalar" })); break;
        case 19: TEST_EXPECT(ctx, f.live.set_name(first, CStringView{ "a\0b", 3u })); break;
        case 20:
            TEST_EXPECT(ctx, f.live.erase(property(first, "offset")));
            tests::add_document_child(ctx, f.live, first, f.live.create_signed_integer(1, CStringView{ "offset" })); break;
        case 21:
            TEST_EXPECT(ctx, f.live.erase_payload(f.nested));
            TEST_EXPECT(ctx, f.live.erase(f.nested));
            tests::add_document_child(ctx, f.live, f.directory, f.live.create_array(CStringView{ "content" })); break;
        }
        f.pack(ctx);
        CBundleView view;
        const auto result = f.check(view);
        TEST_EXPECT(ctx, !result.succeeded() && result.value.is_valid() && !view.is_ready());
    }
}

void test_nested_and_duplicate_names(TTestContext& ctx)
{
    SFixture f;
    f.initialise(ctx);
    CNodeKey content = f.content;
    std::string path;
    for (unsigned i = 0u; i < 256u; ++i)
    {
        const auto directory = tests::add_document_child(ctx, f.live, content, f.live.create_object(CStringView{ "nested" }));
        content = tests::add_document_child(ctx, f.live, directory, f.live.create_object(CStringView{ "content" }));
        path += "NESTED/";
    }
    f.add_file(ctx, content, "last", 128u);
    path += "LAST";
    // Child order is also independent of validation, not just payload order.
    f.add_file(ctx, f.content, "zzz", 1u);
    f.add_file(ctx, f.content, "aaa", 1u);
    f.pack(ctx);
    CBundleView view;
    TEST_EXPECT(ctx, f.check(view).succeeded());
    const auto result = view.lookup(CStringView{ path.c_str() });
    TEST_EXPECT(ctx, result.status == ELookupStatus::success && result.bytes.bytes() == 128u);
    TEST_EXPECT(ctx, view.lookup(CStringView{ "aaa" }).status == ELookupStatus::success);

    // Duplicate object keys must be rejected by the baked-prefix validator.
    const auto doc = f.block.document();
    const auto root_content = tests::baked_document_child(doc, doc.root(), "content");
    const auto a = tests::baked_document_child(doc, root_content, "aaa");
    const auto z = tests::baked_document_child(doc, root_content, "zzz");
    const auto bytes = f.block.buffer_bytes();
    const auto* header = reinterpret_cast<const SBakedDocumentHeader*>(bytes.data());
    auto* records = reinterpret_cast<SBakedValueRecord*>(bytes.data() + header->values_offset);
    records[z.query_value()].property_name_index = records[a.query_value()].property_name_index;
    TEST_EXPECT(ctx, f.check(view).status == EValidationStatus::document_validation_failed);
    TEST_EXPECT(ctx, !view.is_ready());
}

struct SAllocationState { std::size_t calls{ 0u }; std::size_t fail_on{ SIZE_MAX }; };

void* MV_STD_ABI_CALL allocate(void* state, std::size_t alignment, std::size_t size) noexcept
{
    auto& fixture = *static_cast<SAllocationState*>(state);
    if (++fixture.calls == fixture.fail_on) { return nullptr; }
    return tests::allocate_test_memory(nullptr, alignment, size);
}

void test_adoption_and_failures(TTestContext& ctx)
{
    SFixture fixture;
    fixture.initialise(ctx);
    fixture.pack(ctx);
    bool succeeded = false;
    bool extent_failure = false;
    std::size_t failures = 0u;
    for (std::size_t fail_on = 1u; fail_on < 16u && !succeeded; ++fail_on)
    {
        CByteBuffer source;
        TEST_EXPECT(ctx, source.construct_and_copy_from(static_cast<const CBakedDocumentBlock&>(fixture.block).buffer_bytes()));
        CBakedDocumentBlock destination;
        CLiveDocument original;
        TEST_EXPECT(ctx, original.initialise() && document_translation::bake(original, destination));
        const void* destination_before = destination.bytes().data();
        const void* source_before = source.data();
        CBundleView view;
        TEST_EXPECT(ctx, fixture.check(view).succeeded());
        SAllocationState state{ 0u, fail_on };
        memory::CMemoryAllocator allocator{ &state, &allocate, &tests::deallocate_test_memory, system_ids::host };
        memory::CMemoryContext context{ allocator, system_ids::host };
        {
            const tests::TMemoryContextScope scope{ &context };
            const auto result = bundle::adopt(std::move(source), destination, view);
            succeeded = result.succeeded();
            extent_failure |= result.status == EValidationStatus::allocation_failed;
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
        if (!succeeded)
        {
            ++failures;
            TEST_EXPECT(ctx, source.data() == source_before && destination.bytes().data() == destination_before);
            TEST_EXPECT(ctx, view.bytes().data() == fixture.block.buffer_bytes().data());
        }
        else
        {
            TEST_EXPECT(ctx, !source.is_ready() && destination.buffer_bytes().data() == source_before);
            TEST_EXPECT(ctx, view.bytes().data() == source_before && view.bytes().size() == fixture.block.buffer_bytes().size());
            CBakedDocumentBlock moved{ std::move(destination) };
            TEST_EXPECT(ctx, view.lookup(CStringView{ "size" }).status == ELookupStatus::success);
            TEST_EXPECT(ctx, moved.buffer_bytes().data() == source_before);
        }
    }
    TEST_EXPECT(ctx, succeeded && extent_failure && failures >= 3u);

    CByteBuffer invalid;
    TEST_EXPECT(ctx, invalid.construct_and_copy_from(static_cast<const CBakedDocumentBlock&>(fixture.block).buffer_bytes()));
    invalid.data()[fixture.offsets.back() + fixture.sizes.back()] = 1u;
    CBakedDocumentBlock retained;
    CByteBuffer valid;
    TEST_EXPECT(ctx, valid.construct_and_copy_from(static_cast<const CBakedDocumentBlock&>(fixture.block).buffer_bytes()));
    CBundleView view;
    TEST_EXPECT(ctx, bundle::adopt(std::move(valid), retained, view).succeeded());
    const auto* retained_pointer = retained.buffer_bytes().data();
    const auto* invalid_pointer = invalid.data();
    TEST_EXPECT(ctx, bundle::adopt(std::move(invalid), retained, view).status == EValidationStatus::missing_terminator);
    TEST_EXPECT(ctx, invalid.data() == invalid_pointer && retained.buffer_bytes().data() == retained_pointer);
    TEST_EXPECT(ctx, view.bytes().data() == retained_pointer);
}

struct SReaders
{
    CBundleView view;
    std::atomic<bool> failed{ false };
    static std::uint32_t MV_STD_ABI_CALL read(void* data) noexcept
    {
        auto& state = *static_cast<SReaders*>(data);
        for (unsigned i = 0u; i < 2000u; ++i)
        {
            const auto result = state.view.lookup(CStringView{ "SHADERS/File0" });
            if ((result.status != ELookupStatus::success) || (result.bytes.bytes() != 1u) ||
                (state.view.lookup(CStringView{ "size" }).status != ELookupStatus::success))
            {
                state.failed.store(true, std::memory_order_relaxed);
            }
        }
        return 0u;
    }
};

void test_concurrent_lookup(TTestContext& ctx)
{
    SFixture fixture;
    fixture.initialise(ctx);
    fixture.pack(ctx);
    SReaders state;
    TEST_EXPECT(ctx, fixture.check(state.view).succeeded());
    std::array<platform::threading::CThread, 4u> workers;
    for (auto& worker : workers) { TEST_EXPECT(ctx, worker.create(&SReaders::read, &state)); }
    for (auto& worker : workers) { if (worker.is_valid()) { TEST_EXPECT(ctx, worker.join_and_close()); } }
    TEST_EXPECT(ctx, !state.failed.load(std::memory_order_relaxed));
    // A failing allocator does not affect queries or copying already checked views.
    tests::TAllocatorFixture failed{ true };
    memory::CMemoryAllocator allocator{ &failed, &tests::allocate_test_memory, &tests::deallocate_test_memory, system_ids::host };
    memory::CMemoryContext context{ allocator, system_ids::host };
    const tests::TMemoryContextScope scope{ &context };
    const CBundleView copy = state.view;
    TEST_EXPECT(ctx, copy.lookup(CStringView{ "SIZE" }).status == ELookupStatus::success);
}

}   // namespace

int run_bundle_tests()
{
    TTestContext ctx;
    test_views_and_lookup(ctx);
    test_layout_rejections(ctx);
    test_schema_rejections(ctx);
    test_nested_and_duplicate_names(ctx);
    test_adoption_and_failures(ctx);
    test_concurrent_lookup(ctx);
    return ctx.report("Bundle");
}
