//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SchemaDataBlock_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    14 Oct 26
//
//  Persistence envelopes assembled without a schema block writer.

#include "tests/environment/local_type_ids.hpp"

#include "tests/test_suites/SchemaDataBlock_test_cases.hpp"
#include "schema/data_block_format.hpp"
#include "schema/baked_instances_block.hpp"
#include "schema/baked_bulk_data_block.hpp"
#include "schema/live_instances.hpp"
#include "schema/live_bulk_data.hpp"
#include "host/runtime/host_payloads.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"

#include <cstring>
#include <limits>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
using namespace schema;
using namespace schema::detail;
using tests::TTestContext;

static_assert(std::is_trivially_copyable_v<SDataBlockDiagnostic>);
static_assert(std::is_trivially_copyable_v<SInstancesBlockDiagnostic>);
static_assert(std::is_trivially_copyable_v<SBulkDataBlockDiagnostic>);
static_assert(!std::is_copy_constructible_v<CBakedInstancesBlock> && !std::is_move_constructible_v<CBakedInstancesBlock>);
static_assert(!std::is_copy_constructible_v<CBakedBulkDataBlock> && !std::is_move_constructible_v<CBakedBulkDataBlock>);
static_assert(std::is_same_v<decltype(std::declval<CBakedInstancesBlock&>().document()), const CBakedDocument&>);
static_assert(std::is_same_v<decltype(std::declval<CBakedBulkDataBlock&>().document()), const CBakedDocument&>);

struct SFixture
{
    CBakedDocumentBlock prefix;
    CByteBuffer file;
    std::size_t payload_offset{};

    bool build(const std::string& json, const std::size_t payload_size = 0u)
    {
        CLiveDocument live;
        if (!document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(json.data()), json.size(), 1u }, live).accepted() ||
            !document_translation::bake(live, prefix)) return false;
        //  Independent assembly: no persistence layout/validation helper is used.
        const std::size_t document_size = prefix.bytes().size();
        payload_offset = (payload_size == 0u) ? document_size : ((document_size + 127u) / 128u) * 128u;
        if (!file.resize(payload_offset + payload_size, 128u)) return false;
        std::memset(file.data(), 0, file.size());
        std::memcpy(file.data(), prefix.bytes().data(), document_size);
        if (payload_size != 0u) std::memset(file.data() + payload_offset, 0xa5, payload_size);
        return true;
    }
};

static std::string envelope(const EDataBlockKind kind, const std::string& size = "0",
    const std::string& section = "{}", const std::string& extra = {})
{
    const bool instances = kind == EDataBlockKind::instances;
    return std::string{ "{\"fileType\":\"" } + (instances ? "morphic-instance-data" : "morphic-bulk-data") +
        "\",\"version\":1,\"payloadSize\":" + size + ",\"" + (instances ? "instances" : "data") + "\":" + section + extra + "}";
}

static void seed(SDataBlockDiagnostic& diagnostic)
{
    diagnostic.stage = EDataBlockStage::assembly;
    diagnostic.reason = EDataBlockReason::assembly_failed;
    diagnostic.location.kind = EDataBlockLocationKind::file_byte;
    diagnostic.location.byte_offset = 37u;
}

static void expect_empty(TTestContext& ctx, const SDataBlockView& view)
{
    TEST_EXPECT(ctx, !view.document.is_ready() && !view.section && !view.payload.is_ready() && !view.stripped);
}

static void expect_failure(TTestContext& ctx, const void* const bytes, const std::size_t size,
    const EDataBlockKind kind, const EDataBlockReason reason, const EDataBlockLocationKind location,
    const CBakedValueIndex value = {}, const std::uint64_t offset = 0u,
    const EDataBlockStage stage = EDataBlockStage::format_validation)
{
    SDataBlockView output;
    output.stripped = true;
    SDataBlockDiagnostic diagnostic;
    seed(diagnostic);
    TEST_EXPECT(ctx, !validate_data_block(bytes, size, kind, output, diagnostic));
    TEST_EXPECT(ctx, diagnostic.reason == reason && diagnostic.stage == stage);
    TEST_EXPECT(ctx, diagnostic.location.kind == location &&
        diagnostic.location.baked_value == value && diagnostic.location.byte_offset == offset &&
        !diagnostic.location.live_node.is_valid());
    expect_empty(ctx, output);
}

static void test_valid(TTestContext& ctx)
{
    TEST_EXPECT(ctx, SInstancesBlockDiagnostic{}.role.reason == EInstanceLoadReason::none &&
        SBulkDataBlockDiagnostic{}.role.reason == EBulkDataLoadReason::none);
    for (const auto kind : { EDataBlockKind::instances, EDataBlockKind::bulk_data })
    {
        for (const std::string& marker : { std::string{}, std::string{ ",\"stripped\":false" }, std::string{ ",\"stripped\":true" } })
        {
            SFixture fixture;
            if (!fixture.build(envelope(kind, "0", "{}", marker))) { TEST_EXPECT(ctx, false); continue; }
            SDataBlockView output;
            SDataBlockDiagnostic diagnostic;
            seed(diagnostic);
            TEST_EXPECT(ctx, validate_data_block(fixture.file.data(), fixture.file.size(), kind, output, diagnostic));
            TEST_EXPECT(ctx, diagnostic.stage == EDataBlockStage::none && diagnostic.reason == EDataBlockReason::none &&
                diagnostic.location.kind == EDataBlockLocationKind::none && diagnostic.location.byte_offset == 0u &&
                !diagnostic.location.baked_value && !diagnostic.location.live_node.is_valid());
            TEST_EXPECT(ctx, output.document.byte_count() == fixture.file.size() && output.section &&
                !output.payload.is_ready() && output.payload.data() == nullptr && output.payload.size() == 0u);
            TEST_EXPECT(ctx, output.stripped == (marker == ",\"stripped\":true"));
            TEST_EXPECT(ctx, !validate_data_block(nullptr, 0u, kind, output, diagnostic));
            expect_empty(ctx, output);
        }

        //  Root order is not canonical; nested structure is the role's concern.
        const bool instances = kind == EDataBlockKind::instances;
        const std::string reordered = std::string{ "{\"" } + (instances ? "instances" : "data") +
            "\":{\"opaque\":{}},\"payloadSize\":7,\"stripped\":false,\"version\":1,\"fileType\":\"" +
            (instances ? "morphic-instance-data" : "morphic-bulk-data") + "\"}";
        SFixture fixture;
        if (!fixture.build(reordered, 7u)) { TEST_EXPECT(ctx, false); continue; }
        SDataBlockView output;
        SDataBlockDiagnostic diagnostic;
        TEST_EXPECT(ctx, validate_data_block(fixture.file.data(), fixture.file.size(), kind, output, diagnostic));
        TEST_EXPECT(ctx, output.document.byte_count() == fixture.prefix.bytes().size() &&
            output.payload.data() == fixture.file.data() + fixture.payload_offset && output.payload.size() == 7u &&
            output.payload.align() == 128u && output.payload.data()[0] == 0xa5u && output.payload.data()[6] == 0xa5u);
        TEST_EXPECT(ctx, std::memcmp(fixture.file.data(), fixture.prefix.bytes().data(), fixture.prefix.bytes().size()) == 0);

        SFixture zero_type;
        TEST_EXPECT(ctx, zero_type.build(envelope(kind, "0", R"({"Zero":{"one":{"locator":{"offset":0,"count":1,"size":0,"valid":true}}}})")));
        TEST_EXPECT(ctx, validate_data_block(zero_type.file.data(), zero_type.file.size(), kind, output, diagnostic) && !output.payload.is_ready());
    }
}

static void test_metadata(TTestContext& ctx)
{
    struct SCase { const char* json; EDataBlockReason reason; const char* property; };
    const SCase cases[] = {
        { "[]", EDataBlockReason::invalid_property_type, "" },
        { "{}", EDataBlockReason::missing_property, "" },
        { R"({"version":1,"payloadSize":0,"instances":{}})", EDataBlockReason::missing_property, "" },
        { R"({"fileType":"morphic-instance-data","payloadSize":0,"instances":{}})", EDataBlockReason::missing_property, "" },
        { R"({"fileType":"morphic-instance-data","version":1,"instances":{}})", EDataBlockReason::missing_property, "" },
        { R"({"fileType":"morphic-instance-data","version":1,"payloadSize":0})", EDataBlockReason::missing_property, "" },
        { R"({"fileType":7,"version":1,"payloadSize":0,"instances":{}})", EDataBlockReason::invalid_property_type, "fileType" },
        { R"({"fileType":"morphic-bulk-data","version":1,"payloadSize":0,"instances":{}})", EDataBlockReason::wrong_content_type, "fileType" },
        { R"({"fileType":"","version":1,"payloadSize":0,"instances":{}})", EDataBlockReason::wrong_content_type, "fileType" },
        { R"({"fileType":"morphic-instance-data","version":+1,"payloadSize":0,"instances":{}})", EDataBlockReason::invalid_property_type, "version" },
        { R"({"fileType":"morphic-instance-data","version":1.0,"payloadSize":0,"instances":{}})", EDataBlockReason::invalid_property_type, "version" },
        { R"({"fileType":"morphic-instance-data","version":true,"payloadSize":0,"instances":{}})", EDataBlockReason::invalid_property_type, "version" },
        { R"({"fileType":"morphic-instance-data","version":0,"payloadSize":0,"instances":{}})", EDataBlockReason::unsupported_format_version, "version" },
        { R"({"fileType":"morphic-instance-data","version":2,"payloadSize":0,"instances":{}})", EDataBlockReason::unsupported_format_version, "version" },
        { R"({"fileType":"morphic-instance-data","version":1,"payloadSize":+0,"instances":{}})", EDataBlockReason::invalid_property_type, "payloadSize" },
        { R"({"fileType":"morphic-instance-data","version":1,"payloadSize":0.0,"instances":{}})", EDataBlockReason::invalid_property_type, "payloadSize" },
        { R"({"fileType":"morphic-instance-data","version":1,"payloadSize":"0","instances":{}})", EDataBlockReason::invalid_property_type, "payloadSize" },
        { R"({"fileType":"morphic-instance-data","version":1,"payloadSize":0,"instances":[]})", EDataBlockReason::invalid_property_type, "instances" },
        { R"({"fileType":"morphic-instance-data","version":1,"payloadSize":0,"instances":{},"stripped":0})", EDataBlockReason::invalid_property_type, "stripped" },
        { R"({"fileType":"morphic-instance-data","version":1,"payloadSize":0,"instances":{},"types":{}})", EDataBlockReason::unknown_property, "types" },
        { R"({"fileType":"morphic-instance-data","version":1,"payloadSize":0,"instances":{},"data":{}})", EDataBlockReason::unknown_property, "data" },
        { R"({"fileType":"morphic-instance-data","version":1,"payloadSize":0,"instances":{},"extension":null})", EDataBlockReason::unknown_property, "extension" },
        { R"({"fileType":"morphic-instance-data","version":1,"payloadSize":1,"instances":{}})", EDataBlockReason::invalid_input, "payloadSize" },
        { R"({"fileType":"morphic-instance-data","version":1,"payloadSize":18446744073709551615,"instances":{"x":{}}})", EDataBlockReason::size_overflow, "payloadSize" },
        { R"({"fileType":"morphic-instance-data","version":1,"payloadSize":2147483648,"instances":{"x":{}}})", EDataBlockReason::storage_limit, "payloadSize" }
    };
    for (const auto& item : cases)
    {
        for (const auto kind : { EDataBlockKind::instances, EDataBlockKind::bulk_data })
        {
            std::string json = item.json;
            std::string property = item.property;
            //  Swap both vocabulary and identifiers to exercise symmetric rejection.
            if (kind == EDataBlockKind::bulk_data)
            {
                const auto swap_text = [&json](const std::string& a, const std::string& b)
                {
                    std::size_t pos{};
                    while ((pos = json.find(a, pos)) != std::string::npos) { json.replace(pos, a.size(), b); pos += b.size(); }
                };
                swap_text("morphic-instance-data", "identifier-placeholder");
                swap_text("morphic-bulk-data", "morphic-instance-data");
                swap_text("identifier-placeholder", "morphic-bulk-data");
                swap_text("\"instances\"", "\"section-placeholder\"");
                swap_text("\"data\"", "\"instances\"");
                swap_text("\"section-placeholder\"", "\"data\"");
                if (property == "instances") property = "data";
                else if (property == "data") property = "instances";
            }
            SFixture fixture;
            if (!fixture.build(json)) { TEST_EXPECT(ctx, false); continue; }
            const CBakedDocument document = fixture.prefix.document();
            const CBakedValueIndex location = property.empty() ? document.root() : document.object_child(document.root(), CStringView{ property.c_str() });
            expect_failure(ctx, fixture.file.data(), fixture.file.size(), kind, item.reason,
                EDataBlockLocationKind::source_baked_value, location);
        }
    }
}

static void test_bounds(TTestContext& ctx)
{
    SFixture fixture;
    if (!fixture.build(envelope(EDataBlockKind::instances, "3", R"({"opaque":{}})"), 3u)) { TEST_EXPECT(ctx, false); return; }
    const auto kind = EDataBlockKind::instances;
    const auto payload_value = fixture.prefix.document().object_child(fixture.prefix.document().root(), CStringView{ "payloadSize" });
    expect_failure(ctx, nullptr, 0u, kind, EDataBlockReason::invalid_input, EDataBlockLocationKind::none, {}, 0u, EDataBlockStage::preconditions);
    expect_failure(ctx, fixture.file.data(), fixture.file.size(), static_cast<EDataBlockKind>(255u),
        EDataBlockReason::invalid_option, EDataBlockLocationKind::none, {}, 0u, EDataBlockStage::preconditions);
    expect_failure(ctx, fixture.file.data() + 1u, fixture.file.size() - 1u, kind, EDataBlockReason::invalid_alignment, EDataBlockLocationKind::none);
    for (std::size_t length = 0u; length < sizeof(SBakedDocumentHeader); ++length)
        expect_failure(ctx, fixture.file.data(), length, kind, EDataBlockReason::invalid_document, EDataBlockLocationKind::none);
    expect_failure(ctx, fixture.file.data(), fixture.prefix.bytes().size() - 1u, kind,
        EDataBlockReason::file_length_mismatch, EDataBlockLocationKind::file_byte, {}, offsetof(SBakedDocumentHeader, total_size));
    expect_failure(ctx, fixture.file.data(), fixture.file.size() - 1u, kind,
        EDataBlockReason::file_length_mismatch, EDataBlockLocationKind::source_baked_value, payload_value);
    TEST_EXPECT(ctx, fixture.file.resize(fixture.file.size() + 1u, 128u));
    expect_failure(ctx, fixture.file.data(), fixture.file.size(), kind,
        EDataBlockReason::file_length_mismatch, EDataBlockLocationKind::source_baked_value, payload_value);
    TEST_EXPECT(ctx, fixture.file.set_size(fixture.file.size() - 1u));

    SBakedDocumentHeader original;
    std::memcpy(&original, fixture.file.data(), sizeof(original));
    for (unsigned mutation = 0u; mutation < 7u; ++mutation)
    {
        SBakedDocumentHeader changed = original;
        if (mutation == 0u) changed.magic = 0u;
        else if (mutation == 1u) changed.version = 0u;
        else if (mutation == 2u) changed.header_size = 0u;
        else if (mutation < 6u) changed.reserved[mutation - 3u] = 1u;
        else changed.values_offset = UINT32_MAX;
        std::memcpy(fixture.file.data(), &changed, sizeof(changed));
        const auto location = (mutation < 2u) ? EDataBlockLocationKind::file_byte : EDataBlockLocationKind::none;
        expect_failure(ctx, fixture.file.data(), fixture.file.size(), kind,
            mutation == 1u ? EDataBlockReason::unsupported_document_version : EDataBlockReason::invalid_document,
            location, {}, mutation == 1u ? offsetof(SBakedDocumentHeader, version) : 0u);
    }
    std::memcpy(fixture.file.data(), &original, sizeof(original));

    //  Test every possible container-padding length without large allocations.
    bool seen[128]{};
    for (std::size_t length = 0u; length < 128u; ++length)
    {
        SFixture padded;
        if (!padded.build(envelope(kind, "1", "{\"x" + std::string(length, 'x') + "\":{}}"), 1u)) { TEST_EXPECT(ctx, false); continue; }
        const std::size_t padding = padded.payload_offset - padded.prefix.bytes().size();
        seen[padding] = true;
        SDataBlockView output;
        SDataBlockDiagnostic diagnostic;
        TEST_EXPECT(ctx, validate_data_block(padded.file.data(), padded.file.size(), kind, output, diagnostic));
        for (std::size_t index = padded.prefix.bytes().size(); index < padded.payload_offset; ++index)
        {
            padded.file.data()[index] = 1u;
            expect_failure(ctx, padded.file.data(), padded.file.size(), kind, EDataBlockReason::nonzero_padding,
                EDataBlockLocationKind::file_byte, {}, index);
            padded.file.data()[index] = 0u;
        }
    }
    for (const bool value : seen) TEST_EXPECT(ctx, value);

    SFixture empty;
    TEST_EXPECT(ctx, empty.build(envelope(kind)));
    TEST_EXPECT(ctx, empty.file.resize(empty.file.size() + 1u, 128u));
    expect_failure(ctx, empty.file.data(), empty.file.size(), kind, EDataBlockReason::file_length_mismatch,
        EDataBlockLocationKind::source_baked_value, empty.prefix.document().object_child(empty.prefix.document().root(), CStringView{ "payloadSize" }));

    tests::TAllocatorFixture failing{ true };
    memory::CMemoryAllocator allocator{ &failing, &tests::allocate_test_memory, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        expect_failure(ctx, fixture.file.data(), fixture.file.size(), kind, EDataBlockReason::invalid_document, EDataBlockLocationKind::none);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static void test_layout(TTestContext& ctx)
{
    struct SCase { std::uint64_t document, payload; EDataBlockReason reason; std::size_t offset, size; };
    const SCase cases[] = {
        { 0u, 0u, EDataBlockReason::invalid_document, 0u, 0u },
        { 114u, 0u, EDataBlockReason::none, 0u, 114u },
        { 128u, 1u, EDataBlockReason::none, 128u, 129u },
        { 129u, 1u, EDataBlockReason::none, 256u, 257u },
        { 0x80000000u, 0u, EDataBlockReason::none, 0u, 0x80000000u },
        { 0x7fffff80u, 128u, EDataBlockReason::none, 0x7fffff80u, 0x80000000u },
        { 0x7fffff80u, 129u, EDataBlockReason::storage_limit, 0u, 0u },
        { 0x7fffffffu, 1u, EDataBlockReason::storage_limit, 0u, 0u },
        { 0x80000001ull, 0u, EDataBlockReason::storage_limit, 0u, 0u },
        { 128u, UINT64_MAX, EDataBlockReason::size_overflow, 0u, 0u },
        { 128u, UINT64_MAX - 128u, EDataBlockReason::storage_limit, 0u, 0u }
    };
    for (const auto& item : cases)
    {
        SDataBlockLayout layout{ 7u, 9u };
        EDataBlockReason reason{ EDataBlockReason::invalid_input };
        TEST_EXPECT(ctx, data_block_layout(item.document, item.payload, layout, reason) == (item.reason == EDataBlockReason::none));
        TEST_EXPECT(ctx, reason == item.reason && layout.payload_offset == item.offset && layout.file_size == item.size);
    }
}

struct SSchemaFixture
{
    CLiveSchema live;
    CBakedDocumentBlock storage;
    CBakedSchema baked;

    bool build(const std::string& json = R"({"types":{"structures":{"Zero":{"members":[]}}}})")
    {
        CLiveDocument document;
        SDiagnostic diagnostic;
        return document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(json.data()), json.size(), 1u }, document).accepted() &&
            live.try_adopt(std::move(document)) && live.resolve(diagnostic) && live.bake(storage, baked) && baked.resolve(diagnostic);
    }
};

template<bool Instances>
struct SBlockTraits
{
    using Live = std::conditional_t<Instances, CLiveInstances, CLiveBulkData>;
    using Role = std::conditional_t<Instances, CBakedInstances, CBakedBulkData>;
    using RoleDiagnostic = std::conditional_t<Instances, SInstanceDiagnostic, SBulkDataDiagnostic>;
    using Block = std::conditional_t<Instances, CBakedInstancesBlock, CBakedBulkDataBlock>;
    using Diagnostic = std::conditional_t<Instances, SInstancesBlockDiagnostic, SBulkDataBlockDiagnostic>;
    using Reason = std::conditional_t<Instances, EInstanceLoadReason, EBulkDataLoadReason>;
    static constexpr EDataBlockKind kind = Instances ? EDataBlockKind::instances : EDataBlockKind::bulk_data;

    static auto query(const Block& block)
    {
        return std::conditional_t<Instances, CInstanceDocumentQuery, CBulkDataDocumentQuery>{ block.document() };
    }

    static bool stripped(const Block& block)
    {
        bool value{};
        const auto& document = block.document();
        (void)document.boolean_value(document.object_child(document.root(), CStringView{ "stripped" }), value);
        return value;
    }

    template<class TSchema>
    static bool attach(Role& role, const Block& block, TSchema& schema, RoleDiagnostic& diagnostic, const bool compare = true)
    {
        return role.set_document(block.document()) && role.attach_schema(schema, block.payload_view(), compare, diagnostic);
    }

    static std::string entry(const unsigned offset, const bool embedded = false)
    {
        const std::string locator = "\"locator\":{\"offset\":" + std::to_string(offset) + ",\"valid\":true" +
            (Instances ? "}" : ",\"count\":1,\"size\":1}");
        return "{" + locator + (embedded ? (Instances ? ",\"declaration\":165" : ",\"data\":[165]") : "") + "}";
    }

    static std::string section(const bool embedded = false)
    {
        //  Leading/inter-entry gaps and trailing storage remain opaque payload bytes.
        return "{\"u8\":{\"one\":" + entry(2u, embedded) + ",\"two\":" + entry(6u, embedded) + "}}";
    }

    template<class TSchema>
    static void check_entry(TTestContext& ctx, const Block& block, TSchema& schema, const std::uint8_t* const payload)
    {
        Role role;
        RoleDiagnostic diagnostic;
        TEST_EXPECT(ctx, attach(role, block, schema, diagnostic, false));
        if constexpr (Instances)
        {
            SInstanceEntryView entry;
            const auto handle = role.find_base(CStringView{ "u8" }, CStringView{ "one" });
            TEST_EXPECT(ctx, handle && role.entry(handle, entry));
            TEST_EXPECT(ctx, entry.offset == 2u && entry.byte_count == 1u && entry.bytes == payload + 2u);
        }
        else
        {
            SBulkDataEntryView entry;
            const auto handle = role.find_entry(CStringView{ "u8" }, CStringView{ "one" });
            TEST_EXPECT(ctx, handle && role.entry(handle, entry));
            TEST_EXPECT(ctx, entry.offset == 2u && entry.count == 1u && entry.byte_count == 1u && entry.bytes == payload + 2u);
        }
    }
};

template<bool Instances, class TSchema>
static void test_block_ownership(TTestContext& ctx, TSchema& schema)
{
    using T = SBlockTraits<Instances>;
    typename T::Diagnostic diagnostic;
    SFixture fixture;
    if (!fixture.build(envelope(T::kind, "9", T::section(true)), 9u)) { TEST_EXPECT(ctx, false); return; }
    const auto* const original = fixture.file.data();
    const std::size_t file_size = fixture.file.size();
    const std::string snapshot{ reinterpret_cast<const char*>(original), file_size };
    CBakedDocumentBlock source;
    TEST_EXPECT(ctx, source.adopt_buffer(std::move(fixture.file)));
    typename T::Block block;
    TEST_EXPECT(ctx, block.backing() == EEntityBacking::none && !block.loaded_ready() && !block.asset_id() &&
        block.file_bytes().data() == nullptr && !block.document().is_ready());
    CBakedDocumentBlock extracted;
    TEST_EXPECT(ctx, !block.release_local(extracted));
    seed(diagnostic.block);
    diagnostic.role.reason = T::Reason::unknown_type;
    TEST_EXPECT(ctx, block.adopt_local(std::move(source), diagnostic));
    TEST_EXPECT(ctx, diagnostic.block.reason == EDataBlockReason::none && diagnostic.role.reason == T::Reason::none &&
        diagnostic.block.location.kind == EDataBlockLocationKind::none);
    TEST_EXPECT(ctx, !source.is_ready() && source.buffer_capacity() == 0u);
    TEST_EXPECT(ctx, schema.reference_count() == 0u && block.loaded_ready() && block.backing() == EEntityBacking::local && !block.asset_id());
    TEST_EXPECT(ctx, block.file_bytes().data() == original && block.file_bytes().size() == file_size);
    T::check_entry(ctx, block, schema, (original + fixture.payload_offset));

    SFixture replacement;
    TEST_EXPECT(ctx, replacement.build(envelope(T::kind)));
    TEST_EXPECT(ctx, source.adopt_buffer(std::move(replacement.file)));
    const auto* const other = source.buffer_bytes().data();
    TEST_EXPECT(ctx, !block.adopt_local(std::move(source), diagnostic));
    TEST_EXPECT(ctx, diagnostic.block.stage == EDataBlockStage::preconditions &&
        diagnostic.block.reason == EDataBlockReason::destination_not_empty && diagnostic.role.reason == T::Reason::none);
    TEST_EXPECT(ctx, source.buffer_bytes().data() == other && block.file_bytes().data() == original && schema.reference_count() == 0u);
    TEST_EXPECT(ctx, !block.attach_asset({}, {}, diagnostic) &&
        diagnostic.block.reason == EDataBlockReason::destination_not_empty);
    TEST_EXPECT(ctx, !block.release_local(source) && source.buffer_bytes().data() == other && block.loaded_ready());
    TEST_EXPECT(ctx, block.release_local(extracted));
    TEST_EXPECT(ctx, schema.reference_count() == 0u && block.backing() == EEntityBacking::none &&
        !block.document().is_ready() && !block.file_bytes().is_ready());
    TEST_EXPECT(ctx, extracted.buffer_bytes().data() == original && extracted.buffer_bytes().size() == file_size &&
        std::memcmp(original, snapshot.data(), file_size) == 0);
    TEST_EXPECT(ctx, block.adopt_local(std::move(extracted), diagnostic));
    block.clear();
    block.clear();
    TEST_EXPECT(ctx, schema.reference_count() == 0u && !block.loaded_ready() && !block.asset_id());

    //  A repository supplies a genuine ID and owner; this is not Host integration.
    CAssetRepository assets;
    TEST_EXPECT(ctx, assets.initialise());
    CErasedOwner owner = CErasedOwner::create<BakedDocumentAsset>();
    auto* const stored = owner.payload<BakedDocumentAsset>();
    if (!stored) { TEST_EXPECT(ctx, false); return; }
    stored->block = std::move(source);
    const CByteConstView borrowed = static_cast<const CBakedDocumentBlock&>(stored->block).buffer_bytes();
    const CAssetId id = assets.insert(std::move(owner));
    TEST_EXPECT(ctx, id.is_valid());
    TEST_EXPECT(ctx, !block.attach_asset({}, borrowed, diagnostic) &&
        diagnostic.block.reason == EDataBlockReason::invalid_input && block.backing() == EEntityBacking::none);
    TEST_EXPECT(ctx, !block.attach_asset(id, {}, diagnostic) && assets.resolve(id) != nullptr &&
        schema.reference_count() == 0u);
    TEST_EXPECT(ctx, block.attach_asset(id, borrowed, diagnostic));
    TEST_EXPECT(ctx, block.backing() == EEntityBacking::asset && block.asset_id() == id && block.loaded_ready() &&
        block.file_bytes().data() == other && schema.reference_count() == 0u);
    TEST_EXPECT(ctx, !block.release_local(extracted) && !extracted.is_ready());
    block.clear();
    TEST_EXPECT(ctx, !block.asset_id() && !block.file_bytes().is_ready() && schema.reference_count() == 0u && assets.resolve(id) != nullptr);
    {
        typename T::Block temporary;
        TEST_EXPECT(ctx, temporary.attach_asset(id, borrowed, diagnostic));
    }
    TEST_EXPECT(ctx, schema.reference_count() == 0u && assets.resolve(id) != nullptr && stored->block.is_ready());
    TEST_EXPECT(ctx, assets.erase(id));
}

template<bool Instances, class TSchema>
static void test_block_failures(TTestContext& ctx, TSchema& schema)
{
    using T = SBlockTraits<Instances>;
    struct SCase { std::string section; typename T::Reason reason; bool compare; };
    const SCase cases[] = {
        { R"({"Unknown":{"one":{}}})", T::Reason::unknown_type, false },
        { "{\"u8\":{\"one\":" + T::entry(9u) + "}}", T::Reason::invalid_range, false },
        { "{\"u8\":{\"one\":" + T::entry(2u) + ",\"two\":" + T::entry(2u) + "}}", T::Reason::overlap, false },
        { T::section(true), T::Reason::embedded_mismatch, true }
    };
    for (const auto& item : cases)
    {
        SFixture fixture;
        if (!fixture.build(envelope(T::kind, "9", item.section), 9u)) { TEST_EXPECT(ctx, false); continue; }
        if (item.compare) fixture.file.data()[fixture.payload_offset + 2u] = 42u;
        const auto* const original = fixture.file.data();
        const std::string snapshot{ reinterpret_cast<const char*>(original), fixture.file.size() };
        CBakedDocumentBlock source;
        TEST_EXPECT(ctx, source.adopt_buffer(std::move(fixture.file)));
        typename T::Block block;
        typename T::Diagnostic diagnostic;
        TEST_EXPECT(ctx, block.adopt_local(std::move(source), diagnostic));
        typename T::Role role;
        typename T::RoleDiagnostic error;
        TEST_EXPECT(ctx, !T::attach(role, block, schema, error, item.compare));
        TEST_EXPECT(ctx, block.loaded_ready() && block.file_bytes().data() == original && !role.loaded_ready());
        TEST_EXPECT(ctx, schema.reference_count() == 0u && std::memcmp(original, snapshot.data(), snapshot.size()) == 0);
        TEST_EXPECT(ctx, error.reason == item.reason && T::query(block).contains(error.occurrence));
        if (item.compare)
        {
            TEST_EXPECT(ctx, role.attach_schema(schema, block.payload_view(), false, error));
            TEST_EXPECT(ctx, role.loaded_ready() && error.reason == T::Reason::none);
        }
    }

    for (const bool stripped : { false, true })
    {
        for (const bool zero_type : { false, true })
        {
            SFixture fixture;
            const std::string zero = Instances ?
                R"({"Zero":{"one":{"locator":{"offset":0,"valid":true}}}})" :
                R"({"Zero":{"one":{"locator":{"offset":0,"valid":true,"count":1,"size":0}}}})";
            TEST_EXPECT(ctx, fixture.build(envelope(T::kind, "0", zero_type ? zero : "{}", stripped ? ",\"stripped\":true" : "")));
            CBakedDocumentBlock source;
            TEST_EXPECT(ctx, source.adopt_buffer(std::move(fixture.file)));
            {
                typename T::Block block;
                typename T::Diagnostic diagnostic;
                TEST_EXPECT(ctx, block.adopt_local(std::move(source), diagnostic));
                TEST_EXPECT(ctx, block.loaded_ready() && T::stripped(block) == stripped &&
                    block.payload_view().size() == 0u && schema.reference_count() == 0u);
            }
            TEST_EXPECT(ctx, schema.reference_count() == 0u);
        }
    }

    //  Generic document storage can contain an invalid persistence envelope.
    SFixture malformed;
    TEST_EXPECT(ctx, malformed.build(envelope(T::kind, "0", "{}", ",\"extension\":true")));
    CBakedDocumentBlock source;
    TEST_EXPECT(ctx, source.adopt_buffer(std::move(malformed.file)));
    typename T::Block block;
    typename T::Diagnostic diagnostic;
    diagnostic.role.reason = T::Reason::unknown_type;
    TEST_EXPECT(ctx, !block.adopt_local(std::move(source), diagnostic));
    TEST_EXPECT(ctx, source.is_ready() && block.backing() == EEntityBacking::none &&
        diagnostic.block.stage == EDataBlockStage::format_validation &&
        diagnostic.block.reason == EDataBlockReason::unknown_property && diagnostic.role.reason == T::Reason::none);
}

template<bool Instances, bool Baked>
static void test_schema_lifetime(TTestContext& ctx)
{
    using T = SBlockTraits<Instances>;
    typename T::Block block;
    typename T::Diagnostic diagnostic;
    SFixture fixture;
    TEST_EXPECT(ctx, fixture.build(envelope(T::kind, "9", T::section()), 9u));
    const auto* const original = fixture.file.data();
    CBakedDocumentBlock source;
    TEST_EXPECT(ctx, source.adopt_buffer(std::move(fixture.file)));
    typename T::Role role;
    typename T::RoleDiagnostic error;
    {
        SSchemaFixture schemas;
        auto& schema = [&]() -> auto& { if constexpr (Baked) return schemas.baked; else return schemas.live; }();
        TEST_EXPECT(ctx, block.adopt_local(std::move(source), diagnostic));
        TEST_EXPECT(ctx, role.set_document(block.document()) && !role.attach_schema(schema, block.payload_view(), false, error));
        TEST_EXPECT(ctx, schemas.build());
        TEST_EXPECT(ctx, role.attach_schema(schema, block.payload_view(), false, error) && schema.reference_count() == 1u);
    }
    TEST_EXPECT(ctx, !role.loaded_ready() && block.loaded_ready() && block.backing() == EEntityBacking::local && block.file_bytes().data() == original);
    role.clear();
    SSchemaFixture replacement;
    TEST_EXPECT(ctx, replacement.build());
    TEST_EXPECT(ctx, !block.adopt_local(std::move(source), diagnostic) &&
        diagnostic.block.reason == EDataBlockReason::destination_not_empty && replacement.live.reference_count() == 0u);
    TEST_EXPECT(ctx, block.release_local(source) && source.buffer_bytes().data() == original);
    TEST_EXPECT(ctx, block.adopt_local(std::move(source), diagnostic));
    TEST_EXPECT(ctx, block.loaded_ready());
    TEST_EXPECT(ctx, block.release_local(source));

    CAssetRepository assets;
    TEST_EXPECT(ctx, assets.initialise());
    CErasedOwner owner = CErasedOwner::create<BakedDocumentAsset>();
    auto* const stored = owner.payload<BakedDocumentAsset>();
    if (!stored) { TEST_EXPECT(ctx, false); return; }
    stored->block = std::move(source);
    const CByteConstView bytes = static_cast<const CBakedDocumentBlock&>(stored->block).buffer_bytes();
    const CAssetId id = assets.insert(std::move(owner));
    {
        SSchemaFixture schemas;
        TEST_EXPECT(ctx, schemas.build());
        auto& schema = [&]() -> auto& { if constexpr (Baked) return schemas.baked; else return schemas.live; }();
        TEST_EXPECT(ctx, block.attach_asset(id, bytes, diagnostic));
        T::check_entry(ctx, block, schema, (original + fixture.payload_offset));
    }
    TEST_EXPECT(ctx, block.loaded_ready() && block.backing() == EEntityBacking::asset &&
        block.asset_id() == id && block.file_bytes().data() == original && block.file_bytes().size() == bytes.size());
    TEST_EXPECT(ctx, !block.release_local(source) && !source.is_ready());
    TEST_EXPECT(ctx, !block.attach_asset(id, bytes, diagnostic) &&
        diagnostic.block.reason == EDataBlockReason::destination_not_empty);
    block.clear();
    TEST_EXPECT(ctx, assets.resolve(id) != nullptr && assets.erase(id));
}

struct SReleaseObserver
{
    CBakedSchema* schema{};
    void* backing{};
    bool released{}, unbound_before_release{};
};

static bool MV_STD_ABI_CALL deallocate_observed_block(void* const state, const std::size_t alignment, void* const pointer) noexcept
{
    auto& observer = *static_cast<SReleaseObserver*>(state);
    if (pointer == observer.backing)
    {
        observer.released = true;
        observer.unbound_before_release = observer.schema->reference_count() == 0u;
    }
    return tests::deallocate_test_memory(nullptr, alignment, pointer);
}

static void* MV_STD_ABI_CALL allocate_observed_block(void*, const std::size_t alignment, const std::size_t bytes) noexcept
{
    return tests::allocate_test_memory(nullptr, alignment, bytes);
}

template<bool Instances>
static void test_block_destruction_order(TTestContext& ctx)
{
    using T = SBlockTraits<Instances>;
    SSchemaFixture schemas;
    TEST_EXPECT(ctx, schemas.build());
    SReleaseObserver observer;
    observer.schema = &schemas.baked;
    memory::CMemoryAllocator allocator{ &observer, &allocate_observed_block, &deallocate_observed_block };
    memory::CMemoryContext context{ allocator };
    {
        const tests::TMemoryContextScope scope{ &context };
        SFixture fixture;
        TEST_EXPECT(ctx, fixture.build(envelope(T::kind, "9", T::section()), 9u));
        observer.backing = fixture.file.data();
        CBakedDocumentBlock source;
        TEST_EXPECT(ctx, source.adopt_buffer(std::move(fixture.file)));
        {
            typename T::Block block;
            typename T::Diagnostic diagnostic;
            TEST_EXPECT(ctx, block.adopt_local(std::move(source), diagnostic));
            TEST_EXPECT(ctx, schemas.baked.reference_count() == 0u && !observer.released);
        }
        TEST_EXPECT(ctx, observer.released && observer.unbound_before_release);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

template<bool Instances>
static void test_block_allocation_failures(TTestContext& ctx)
{
    using T = SBlockTraits<Instances>;
    SSchemaFixture schemas;
    TEST_EXPECT(ctx, schemas.build());
    SFixture fixture;
    TEST_EXPECT(ctx, fixture.build(envelope(T::kind, "9", T::section(true)), 9u));
    CBakedDocumentBlock source;
    TEST_EXPECT(ctx, source.adopt_buffer(std::move(fixture.file)));
    const auto* const original = source.buffer_bytes().data();
    bool completed{};
    //  Fail each preparation allocation in turn, including opt-in comparison.
    for (std::size_t fail_on = 0u; fail_on != 128u && !completed; ++fail_on)
    {
        tests::TAllocationFailureFixture failures{ 0u, fail_on };
        memory::CMemoryAllocator allocator{ &failures, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            const tests::TMemoryContextScope scope{ &context };
            typename T::Block block;
            typename T::Diagnostic diagnostic;
            completed = block.adopt_local(std::move(source), diagnostic);
            if (completed)
            {
                TEST_EXPECT(ctx, block.release_local(source));
            }
            else
            {
                TEST_EXPECT(ctx, block.backing() == EEntityBacking::none && !block.document().is_ready());
                TEST_EXPECT(ctx, diagnostic.block.reason == EDataBlockReason::invalid_document ||
                    (diagnostic.block.reason == EDataBlockReason::specialised_failure && diagnostic.role.reason == T::Reason::allocation_failed));
            }
            TEST_EXPECT(ctx, source.buffer_bytes().data() == original && schemas.baked.reference_count() == 0u);
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, completed);
}

static void test_blocks(TTestContext& ctx)
{
    SSchemaFixture schemas;
    if (!schemas.build()) { TEST_EXPECT(ctx, false); return; }
    test_block_ownership<true>(ctx, schemas.live);
    test_block_ownership<false>(ctx, schemas.live);
    test_block_ownership<true>(ctx, schemas.baked);
    test_block_ownership<false>(ctx, schemas.baked);
    test_block_failures<true>(ctx, schemas.live);
    test_block_failures<false>(ctx, schemas.live);
    test_block_failures<true>(ctx, schemas.baked);
    test_block_failures<false>(ctx, schemas.baked);
    test_schema_lifetime<true, true>(ctx);
    test_schema_lifetime<true, false>(ctx);
    test_schema_lifetime<false, true>(ctx);
    test_schema_lifetime<false, false>(ctx);
    test_block_allocation_failures<true>(ctx);
    test_block_allocation_failures<false>(ctx);
    test_block_destruction_order<true>(ctx);
    test_block_destruction_order<false>(ctx);
}

template<bool Instances, class TSchema>
static bool populate_bake_source(TTestContext& ctx, typename SBlockTraits<Instances>::Live& source, TSchema& schema, const unsigned content)
{
    using T = SBlockTraits<Instances>;
    typename T::RoleDiagnostic diagnostic;
    if (!source.initialise(schema)) { TEST_EXPECT(ctx, false); return false; }
    if (content == 0u) return true;
    if constexpr (Instances)
    {
        TEST_EXPECT(ctx, source.capture_base(CStringView{ "Zero" }, CStringView{ "zero" }, {}, diagnostic));
    }
    else
    {
        TEST_EXPECT(ctx, source.capture(CStringView{ "Zero" }, CStringView{ "zero" }, {}, 3u, diagnostic));
    }
    if (content == 1u) return true;
    alignas(128) std::uint8_t values[128] = { 7u, 8u };
    alignas(128) std::uint32_t wide[32] = { 12345u, 67890u };
    if constexpr (Instances)
    {
        const auto base = source.capture_base(CStringView{ "u8" }, CStringView{ "one" }, CByteConstView{ values, 1u, 128u }, diagnostic);
        const auto child = source.create_specialisation(base, CStringView{ "child" }, {}, {}, diagnostic);
        TEST_EXPECT(ctx, base && child);
        SMutableInstanceEntryView entry;
        if (!source.mutable_entry(base, entry)) { TEST_EXPECT(ctx, false); return false; }
        entry.bytes.data()[0] = 9u;
        if (!source.mutable_entry(child, entry)) { TEST_EXPECT(ctx, false); return false; }
        entry.bytes.data()[0] = 11u;
        TEST_EXPECT(ctx, source.capture_base(CStringView{ "u32" }, CStringView{ "wide" },
            CByteConstView{ reinterpret_cast<const std::uint8_t*>(wide), sizeof(std::uint32_t), 128u }, diagnostic));
    }
    else
    {
        const auto handle = source.capture(CStringView{ "u8" }, CStringView{ "one" }, CByteConstView{ values, 2u, 128u }, 2u, diagnostic);
        SMutableBulkDataEntryView entry;
        if (!handle || !source.mutable_entry(handle, entry)) { TEST_EXPECT(ctx, false); return false; }
        entry.bytes.data()[0] = 9u;
        TEST_EXPECT(ctx, source.capture(CStringView{ "u32" }, CStringView{ "wide" },
            CByteConstView{ reinterpret_cast<const std::uint8_t*>(wide), 2u * sizeof(std::uint32_t), 128u }, 2u, diagnostic));
    }
    return true;
}

static std::string byte_snapshot(const CByteConstView bytes)
{
    return bytes.size() ? std::string{ reinterpret_cast<const char*>(bytes.data()), bytes.size() } : std::string{};
}

static bool same_bytes(const CByteConstView bytes, const std::string& snapshot)
{
    return bytes.size() == snapshot.size() && (snapshot.empty() || std::memcmp(bytes.data(), snapshot.data(), snapshot.size()) == 0);
}

template<bool Instances, class TSourceSchema, class TDestinationSchema>
static void test_completed_bake(TTestContext& ctx, TSourceSchema& source_schema, TDestinationSchema& destination_schema)
{
    using T = SBlockTraits<Instances>;
    for (const auto form : { EDataOutputForm::embedded, EDataOutputForm::external, EDataOutputForm::stripped })
    {
        for (unsigned content = 0u; content != 3u; ++content)
        {
            typename T::Live source;
            if (!populate_bake_source<Instances>(ctx, source, source_schema, content)) continue;
            const auto original_query = source.document_query();
            const auto original_root = original_query.root();
            const auto original_bytes = source.payload_view();
            const std::string snapshot = byte_snapshot(original_bytes);
            CBakedDocumentBlock baseline_document;
            CByteBuffer baseline_payload;
            typename T::Role baseline;
            typename T::RoleDiagnostic baseline_diagnostic;
            TEST_EXPECT(ctx, source.bake(baseline_document, baseline_payload, baseline, destination_schema, form, baseline_diagnostic));
            typename T::Block result;
            typename T::Diagnostic diagnostic;
            seed(diagnostic.block);
            diagnostic.role.reason = T::Reason::unknown_type;
            const bool baked = source.bake(result, destination_schema, form, diagnostic);
            TEST_EXPECT(ctx, baked);
            if (!baked)
            {
                std::cerr << "Completed bake: instances=" << Instances << " form=" << static_cast<unsigned>(form)
                    << " content=" << content << " stage=" << static_cast<unsigned>(diagnostic.block.stage)
                    << " reason=" << static_cast<unsigned>(diagnostic.block.reason) << '\n';
                continue;
            }
            TEST_EXPECT(ctx, result.loaded_ready() && result.backing() == EEntityBacking::local && !result.asset_id() &&
                diagnostic.block.reason == EDataBlockReason::none && diagnostic.role.reason == T::Reason::none);
            typename T::Role result_role;
            typename T::RoleDiagnostic result_error;
            TEST_EXPECT(ctx, T::attach(result_role, result, destination_schema, result_error));
            const auto bytes = result.file_bytes();
            TEST_EXPECT(ctx, (reinterpret_cast<std::uintptr_t>(bytes.data()) & 127u) == 0u);
            SDataBlockView view;
            SDataBlockDiagnostic format_diagnostic;
            TEST_EXPECT(ctx, validate_data_block(bytes.data(), bytes.size(), T::kind, view, format_diagnostic));
            TEST_EXPECT(ctx, view.stripped == (form == EDataOutputForm::stripped));
            //  Newly packed gaps are zero; complete records retain their source bytes.
            TEST_EXPECT(ctx, same_bytes(view.payload, byte_snapshot(baseline_payload.const_view())));
            const std::string completed_snapshot = byte_snapshot(view.payload);
            const auto compare_entry = [&](const auto actual_handle, const auto baseline_handle)
            {
                std::conditional_t<Instances, SInstanceEntryView, SBulkDataEntryView> actual_entry, baseline_entry;
                const bool found = result_role.entry(actual_handle, actual_entry) && baseline.entry(baseline_handle, baseline_entry);
                TEST_EXPECT(ctx, found);
                if (!found) return;
                TEST_EXPECT(ctx, actual_entry.offset == baseline_entry.offset && actual_entry.byte_count == baseline_entry.byte_count &&
                    (actual_entry.byte_count == 0u || std::memcmp(actual_entry.bytes, baseline_entry.bytes, static_cast<std::size_t>(actual_entry.byte_count)) == 0));
                if constexpr (!Instances) TEST_EXPECT(ctx, actual_entry.count == baseline_entry.count && actual_entry.stride == baseline_entry.stride);
            };
            const char* types[]{ "Zero", "u8", "u32" };
            const char* names[]{ "zero", "one", "wide" };
            for (unsigned entry = 0u; entry < (content == 2u ? 3u : content); ++entry)
            {
                if constexpr (Instances)
                    compare_entry(result_role.find_base(CStringView{ types[entry] }, CStringView{ names[entry] }),
                        baseline.find_base(CStringView{ types[entry] }, CStringView{ names[entry] }));
                else
                    compare_entry(result_role.find_entry(CStringView{ types[entry] }, CStringView{ names[entry] }),
                        baseline.find_entry(CStringView{ types[entry] }, CStringView{ names[entry] }));
            }
            TEST_EXPECT(ctx, result.payload_view().data() == view.payload.data());
            if (content < 2u)
            {
                TEST_EXPECT(ctx, bytes.size() == view.document.byte_count() && view.payload.size() == 0u && view.payload.data() == nullptr);
            }
            else
            {
                const std::size_t offset = ((view.document.byte_count() + 127u) / 128u) * 128u;
                TEST_EXPECT(ctx, view.payload.data() == bytes.data() + offset && bytes.size() == offset + view.payload.size());
                for (std::size_t index = view.document.byte_count(); index != offset; ++index) TEST_EXPECT(ctx, bytes.data()[index] == 0u);
                const auto query = T::query(result);
                if constexpr (Instances)
                {
                    const auto base = result_role.find_base(CStringView{ "u8" }, CStringView{ "one" });
                    const auto child = result_role.find_specialisation(base, CStringView{ "child" });
                    compare_entry(child, baseline.find_specialisation(baseline.find_base(CStringView{ "u8" }, CStringView{ "one" }), CStringView{ "child" }));
                    SInstanceEntryView base_entry, child_entry;
                    TEST_EXPECT(ctx, result_role.entry(base, base_entry) && result_role.entry(child, child_entry));
                    TEST_EXPECT(ctx, base_entry.bytes[0] == 9u && child_entry.bytes[0] == 11u && child_entry.parent == base);
                    TEST_EXPECT(ctx, static_cast<bool>(query.object_child(base, CStringView{ "declaration" })) == (form != EDataOutputForm::stripped));
                    TEST_EXPECT(ctx, static_cast<bool>(query.object_child(child, CStringView{ "declaration" })) == (form != EDataOutputForm::stripped));
                }
                else
                {
                    const auto handle = result_role.find_entry(CStringView{ "u8" }, CStringView{ "one" });
                    SBulkDataEntryView entry;
                    TEST_EXPECT(ctx, result_role.entry(handle, entry));
                    TEST_EXPECT(ctx, entry.count == 2u && entry.bytes[0] == 9u && entry.bytes[1] == 8u);
                    TEST_EXPECT(ctx, static_cast<bool>(query.object_child(handle, CStringView{ "data" })) == (form == EDataOutputForm::embedded));
                    const auto zero = result_role.find_entry(CStringView{ "Zero" }, CStringView{ "zero" });
                    TEST_EXPECT(ctx, result_role.entry(zero, entry) && entry.count == 3u && entry.byte_count == 0u && entry.bytes == nullptr);
                }
            }
            TEST_EXPECT(ctx, source.loaded_ready() && source.document_query().root() == original_root &&
                source.payload_view().data() == original_bytes.data() && same_bytes(source.payload_view(), snapshot));
            TEST_EXPECT(ctx, !source.bake(result, destination_schema, form, diagnostic) &&
                diagnostic.block.reason == EDataBlockReason::destination_not_empty && result.file_bytes().data() == bytes.data());
            source.clear();
            TEST_EXPECT(ctx, result.loaded_ready() && same_bytes(result.payload_view(), completed_snapshot));
            //  Reattach a separately allocated copy, then release the original.
            CByteBuffer copied;
            CBakedDocumentBlock owner;
            TEST_EXPECT(ctx, copied.construct_and_copy_from(bytes) && owner.adopt_buffer(std::move(copied)));
            typename T::Block independent;
            TEST_EXPECT(ctx, independent.adopt_local(std::move(owner), diagnostic));
            CBakedDocumentBlock original_owner;
            result_role.clear();
            TEST_EXPECT(ctx, result.release_local(original_owner) && original_owner.buffer_capacity() == bytes.size());
            original_owner.deallocate();
            TEST_EXPECT(ctx, independent.loaded_ready() && same_bytes(independent.payload_view(), completed_snapshot));
            TEST_EXPECT(ctx, independent.release_local(owner) && owner.buffer_capacity() == bytes.size());
        }
    }
}

template<bool Instances>
static void test_bake_failures(TTestContext& ctx)
{
    using T = SBlockTraits<Instances>;
    SSchemaFixture schemas;
    TEST_EXPECT(ctx, schemas.build());
    typename T::Live source;
    typename T::Block destination;
    typename T::Diagnostic diagnostic;
    TEST_EXPECT(ctx, !source.bake(destination, schemas.baked, EDataOutputForm::embedded, diagnostic) &&
        diagnostic.block.stage == EDataBlockStage::preconditions && diagnostic.block.reason == EDataBlockReason::invalid_input);
    if (!populate_bake_source<Instances>(ctx, source, schemas.live, 2u)) return;
    const auto snapshot = byte_snapshot(source.payload_view());
    TEST_EXPECT(ctx, !source.bake(destination, schemas.baked, static_cast<EDataOutputForm>(255u), diagnostic) &&
        diagnostic.block.reason == EDataBlockReason::invalid_option);
    CBakedSchema unresolved;
    TEST_EXPECT(ctx, !source.bake(destination, unresolved, EDataOutputForm::embedded, diagnostic) &&
        diagnostic.block.stage == EDataBlockStage::schema_binding && diagnostic.block.reason == EDataBlockReason::schema_unavailable);

    //  A schema without the source's Zero type fails during preparation.
    CLiveDocument empty_types;
    const std::string text = R"({"types":{}})";
    TEST_EXPECT(ctx, document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), 1u }, empty_types).accepted());
    CLiveSchema incompatible;
    SDiagnostic schema_diagnostic;
    TEST_EXPECT(ctx, incompatible.try_adopt(std::move(empty_types)) && incompatible.resolve(schema_diagnostic));
    TEST_EXPECT(ctx, !source.bake(destination, incompatible, EDataOutputForm::embedded, diagnostic) &&
        diagnostic.block.stage == EDataBlockStage::description_preparation &&
        diagnostic.block.reason == EDataBlockReason::specialised_failure && diagnostic.role.reason == T::Reason::incompatible_schema);
    TEST_EXPECT(ctx, diagnostic.block.location.kind == EDataBlockLocationKind::source_live_node &&
        source.document_query().contains(diagnostic.role.occurrence));
    TEST_EXPECT(ctx, destination.backing() == EEntityBacking::none && incompatible.reference_count() == 0u &&
        same_bytes(source.payload_view(), snapshot));

    //  Raw noncanonical boolean bytes cannot be represented as embedded values.
    typename T::Live noncanonical;
    TEST_EXPECT(ctx, noncanonical.initialise(schemas.live));
    alignas(128) std::uint8_t boolean[128] = {};
    typename T::RoleDiagnostic role_diagnostic;
    if constexpr (Instances)
    {
        const auto handle = noncanonical.capture_base(CStringView{ "b8" }, CStringView{ "bad" },
            CByteConstView{ boolean, 1u, 128u }, role_diagnostic);
        SMutableInstanceEntryView entry;
        if (!handle || !noncanonical.mutable_entry(handle, entry)) { TEST_EXPECT(ctx, false); return; }
        entry.bytes.data()[0] = 2u;
    }
    else
    {
        boolean[0] = 2u;
        TEST_EXPECT(ctx, noncanonical.capture(CStringView{ "b8" }, CStringView{ "bad" },
            CByteConstView{ boolean, 1u, 128u }, 1u, role_diagnostic));
    }
    TEST_EXPECT(ctx, !noncanonical.bake(destination, schemas.baked, EDataOutputForm::embedded, diagnostic));
    TEST_EXPECT(ctx, diagnostic.block.stage == EDataBlockStage::description_preparation &&
        diagnostic.role.reason == T::Reason::unrepresentable_value &&
        diagnostic.block.location.kind == EDataBlockLocationKind::source_live_node &&
        noncanonical.document_query().contains(diagnostic.role.occurrence));
    const bool external = noncanonical.bake(destination, schemas.baked, EDataOutputForm::external, diagnostic);
    TEST_EXPECT(ctx, external == !Instances);
    destination.clear();
    TEST_EXPECT(ctx, noncanonical.bake(destination, schemas.baked, EDataOutputForm::stripped, diagnostic));
    TEST_EXPECT(ctx, destination.loaded_ready() && destination.payload_view().data()[0] == 2u);
}

template<bool Instances>
static void test_bake_allocation_failures(TTestContext& ctx)
{
    using T = SBlockTraits<Instances>;
    SSchemaFixture schemas;
    TEST_EXPECT(ctx, schemas.build());
    typename T::Live source;
    if (!populate_bake_source<Instances>(ctx, source, schemas.live, 2u)) return;
    const auto snapshot = byte_snapshot(source.payload_view());
    bool completed{}, preparation{}, document_bake{}, assembly{}, attachment{};
    for (std::size_t fail_on = 0u; fail_on != 1024u && !completed; ++fail_on)
    {
        tests::TAllocationFailureFixture failures{ 0u, fail_on };
        memory::CMemoryAllocator allocator{ &failures, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            const tests::TMemoryContextScope scope{ &context };
            typename T::Block output;
            typename T::Diagnostic diagnostic;
            completed = source.bake(output, schemas.baked, EDataOutputForm::embedded, diagnostic);
            if (!completed)
            {
                TEST_EXPECT(ctx, output.backing() == EEntityBacking::none && !output.document().is_ready() &&
                    schemas.baked.reference_count() == 0u && diagnostic.block.reason != EDataBlockReason::none);
                const auto stage = diagnostic.block.stage;
                preparation |= stage == EDataBlockStage::description_preparation;
                document_bake |= stage == EDataBlockStage::document_bake;
                assembly |= stage == EDataBlockStage::assembly;
                attachment |= stage == EDataBlockStage::format_validation || stage == EDataBlockStage::specialised_loading;
                if (stage == EDataBlockStage::description_preparation)
                {
                    TEST_EXPECT(ctx, !diagnostic.role.occurrence || source.document_query().contains(diagnostic.role.occurrence));
                    TEST_EXPECT(ctx, diagnostic.block.location.kind == EDataBlockLocationKind::none ||
                        diagnostic.block.location.kind == EDataBlockLocationKind::source_live_node);
                }
                else
                {
                    TEST_EXPECT(ctx, !diagnostic.role.occurrence && diagnostic.block.location.kind == EDataBlockLocationKind::none);
                }
                if (stage == EDataBlockStage::document_bake)
                    TEST_EXPECT(ctx, diagnostic.block.reason == EDataBlockReason::document_bake_failed);
                if (stage == EDataBlockStage::assembly)
                    TEST_EXPECT(ctx, diagnostic.block.reason == EDataBlockReason::allocation_failed || diagnostic.block.reason == EDataBlockReason::assembly_failed);
            }
            TEST_EXPECT(ctx, source.loaded_ready() && schemas.live.reference_count() == 1u && same_bytes(source.payload_view(), snapshot));
        }
        TEST_EXPECT(ctx, context.is_attribution_empty() && schemas.baked.reference_count() == 0u);
    }
    TEST_EXPECT(ctx, completed && preparation && document_bake && assembly && attachment);
}

static void* MV_STD_ABI_CALL allocate_poisoned_payload(void*, const std::size_t alignment, const std::size_t bytes) noexcept
{
    void* const result = tests::allocate_test_memory(nullptr, alignment, bytes);
    if (result) std::memset(result, 0xa5, bytes);
    return result;
}

template<bool Instances>
static void test_new_payload_gaps(TTestContext& ctx)
{
    using T = SBlockTraits<Instances>;
    memory::CMemoryAllocator allocator{ nullptr, &allocate_poisoned_payload, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        const tests::TMemoryContextScope scope{ &context };
        SSchemaFixture schemas;
        TEST_EXPECT(ctx, schemas.build(R"({"types":{"structures":{
            "Zero":{"members":[]},"Padded":{"members":[{"a":{"type":"u8"}},{"b":{"type":"u32"}}]}}}})"));
        typename T::Live source;
        typename T::RoleDiagnostic role_diagnostic;
        TEST_EXPECT(ctx, source.initialise(schemas.baked));
        alignas(128) const std::uint8_t small[]{ 1u };
        alignas(128) const std::uint8_t padded[]{ 7u, 0xa5u, 0xa5u, 0xa5u, 42u, 0u, 0u, 0u };
        if constexpr (Instances)
        {
            TEST_EXPECT(ctx, source.capture_base(CStringView{ "u8" }, CStringView{ "first" }, CByteConstView{ small, sizeof(small), 128u }, role_diagnostic));
            TEST_EXPECT(ctx, source.capture_base(CStringView{ "Zero" }, CStringView{ "empty" }, {}, role_diagnostic));
            TEST_EXPECT(ctx, source.capture_base(CStringView{ "Padded" }, CStringView{ "last" }, CByteConstView{ padded, sizeof(padded), 128u }, role_diagnostic));
        }
        else
        {
            TEST_EXPECT(ctx, source.capture(CStringView{ "u8" }, CStringView{ "first" }, CByteConstView{ small, sizeof(small), 128u }, 1u, role_diagnostic));
            TEST_EXPECT(ctx, source.capture(CStringView{ "Zero" }, CStringView{ "empty" }, {}, 1u, role_diagnostic));
            TEST_EXPECT(ctx, source.capture(CStringView{ "Padded" }, CStringView{ "last" }, CByteConstView{ padded, sizeof(padded), 128u }, 1u, role_diagnostic));
        }
        const auto original = byte_snapshot(source.payload_view());
        CLiveDocument description;
        const std::string text = Instances ? R"({"instances":{
            "u8":{"first":{"locator":{"offset":2,"valid":true},"declaration":1}},
            "Zero":{"empty":{"locator":{"offset":0,"valid":true},"declaration":{}}},
            "Padded":{"last":{"locator":{"offset":8,"valid":true},"declaration":{"a":7,"b":42}}}}})" : R"({"data":{
            "u8":{"first":{"locator":{"offset":2,"valid":true},"data":[1]}},
            "Zero":{"empty":{"locator":{"offset":0,"valid":true},"data":[{}]}},
            "Padded":{"last":{"locator":{"offset":8,"valid":true},"data":[{"a":7,"b":42}]}}}})";
        TEST_EXPECT(ctx, document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() }, description).accepted());
        for (const auto form : { EDataOutputForm::embedded, EDataOutputForm::external, EDataOutputForm::stripped })
        {
            typename T::Block output, repeated, materialised;
            typename T::Diagnostic diagnostic;
            TEST_EXPECT(ctx, source.bake(output, schemas.baked, form, diagnostic));
            const auto bytes = output.payload_view();
            TEST_EXPECT(ctx, bytes.size() == 12u && bytes.data()[0] == 1u && std::memcmp(bytes.data() + 4u, padded, sizeof(padded)) == 0);
            for (std::size_t i = 1u; i != 4u; ++i) TEST_EXPECT(ctx, bytes.data()[i] == 0u);
            TEST_EXPECT(ctx, source.bake(repeated, schemas.baked, form, diagnostic) && same_bytes(repeated.file_bytes(), byte_snapshot(output.file_bytes())));
            TEST_EXPECT(ctx, same_bytes(source.payload_view(), original));
            TEST_EXPECT(ctx, materialised.materialise(description, schemas.baked, form, diagnostic));
            const auto constructed = materialised.payload_view();
            TEST_EXPECT(ctx, constructed.size() == 16u && constructed.data()[2] == 1u &&
                std::memcmp(constructed.data() + 8u, padded, sizeof(padded)) == 0);
            for (std::size_t i = 0u; i != 8u; ++i)
                if (i != 2u) TEST_EXPECT(ctx, constructed.data()[i] == 0u);
        }
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static void test_completed_bakes(TTestContext& ctx)
{
    SSchemaFixture source, destination;
    if (!source.build() || !destination.build()) { TEST_EXPECT(ctx, false); return; }
    test_completed_bake<true>(ctx, source.live, destination.live);
    test_completed_bake<true>(ctx, source.live, destination.baked);
    test_completed_bake<true>(ctx, source.baked, destination.live);
    test_completed_bake<true>(ctx, source.baked, destination.baked);
    test_completed_bake<false>(ctx, source.live, destination.live);
    test_completed_bake<false>(ctx, source.live, destination.baked);
    test_completed_bake<false>(ctx, source.baked, destination.live);
    test_completed_bake<false>(ctx, source.baked, destination.baked);
    test_bake_failures<true>(ctx);
    test_bake_failures<false>(ctx);
    test_bake_allocation_failures<true>(ctx);
    test_bake_allocation_failures<false>(ctx);
    test_new_payload_gaps<true>(ctx);
    test_new_payload_gaps<false>(ctx);
}
static const std::string materialisation_types = R"({"types":{"structures":{
    "Zero":{"members":[]},
    "Pair":{"members":[{"a":{"type":"u8","default":5}},{"b":{"type":"u8","default":9}}]}}}})";

template<bool Instances>
static std::string materialisation_description(const unsigned content)
{
    if (content == 0u) return "{}";
    if (content == 1u) return Instances ? R"({"instances":{}})" : R"({"data":{}})";
    if (content == 2u)
    {
        return Instances ? R"({"instances":{"Zero":{"zero":{"locator":{"offset":0,"valid":false}}}}})" :
            R"({"data":{"Zero":{"zero":{"locator":{"offset":0,"valid":false},"data":[{},{}]}}}})";
    }
    //  Construction selects only its role and replaces stale persistence metadata.
    return Instances ? R"({"fileType":"old","version":0,"payloadSize":999,"types":{},
        "data":{"unrelated":true},"instances":{"Pair":{
        "base":{"locator":{"offset":8,"valid":true},"declaration":{"a":3},
            "specialisation":{"child":{"locator":{"offset":4294967295,"valid":false},"declaration":{"b":7},
                "specialisation":{"deep":{"locator":{"offset":0,"valid":false}}}}}},
        "defaults":{"locator":{"offset":0,"valid":false}}}}})" :
        R"({"fileType":"old","version":0,"payloadSize":999,"types":{},
        "instances":{"unrelated":true},"data":{"Pair":{
        "base":{"locator":{"offset":8,"valid":true},"data":[{"a":3,"b":9},{"a":5,"b":7}]},
        "next":{"locator":{"offset":4294967295,"valid":false,"count":1,"size":2},"data":[[1,2]]}}}})";
}

template<bool Instances, bool Baked, class TSchema>
static void test_materialised_blocks(TTestContext& ctx, TSchema& schema)
{
    using T = SBlockTraits<Instances>;
    for (const auto form : { EDataOutputForm::embedded, EDataOutputForm::external, EDataOutputForm::stripped })
    {
        for (unsigned content = 0u; content != 4u; ++content)
        {
            CLiveDocument live;
            CBakedDocumentBlock baked;
            if (!tests::parse_document(materialisation_description<Instances>(content), live) || !document_translation::bake(live, baked))
            { TEST_EXPECT(ctx, false); continue; }
            const auto source_bytes = byte_snapshot(baked.bytes());
            const auto source_root = live.root();
            const auto run = [&](const auto& source)
            {
                typename T::Block output;
                typename T::Diagnostic diagnostic;
                seed(diagnostic.block);
                diagnostic.role.reason = T::Reason::unknown_type;
                const bool ok = output.materialise(source, schema, form, diagnostic);
                TEST_EXPECT(ctx, ok);
                if (!ok)
                {
                    std::cerr << "Materialisation: instances=" << Instances << " baked=" << Baked << " content=" << content
                        << " form=" << static_cast<unsigned>(form) << " stage=" << static_cast<unsigned>(diagnostic.block.stage)
                        << " reason=" << static_cast<unsigned>(diagnostic.block.reason) << " role=" << static_cast<unsigned>(diagnostic.role.reason) << '\n';
                    return;
                }
                TEST_EXPECT(ctx, output.loaded_ready() && output.backing() == EEntityBacking::local && !output.asset_id() &&
                    diagnostic.block.stage == EDataBlockStage::none && diagnostic.block.reason == EDataBlockReason::none &&
                    diagnostic.block.location.kind == EDataBlockLocationKind::none && diagnostic.role.reason == T::Reason::none);
                typename T::Role output_role;
                typename T::RoleDiagnostic output_error;
                TEST_EXPECT(ctx, T::attach(output_role, output, schema, output_error));
                SDataBlockView view;
                SDataBlockDiagnostic format;
                TEST_EXPECT(ctx, validate_data_block(output.file_bytes().data(), output.file_bytes().size(), T::kind, view, format));
                TEST_EXPECT(ctx, view.stripped == (form == EDataOutputForm::stripped) &&
                    !view.document.object_child(view.document.root(), CStringView{ "types" }) &&
                    !view.document.object_child(view.document.root(), CStringView{ Instances ? "data" : "instances" }));
                if (content < 3u) TEST_EXPECT(ctx, view.payload.size() == 0u && output.file_bytes().size() == view.document.byte_count());
                if (content == 2u)
                {
                    if constexpr (Instances)
                    {
                        SInstanceEntryView entry;
                        TEST_EXPECT(ctx, output_role.entry(output_role.find_base(CStringView{ "Zero" }, CStringView{ "zero" }), entry) &&
                            entry.byte_count == 0u && entry.bytes == nullptr);
                    }
                    else
                    {
                        SBulkDataEntryView entry;
                        TEST_EXPECT(ctx, output_role.entry(output_role.find_entry(CStringView{ "Zero" }, CStringView{ "zero" }), entry) &&
                            entry.count == 2u && entry.byte_count == 0u && entry.bytes == nullptr);
                    }
                }
                if (content == 3u)
                {
                    const auto& role = output_role;
                    const auto query = role.document_query();
                    if constexpr (Instances)
                    {
                        const auto base = role.find_base(CStringView{ "Pair" }, CStringView{ "base" });
                        const auto child = role.find_specialisation(base, CStringView{ "child" });
                        const auto deep = role.find_specialisation(child, CStringView{ "deep" });
                        const auto defaults = role.find_base(CStringView{ "Pair" }, CStringView{ "defaults" });
                        SInstanceEntryView entry;
                        TEST_EXPECT(ctx, role.entry(base, entry) && entry.offset == 8u && entry.byte_count == 2u && entry.bytes[0] == 3u && entry.bytes[1] == 9u);
                        TEST_EXPECT(ctx, role.entry(child, entry) && entry.offset == 10u && entry.bytes[0] == 3u && entry.bytes[1] == 7u);
                        TEST_EXPECT(ctx, role.entry(deep, entry) && entry.offset == 12u && entry.bytes[0] == 3u && entry.bytes[1] == 7u);
                        TEST_EXPECT(ctx, role.entry(defaults, entry) && entry.offset == 14u && entry.bytes[0] == 5u && entry.bytes[1] == 9u);
                        for (const auto handle : { base, child })
                            TEST_EXPECT(ctx, static_cast<bool>(query.object_child(handle, CStringView{ "declaration" })) == (form != EDataOutputForm::stripped));
                    }
                    else
                    {
                        const auto base = role.find_entry(CStringView{ "Pair" }, CStringView{ "base" });
                        const auto next = role.find_entry(CStringView{ "Pair" }, CStringView{ "next" });
                        SBulkDataEntryView entry;
                        TEST_EXPECT(ctx, role.entry(base, entry) && entry.offset == 8u && entry.count == 2u && entry.byte_count == 4u &&
                            entry.bytes[0] == 3u && entry.bytes[1] == 9u && entry.bytes[2] == 5u && entry.bytes[3] == 7u);
                        TEST_EXPECT(ctx, role.entry(next, entry) && entry.offset == 12u && entry.count == 1u && entry.bytes[0] == 1u && entry.bytes[1] == 2u);
                        for (const auto handle : { base, next })
                            TEST_EXPECT(ctx, static_cast<bool>(query.object_child(handle, CStringView{ "data" })) == (form == EDataOutputForm::embedded));
                    }
                }
                CBakedDocumentBlock live_check;
                TEST_EXPECT(ctx, live.root() == source_root && document_translation::bake(live, live_check) && same_bytes(live_check.bytes(), source_bytes));
                TEST_EXPECT(ctx, same_bytes(baked.bytes(), source_bytes));
                const auto output_address = output.file_bytes().data();
                TEST_EXPECT(ctx, !output.materialise(source, schema, form, diagnostic) &&
                    diagnostic.block.reason == EDataBlockReason::destination_not_empty && output.file_bytes().data() == output_address);
                const auto payload_snapshot = byte_snapshot(output.payload_view());
                live.deallocate();
                baked.deallocate();
                TEST_EXPECT(ctx, output.loaded_ready() && same_bytes(output.payload_view(), payload_snapshot));
                CBakedDocumentBlock owner;
                output_role.clear();
                TEST_EXPECT(ctx, output.release_local(owner));
                TEST_EXPECT(ctx, owner.buffer_capacity() == owner.buffer_bytes().size());
                typename T::Block reattached;
                TEST_EXPECT(ctx, reattached.adopt_local(std::move(owner), diagnostic) &&
                    same_bytes(reattached.payload_view(), payload_snapshot));
            };
            if constexpr (Baked) run(baked.document());
            else run(live);
        }
    }
}

template<bool Instances, bool Baked, class TSchema>
static void test_materialisation_failures(TTestContext& ctx, TSchema& schema)
{
    using T = SBlockTraits<Instances>;
    typename T::Block output;
    typename T::Diagnostic diagnostic;
    const std::string section = Instances ? "instances" : "data";
    struct SCase { std::string text; typename T::Reason reason; };
    const SCase cases[] = {
        { "[]", T::Reason::invalid_input },
        { "{\"" + section + "\":[]}", T::Reason::invalid_input },
        { "{\"stripped\":true,\"" + section + "\":{}}", T::Reason::binary_required },
        { "{\"" + section + "\":{\"Unknown\":{}}}", T::Reason::unknown_type },
        { "{\"" + section + "\":{\"Pair\":{\"bad\":{}}}}", T::Reason::missing_property },
        { "{\"" + section + "\":{\"Pair\":{\"bad\":{\"locator\":{\"offset\":0,\"valid\":false,\"count\":0}" +
            (Instances ? "" : ",\"data\":[[1,2]]") + "}}}}", T::Reason::invalid_count }
    };
    for (const auto& item : cases)
    {
        CLiveDocument live;
        CBakedDocumentBlock baked;
        TEST_EXPECT(ctx, tests::parse_document(item.text, live) && document_translation::bake(live, baked));
        const auto snapshot = byte_snapshot(baked.bytes());
        const auto run = [&](const auto& source)
        {
            TEST_EXPECT(ctx, !output.materialise(source, schema, EDataOutputForm::stripped, diagnostic));
            TEST_EXPECT(ctx, diagnostic.block.stage == EDataBlockStage::materialisation &&
                diagnostic.block.reason == EDataBlockReason::specialised_failure && diagnostic.role.reason == item.reason);
            TEST_EXPECT(ctx, output.backing() == EEntityBacking::none && !output.document().is_ready());
            if constexpr (Baked)
                TEST_EXPECT(ctx, diagnostic.block.location.kind == EDataBlockLocationKind::source_baked_value &&
                    source.contains(diagnostic.block.location.baked_value));
            else
                TEST_EXPECT(ctx, diagnostic.block.location.kind == EDataBlockLocationKind::source_live_node &&
                    source.contains(diagnostic.block.location.live_node));
            using Access = std::conditional_t<Instances, SInstanceHandleAccess, SBulkDataHandleAccess>;
            const auto occurrence = Access::occurrence(diagnostic.role.occurrence);
            if constexpr (Baked) TEST_EXPECT(ctx, occurrence.baked == diagnostic.block.location.baked_value);
            else TEST_EXPECT(ctx, occurrence.live == diagnostic.block.location.live_node);
        };
        if constexpr (Baked) run(baked.document());
        else run(live);
        CBakedDocumentBlock live_check;
        TEST_EXPECT(ctx, document_translation::bake(live, live_check) && same_bytes(live_check.bytes(), snapshot) && same_bytes(baked.bytes(), snapshot));
    }
    //  Construction still rejects invalid values when the requested output drops them.
    const std::string invalid = Instances ? R"({"instances":{"Pair":{"bad":{
        "locator":{"offset":0,"valid":false},"declaration":{"unknown":1}}}}})" :
        R"({"data":{"Pair":{"bad":{"locator":{"offset":0,"valid":false},"data":[{"a":1}]}}}})";
    CLiveDocument live;
    CBakedDocumentBlock baked;
    TEST_EXPECT(ctx, tests::parse_document(invalid, live) && document_translation::bake(live, baked));
    const auto run = [&](const auto& source)
    {
        TEST_EXPECT(ctx, !output.materialise(source, schema, EDataOutputForm::stripped, diagnostic));
        if constexpr (Instances) TEST_EXPECT(ctx, diagnostic.role.reason == EInstanceLoadReason::invalid_declaration);
        else TEST_EXPECT(ctx, diagnostic.role.reason == EBulkDataLoadReason::incomplete_record);
        TEST_EXPECT(ctx, diagnostic.block.stage == EDataBlockStage::materialisation);
        TEST_EXPECT(ctx, !output.materialise(source, schema, static_cast<EDataOutputForm>(255u), diagnostic) &&
            diagnostic.block.stage == EDataBlockStage::preconditions && diagnostic.block.reason == EDataBlockReason::invalid_option);
        CBakedSchema unavailable;
        TEST_EXPECT(ctx, !output.materialise(source, unavailable, EDataOutputForm::embedded, diagnostic) &&
            diagnostic.block.stage == EDataBlockStage::schema_binding && diagnostic.block.reason == EDataBlockReason::schema_unavailable);
    };
    if constexpr (Baked)
    {
        run(baked.document());
        TEST_EXPECT(ctx, !output.materialise(CBakedDocument{}, schema, EDataOutputForm::embedded, diagnostic));
    }
    else
    {
        run(live);
        TEST_EXPECT(ctx, !output.materialise(CLiveDocument{}, schema, EDataOutputForm::embedded, diagnostic));
    }
    TEST_EXPECT(ctx, diagnostic.block.stage == EDataBlockStage::preconditions && diagnostic.block.reason == EDataBlockReason::invalid_input);
}

template<bool Instances, bool Baked>
static void test_materialisation_allocations(TTestContext& ctx)
{
    using T = SBlockTraits<Instances>;
    SSchemaFixture schemas;
    CLiveDocument live;
    CBakedDocumentBlock baked;
    TEST_EXPECT(ctx, schemas.build(materialisation_types) && tests::parse_document(materialisation_description<Instances>(3u), live) &&
        document_translation::bake(live, baked));
    const auto snapshot = byte_snapshot(baked.bytes());
    bool completed{}, materialisation{}, promotion{}, assembly{}, attachment{};
    for (std::size_t fail_on = 0u; fail_on != 2048u && !completed; ++fail_on)
    {
        tests::TAllocationFailureFixture failures{ 0u, fail_on };
        memory::CMemoryAllocator allocator{ &failures, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            const tests::TMemoryContextScope scope{ &context };
            typename T::Block output;
            typename T::Diagnostic diagnostic;
            if constexpr (Baked) completed = output.materialise(baked.document(), schemas.baked, EDataOutputForm::stripped, diagnostic);
            else completed = output.materialise(live, schemas.baked, EDataOutputForm::stripped, diagnostic);
            if (!completed)
            {
                TEST_EXPECT(ctx, output.backing() == EEntityBacking::none && !output.document().is_ready() &&
                    schemas.baked.reference_count() == 0u && diagnostic.block.reason != EDataBlockReason::none);
                const auto stage = diagnostic.block.stage;
                materialisation |= stage == EDataBlockStage::materialisation;
                promotion |= stage == EDataBlockStage::document_promotion;
                assembly |= stage == EDataBlockStage::assembly;
                attachment |= stage == EDataBlockStage::format_validation || stage == EDataBlockStage::specialised_loading;
                if (diagnostic.block.location.kind != EDataBlockLocationKind::none)
                {
                    TEST_EXPECT(ctx, stage == EDataBlockStage::materialisation);
                    if constexpr (Baked) TEST_EXPECT(ctx, baked.document().contains(diagnostic.block.location.baked_value));
                    else TEST_EXPECT(ctx, live.contains(diagnostic.block.location.live_node));
                }
                else TEST_EXPECT(ctx, !diagnostic.role.occurrence);
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty() && schemas.baked.reference_count() == 0u && same_bytes(baked.bytes(), snapshot));
    }
    TEST_EXPECT(ctx, completed && materialisation && promotion && assembly && attachment);
    CBakedDocumentBlock live_check;
    TEST_EXPECT(ctx, document_translation::bake(live, live_check) && same_bytes(live_check.bytes(), snapshot));
}

static void test_materialisation(TTestContext& ctx)
{
    SSchemaFixture schemas;
    if (!schemas.build(materialisation_types)) { TEST_EXPECT(ctx, false); return; }
    test_materialised_blocks<true, true>(ctx, schemas.live);
    test_materialised_blocks<true, false>(ctx, schemas.live);
    test_materialised_blocks<false, true>(ctx, schemas.live);
    test_materialised_blocks<false, false>(ctx, schemas.live);
    test_materialised_blocks<true, true>(ctx, schemas.baked);
    test_materialised_blocks<true, false>(ctx, schemas.baked);
    test_materialised_blocks<false, true>(ctx, schemas.baked);
    test_materialised_blocks<false, false>(ctx, schemas.baked);
    test_materialisation_failures<true, true>(ctx, schemas.live);
    test_materialisation_failures<true, false>(ctx, schemas.live);
    test_materialisation_failures<false, true>(ctx, schemas.live);
    test_materialisation_failures<false, false>(ctx, schemas.live);
    test_materialisation_failures<true, true>(ctx, schemas.baked);
    test_materialisation_failures<true, false>(ctx, schemas.baked);
    test_materialisation_failures<false, true>(ctx, schemas.baked);
    test_materialisation_failures<false, false>(ctx, schemas.baked);
    test_materialisation_allocations<true, true>(ctx);
    test_materialisation_allocations<true, false>(ctx);
    test_materialisation_allocations<false, true>(ctx);
    test_materialisation_allocations<false, false>(ctx);
}
static const std::string stripping_types = R"({"types":{
    "structures":{"Zero":{"members":[]}},
    "bit_structures":{"Flags":{"storage":"u8","members":[{"flag":{"type":"b8","mask":1}}]}}}})";

template<bool Instances>
static bool build_strip_fixture(SFixture& fixture, const unsigned content)
{
    using T = SBlockTraits<Instances>;
    if (content == 0u) return fixture.build(envelope(T::kind));
    const std::string zero = Instances ? R"({"zero":{"locator":{"offset":0,"valid":true,"size":0}}})" :
        R"({"zero":{"locator":{"offset":0,"valid":true},"data":[{},{},{}]}})";
    if (content == 1u) return fixture.build(envelope(T::kind, "0", "{\"Zero\":" + zero + "}"));
    const bool retained = content == 2u;
    std::string section;
    if constexpr (Instances)
    {
        section = "{\"Zero\":" + zero + ",\"b8\":{\"one\":{\"locator\":{\"offset\":2,\"valid\":true,\"size\":1}" +
            std::string{ retained ? ",\"declaration\":false" : "" } +
            ",\"specialisation\":{\"child\":{\"locator\":{\"offset\":6,\"valid\":true,\"size\":1}" +
            std::string{ retained ? ",\"declaration\":true" : "" } + "}}}},\"Flags\":{\"bits\":{\"locator\":{\"offset\":9,\"valid\":true,\"size\":1}" +
            std::string{ retained ? ",\"declaration\":{\"flag\":false}" : "" } + "}}}";
    }
    else
    {
        section = "{\"Zero\":" + (retained ? zero : R"({"zero":{"locator":{"offset":0,"valid":true,"count":3,"size":0}}})") +
            ",\"b8\":{\"one\":{\"locator\":{\"offset\":2,\"valid\":true" +
            std::string{ retained ? "},\"data\":[false,true]" : ",\"size\":2}" } +
            "},\"two\":{\"locator\":{\"offset\":6,\"valid\":true,\"size\":2}}}," +
            "\"Flags\":{\"bits\":{\"locator\":{\"offset\":9,\"valid\":true,\"count\":1}" +
            std::string{ retained ? ",\"data\":[{\"flag\":false}]" : "" } + "}}}";
    }
    if (!fixture.build(envelope(T::kind, "17", section, content == 4u ? ",\"stripped\":true" : ""), 17u)) return false;
    //  Noncanonical booleans, unused flag bits and unreferenced bytes are opaque.
    auto* const payload = fixture.file.data() + fixture.payload_offset;
    for (unsigned index = 0u; index != 17u; ++index) payload[index] = static_cast<std::uint8_t>(0xa0u + index);
    payload[2] = 2u;
    payload[3] = 3u;
    payload[6] = 4u;
    payload[7] = 5u;
    payload[9] = 0xfdu;
    return true;
}

template<bool Instances, class TSourceSchema, class TDestinationSchema>
static void test_stripped_blocks(TTestContext& ctx, TSourceSchema& source_schema, TDestinationSchema& destination_schema)
{
    using T = SBlockTraits<Instances>;
    for (const bool borrowed : { false, true })
    {
        for (unsigned content = 0u; content != 5u; ++content)
        {
            SFixture fixture;
            if (!build_strip_fixture<Instances>(fixture, content)) { TEST_EXPECT(ctx, false); continue; }
            const auto original = byte_snapshot(fixture.file.const_view());
            CBakedDocumentBlock owner;
            TEST_EXPECT(ctx, owner.adopt_buffer(std::move(fixture.file)));
            CAssetRepository assets;
            CAssetId id;
            typename T::Block source, destination;
            typename T::Diagnostic diagnostic;
            if (borrowed)
            {
                TEST_EXPECT(ctx, assets.initialise());
                CErasedOwner erased = CErasedOwner::create<BakedDocumentAsset>();
                auto* const stored = erased.payload<BakedDocumentAsset>();
                if (!stored) { TEST_EXPECT(ctx, false); continue; }
                stored->block = std::move(owner);
                const auto bytes = static_cast<const CBakedDocumentBlock&>(stored->block).buffer_bytes();
                id = assets.insert(std::move(erased));
                TEST_EXPECT(ctx, source.attach_asset(id, bytes, diagnostic));
            }
            else TEST_EXPECT(ctx, source.adopt_local(std::move(owner), diagnostic));
            if (!source.loaded_ready()) { TEST_EXPECT(ctx, false); continue; }
            const auto source_address = source.file_bytes().data();
            const auto source_root = T::query(source).root();
            const auto payload = byte_snapshot(source.payload_view());
            seed(diagnostic.block);
            diagnostic.role.reason = T::Reason::unknown_type;
            TEST_EXPECT(ctx, source.strip_to(destination, destination_schema, diagnostic));
            if (!destination.loaded_ready()) { TEST_EXPECT(ctx, false); continue; }
            TEST_EXPECT(ctx, diagnostic.block.stage == EDataBlockStage::none && diagnostic.block.reason == EDataBlockReason::none &&
                diagnostic.block.location.kind == EDataBlockLocationKind::none && diagnostic.role.reason == T::Reason::none);
            TEST_EXPECT(ctx, destination.backing() == EEntityBacking::local && !destination.asset_id() &&
                destination.file_bytes().data() != source_address && same_bytes(destination.payload_view(), payload));
            SDataBlockView view;
            SDataBlockDiagnostic format;
            TEST_EXPECT(ctx, validate_data_block(destination.file_bytes().data(), destination.file_bytes().size(), T::kind, view, format) && view.stripped);
            if (content == 2u) TEST_EXPECT(ctx, view.document.byte_count() != fixture.prefix.document().byte_count());
            if (content < 2u) TEST_EXPECT(ctx, destination.file_bytes().size() == view.document.byte_count() && view.payload.size() == 0u);
            typename T::Role role;
            typename T::RoleDiagnostic role_error;
            TEST_EXPECT(ctx, T::attach(role, destination, destination_schema, role_error));
            const auto query = role.document_query();
            if (content >= 2u)
            {
                if constexpr (Instances)
                {
                    const auto base = role.find_base(CStringView{ "b8" }, CStringView{ "one" });
                    const auto child = role.find_specialisation(base, CStringView{ "child" });
                    const auto bits = role.find_base(CStringView{ "Flags" }, CStringView{ "bits" });
                    SInstanceEntryView entry;
                    TEST_EXPECT(ctx, role.entry(base, entry) && entry.offset == 2u && entry.bytes[0] == 2u);
                    TEST_EXPECT(ctx, role.entry(child, entry) && entry.offset == 6u && entry.bytes[0] == 4u && entry.parent == base);
                    TEST_EXPECT(ctx, role.entry(bits, entry) && entry.offset == 9u && entry.bytes[0] == 0xfdu);
                    for (const auto handle : { base, child, bits }) TEST_EXPECT(ctx, !query.object_child(handle, CStringView{ "declaration" }));
                }
                else
                {
                    SBulkDataEntryView entry;
                    for (const auto name : { "one", "two" })
                    {
                        const auto handle = role.find_entry(CStringView{ "b8" }, CStringView{ name });
                        TEST_EXPECT(ctx, role.entry(handle, entry) && entry.count == 2u && entry.byte_count == 2u);
                        TEST_EXPECT(ctx, entry.offset == (name[0] == 'o' ? 2u : 6u) && !query.object_child(handle, CStringView{ "data" }));
                        std::uint64_t count{};
                        TEST_EXPECT(ctx, query.unsigned_integer_value(query.object_child(query.object_child(handle, CStringView{ "locator" }), CStringView{ "count" }), count) && count == 2u);
                    }
                    const auto bits = role.find_entry(CStringView{ "Flags" }, CStringView{ "bits" });
                    TEST_EXPECT(ctx, role.entry(bits, entry) && entry.offset == 9u && entry.count == 1u && entry.bytes[0] == 0xfdu &&
                        !query.object_child(bits, CStringView{ "data" }));
                }
            }
            if (content != 0u)
            {
                if constexpr (Instances)
                {
                    SInstanceEntryView entry;
                    TEST_EXPECT(ctx, role.entry(role.find_base(CStringView{ "Zero" }, CStringView{ "zero" }), entry) && entry.byte_count == 0u);
                }
                else
                {
                    SBulkDataEntryView entry;
                    const auto zero = role.find_entry(CStringView{ "Zero" }, CStringView{ "zero" });
                    TEST_EXPECT(ctx, role.entry(zero, entry) && entry.byte_count == 0u && entry.count == 3u && !query.object_child(zero, CStringView{ "data" }));
                }
            }
            TEST_EXPECT(ctx, source.file_bytes().data() == source_address && same_bytes(source.file_bytes(), original) && source.loaded_ready() &&
                T::query(source).root() == source_root && source.asset_id() == id);
            TEST_EXPECT(ctx, !source.strip_to(source, source_schema, diagnostic) && diagnostic.block.reason == EDataBlockReason::destination_not_empty);
            TEST_EXPECT(ctx, !source.strip_to(destination, destination_schema, diagnostic) && diagnostic.block.reason == EDataBlockReason::destination_not_empty);
            if (borrowed) TEST_EXPECT(ctx, assets.resolve(id) != nullptr);
            source.clear();
            if (borrowed) TEST_EXPECT(ctx, assets.resolve(id) != nullptr && assets.erase(id));
            TEST_EXPECT(ctx, destination.loaded_ready() && same_bytes(role.payload_view(), payload));
            //  Already stripped data produces a separate allocation with identical payload.
            typename T::Block again;
            TEST_EXPECT(ctx, destination.strip_to(again, destination_schema, diagnostic) && again.file_bytes().data() != destination.file_bytes().data());
            role.clear();
            destination.clear();
            TEST_EXPECT(ctx, again.loaded_ready() && same_bytes(again.payload_view(), payload));
            TEST_EXPECT(ctx, again.release_local(owner) && owner.buffer_capacity() == owner.buffer_bytes().size());
            TEST_EXPECT(ctx, destination.adopt_local(std::move(owner), diagnostic) && same_bytes(destination.payload_view(), payload));
        }
    }
}

template<bool Instances>
static void test_stripping_failures(TTestContext& ctx)
{
    using T = SBlockTraits<Instances>;
    SSchemaFixture schemas;
    TEST_EXPECT(ctx, schemas.build(stripping_types));
    typename T::Block source, destination;
    typename T::Diagnostic diagnostic;
    TEST_EXPECT(ctx, !source.strip_to(destination, schemas.baked, diagnostic) &&
        diagnostic.block.stage == EDataBlockStage::preconditions && diagnostic.block.reason == EDataBlockReason::invalid_input);
    SFixture fixture;
    TEST_EXPECT(ctx, build_strip_fixture<Instances>(fixture, 2u));
    CBakedDocumentBlock owner;
    TEST_EXPECT(ctx, owner.adopt_buffer(std::move(fixture.file)) && source.adopt_local(std::move(owner), diagnostic));
    const auto snapshot = byte_snapshot(source.file_bytes());
    CBakedSchema unavailable;
    TEST_EXPECT(ctx, !source.strip_to(destination, unavailable, diagnostic) && diagnostic.block.stage == EDataBlockStage::schema_binding &&
        diagnostic.block.reason == EDataBlockReason::schema_unavailable);
    SSchemaFixture missing_types;
    TEST_EXPECT(ctx, missing_types.build(R"({"types":{}})"));
    TEST_EXPECT(ctx, !source.strip_to(destination, missing_types.baked, diagnostic) && diagnostic.block.stage == EDataBlockStage::specialised_loading &&
        diagnostic.block.reason == EDataBlockReason::specialised_failure && diagnostic.role.reason == T::Reason::unknown_type);
    TEST_EXPECT(ctx, diagnostic.block.location.kind == EDataBlockLocationKind::none && diagnostic.role.occurrence &&
        destination.backing() == EEntityBacking::none && missing_types.baked.reference_count() == 0u && same_bytes(source.file_bytes(), snapshot));
    {
        SSchemaFixture temporary;
        TEST_EXPECT(ctx, temporary.build(stripping_types) && source.strip_to(destination, temporary.baked, diagnostic));
    }
    TEST_EXPECT(ctx, destination.loaded_ready());
    const auto occupied = byte_snapshot(destination.file_bytes());
    TEST_EXPECT(ctx, !source.strip_to(destination, schemas.baked, diagnostic) && diagnostic.block.reason == EDataBlockReason::destination_not_empty &&
        same_bytes(destination.file_bytes(), occupied));
    typename T::Block empty;
    TEST_EXPECT(ctx, destination.strip_to(empty, schemas.baked, diagnostic) &&
        empty.backing() == EEntityBacking::local && same_bytes(destination.file_bytes(), occupied));
}

template<bool Instances>
static void test_stripping_allocations(TTestContext& ctx)
{
    using T = SBlockTraits<Instances>;
    SSchemaFixture schemas;
    SFixture fixture;
    TEST_EXPECT(ctx, schemas.build(stripping_types) && build_strip_fixture<Instances>(fixture, 2u));
    CBakedDocumentBlock owner;
    typename T::Block source;
    typename T::Diagnostic diagnostic;
    TEST_EXPECT(ctx, owner.adopt_buffer(std::move(fixture.file)) && source.adopt_local(std::move(owner), diagnostic));
    const auto snapshot = byte_snapshot(source.file_bytes());
    bool completed{}, validation{}, promotion{}, bake{}, assembly{}, attachment{};
    for (std::size_t fail_on = 0u; fail_on != 1024u && !completed; ++fail_on)
    {
        tests::TAllocationFailureFixture failures{ 0u, fail_on };
        memory::CMemoryAllocator allocator{ &failures, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            const tests::TMemoryContextScope scope{ &context };
            typename T::Block destination;
            completed = source.strip_to(destination, schemas.baked, diagnostic);
            if (!completed)
            {
                TEST_EXPECT(ctx, destination.backing() == EEntityBacking::none && !destination.document().is_ready() &&
                    schemas.baked.reference_count() == 0u && diagnostic.block.reason != EDataBlockReason::none);
                const auto stage = diagnostic.block.stage;
                validation |= stage == EDataBlockStage::format_validation;
                promotion |= stage == EDataBlockStage::document_promotion;
                bake |= stage == EDataBlockStage::document_bake;
                assembly |= stage == EDataBlockStage::assembly;
                attachment |= stage == EDataBlockStage::specialised_loading;
                TEST_EXPECT(ctx, (!diagnostic.role.occurrence || T::query(source).contains(diagnostic.role.occurrence)) &&
                    diagnostic.block.location.kind == EDataBlockLocationKind::none);
            }
            TEST_EXPECT(ctx, source.loaded_ready() && schemas.live.reference_count() == 0u && same_bytes(source.file_bytes(), snapshot));
        }
        TEST_EXPECT(ctx, context.is_attribution_empty() && schemas.baked.reference_count() == 0u);
    }
    TEST_EXPECT(ctx, completed && validation && promotion && bake && assembly && attachment);
}

static void test_stripping(TTestContext& ctx)
{
    SSchemaFixture source, destination;
    if (!source.build(stripping_types) || !destination.build(stripping_types)) { TEST_EXPECT(ctx, false); return; }
    test_stripped_blocks<true>(ctx, source.live, destination.live);
    test_stripped_blocks<true>(ctx, source.live, destination.baked);
    test_stripped_blocks<true>(ctx, source.baked, destination.live);
    test_stripped_blocks<true>(ctx, source.baked, destination.baked);
    test_stripped_blocks<false>(ctx, source.live, destination.live);
    test_stripped_blocks<false>(ctx, source.live, destination.baked);
    test_stripped_blocks<false>(ctx, source.baked, destination.live);
    test_stripped_blocks<false>(ctx, source.baked, destination.baked);
    test_stripping_failures<true>(ctx);
    test_stripping_failures<false>(ctx);
    test_stripping_allocations<true>(ctx);
    test_stripping_allocations<false>(ctx);
}
}   // namespace

void run_schema_data_block_cases(tests::TTestContext& context)
{
    test_valid(context);
    test_metadata(context);
    test_bounds(context);
    test_layout(context);
    test_blocks(context);
    test_completed_bakes(context);
    test_materialisation(context);
    test_stripping(context);
}
