
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
#include "schema/data_block_construction.hpp"
#include "schema/baked_data_block.hpp"
#include "schema/live_instances.hpp"
#include "schema/schema.hpp"
#include "schema/live_bulk_data.hpp"
#include "host/runtime/host_payloads.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/byte_helpers.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
using namespace schema;
using namespace schema::detail;
using tests::TTestContext;


//  Parameterise the existing single-domain regression matrix over the common
//  owner. This adapter only selects views and diagnostic fields.
template<bool Instances>
class TDomainBlock : public CBakedDataBlock
{
public:
    using Diagnostic = std::conditional_t<Instances, SInstancesBlockDiagnostic, SBulkDataBlockDiagnostic>;
    CByteConstView payload_view() const noexcept
    {
        return Instances ? instance_payload_view() : bulk_payload_view();
    }
    bool adopt_local(CBakedDocumentBlock&& block, Diagnostic& diagnostic) noexcept
    {
        diagnostic = {};
        return CBakedDataBlock::adopt_local(std::move(block), diagnostic.block);
    }
    bool attach_asset(const CAssetId id, const CByteConstView bytes, Diagnostic& diagnostic) noexcept
    {
        diagnostic = {};
        return CBakedDataBlock::attach_asset(id, bytes, diagnostic.block);
    }

};

struct SSelectedBlockView
{
    CBakedDocument document;
    CBakedValueIndex section;
    CByteConstView payload;
    bool stripped{};
};

static bool validate_domain(const void* bytes, const std::size_t size, const EDataDomain kind,
    SSelectedBlockView& output, SDataBlockDiagnostic& diagnostic)
{
    output = {};
    SDataBlockView view;
    if (!validate_data_block(bytes, size, view, diagnostic)) return false;
    output.document = view.document;
    output.section = view.document.object_child(view.document.root(), CStringView{ kind == EDataDomain::instances ? "instances" : "data" });
    output.payload = (kind == EDataDomain::instances) ? view.instances : view.bulk_data;
    output.stripped = view.stripped;
    return true;
}

static_assert(std::is_trivially_copyable_v<SDataBlockDiagnostic>);
static_assert(std::is_trivially_copyable_v<SInstancesBlockDiagnostic>);
static_assert(std::is_trivially_copyable_v<SBulkDataBlockDiagnostic>);
static_assert(!std::is_copy_constructible_v<TDomainBlock<true>> && !std::is_move_constructible_v<TDomainBlock<true>>);
static_assert(!std::is_copy_constructible_v<TDomainBlock<false>> && !std::is_move_constructible_v<TDomainBlock<false>>);
static_assert(std::is_same_v<decltype(std::declval<TDomainBlock<true>&>().document()), const CBakedDocument&>);
static_assert(std::is_same_v<decltype(std::declval<TDomainBlock<false>&>().document()), const CBakedDocument&>);

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

static std::string envelope(const EDataDomain kind, const std::string& size = "0",
    const std::string& section = "{}", const std::string& extra = {})
{
    const bool instances = kind == EDataDomain::instances;
    return std::string{ "{\"fileType\":\"morphic-schema-data\",\"version\":2,\"" } +
        (instances ? "instancePayloadSize" : "bulkPayloadSize") + "\":" + size + ",\"" +
        (instances ? "instances" : "data") + "\":" + section +
        (extra.find("\"stripped\"") == std::string::npos ? ",\"stripped\":true" : "") + extra + "}";
}

static void seed(SDataBlockDiagnostic& diagnostic)
{
    diagnostic.stage = EDataBlockStage::assembly;
    diagnostic.reason = EDataBlockReason::assembly_failed;
    diagnostic.location.kind = EDataBlockLocationKind::file_byte;
    diagnostic.location.byte_offset = 37u;
}

static void expect_empty(TTestContext& ctx, const SSelectedBlockView& view)
{
    TEST_EXPECT(ctx, !view.document.is_ready() && !view.section && !view.payload.is_ready() && !view.stripped);
}

static void expect_failure(TTestContext& ctx, const void* const bytes, const std::size_t size,
    const EDataDomain kind, const EDataBlockReason reason, const EDataBlockLocationKind location,
    const CBakedValueIndex value = {}, const std::uint64_t offset = 0u,
    const EDataBlockStage stage = EDataBlockStage::format_validation)
{
    SSelectedBlockView output;
    output.stripped = true;
    SDataBlockDiagnostic diagnostic;
    seed(diagnostic);
    TEST_EXPECT(ctx, !validate_domain(bytes, size, kind, output, diagnostic));
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
    for (const auto kind : { EDataDomain::instances, EDataDomain::bulk_data })
    {
        {
            SFixture fixture;
            if (!fixture.build(envelope(kind, "0", "{}", ",\"stripped\":true"))) { TEST_EXPECT(ctx, false); continue; }
            SSelectedBlockView output;
            SDataBlockDiagnostic diagnostic;
            seed(diagnostic);
            TEST_EXPECT(ctx, validate_domain(fixture.file.data(), fixture.file.size(), kind, output, diagnostic));
            TEST_EXPECT(ctx, diagnostic.stage == EDataBlockStage::none && diagnostic.reason == EDataBlockReason::none &&
                diagnostic.location.kind == EDataBlockLocationKind::none && diagnostic.location.byte_offset == 0u &&
                !diagnostic.location.baked_value && !diagnostic.location.live_node.is_valid());
            TEST_EXPECT(ctx, output.document.byte_count() == fixture.file.size() && output.section &&
                !output.payload.is_ready() && output.payload.data() == nullptr && output.payload.size() == 0u);
            TEST_EXPECT(ctx, output.stripped);
            TEST_EXPECT(ctx, !validate_domain(nullptr, 0u, kind, output, diagnostic));
            expect_empty(ctx, output);
        }

        //  Root order is not canonical; nested structure is the role's concern.
        const bool instances = kind == EDataDomain::instances;
        const std::string reordered = std::string{ "{\"" } + (instances ? "instances" : "data") +
            "\":{\"opaque\":{}},\"" + std::string{ instances ? "instancePayloadSize" : "bulkPayloadSize" } + "\":7,\"stripped\":true,\"version\":2,\"fileType\":\"" +
            (instances ? "morphic-schema-data" : "morphic-schema-data") + "\"}";
        SFixture fixture;
        if (!fixture.build(reordered, 7u)) { TEST_EXPECT(ctx, false); continue; }
        SSelectedBlockView output;
        SDataBlockDiagnostic diagnostic;
        TEST_EXPECT(ctx, validate_domain(fixture.file.data(), fixture.file.size(), kind, output, diagnostic));
        TEST_EXPECT(ctx, output.document.byte_count() == fixture.prefix.bytes().size() &&
            output.payload.data() == fixture.file.data() + fixture.payload_offset && output.payload.size() == 7u &&
            output.payload.align() == 128u && output.payload.data()[0] == 0xa5u && output.payload.data()[6] == 0xa5u);
        TEST_EXPECT(ctx, std::memcmp(fixture.file.data(), fixture.prefix.bytes().data(), fixture.prefix.bytes().size()) == 0);

        SFixture zero_type;
        TEST_EXPECT(ctx, zero_type.build(envelope(kind, "0", R"({"Zero":{"one":{"locator":{"offset":0,"count":1,"size":0,"valid":true}}}})")));
        TEST_EXPECT(ctx, validate_domain(zero_type.file.data(), zero_type.file.size(), kind, output, diagnostic) && !output.payload.is_ready());
    }
}

static void test_metadata(TTestContext& ctx)
{
    struct SCase { const char* json; EDataBlockReason reason; const char* property; };
    const SCase cases[] = {
        { R"([])", EDataBlockReason::invalid_input, "" },
        { R"({})", EDataBlockReason::missing_property, "" },
        { R"({"version":2,"stripped":true,"instancePayloadSize":0,"instances":{}})", EDataBlockReason::missing_property, "" },
        { R"({"fileType":"morphic-schema-data","instances":{}})", EDataBlockReason::missing_property, "" },
        { R"({"fileType":"morphic-schema-data","version":2,"stripped":true,"instancePayloadSize":0})", EDataBlockReason::missing_property, "instancePayloadSize" },
        { R"({"fileType":7,"version":2,"stripped":true})", EDataBlockReason::invalid_property_type, "fileType" },
        { R"({"fileType":"morphic-instance-data","version":2,"stripped":true})", EDataBlockReason::wrong_content_type, "fileType" },
        { R"({"fileType":"morphic-bulk-data","version":2,"stripped":true})", EDataBlockReason::wrong_content_type, "fileType" },
        { R"({"fileType":"","version":2,"stripped":true})", EDataBlockReason::wrong_content_type, "fileType" },
        { R"({"fileType":"morphic-schema-data","version":+2})", EDataBlockReason::invalid_property_type, "version" },
        { R"({"fileType":"morphic-schema-data","version":2.0})", EDataBlockReason::invalid_property_type, "version" },
        { R"({"fileType":"morphic-schema-data","version":true})", EDataBlockReason::invalid_property_type, "version" },
        { R"({"fileType":"morphic-schema-data","version":0})", EDataBlockReason::unsupported_format_version, "version" },
        { R"({"fileType":"morphic-schema-data","version":1})", EDataBlockReason::unsupported_format_version, "version" },
        { R"({"fileType":"morphic-schema-data","version":3})", EDataBlockReason::unsupported_format_version, "version" },
        { R"({"fileType":"morphic-schema-data","version":2})", EDataBlockReason::missing_property, "" },
        { R"({"fileType":"morphic-schema-data","version":2,"stripped":false})", EDataBlockReason::invalid_input, "stripped" },
        { R"({"fileType":"morphic-schema-data","version":2,"stripped":true,"instancePayloadSize":+0,"instances":{}})", EDataBlockReason::invalid_property_type, "instancePayloadSize" },
        { R"({"fileType":"morphic-schema-data","version":2,"stripped":true,"instancePayloadSize":0.0,"instances":{}})", EDataBlockReason::invalid_property_type, "instancePayloadSize" },
        { R"({"fileType":"morphic-schema-data","version":2,"stripped":true,"instancePayloadSize":"0","instances":{}})", EDataBlockReason::invalid_property_type, "instancePayloadSize" },
        { R"({"fileType":"morphic-schema-data","version":2,"stripped":true,"instances":[]})", EDataBlockReason::invalid_property_type, "instances" },
        { R"({"fileType":"morphic-schema-data","version":2,"stripped":0})", EDataBlockReason::invalid_property_type, "stripped" },
        { R"({"fileType":"morphic-schema-data","version":2,"stripped":true,"payloadSize":0})", EDataBlockReason::unknown_property, "payloadSize" },
        { R"({"fileType":"morphic-schema-data","version":2,"stripped":true,"extension":null})", EDataBlockReason::unknown_property, "extension" },
        { R"({"fileType":"morphic-schema-data","version":2,"stripped":true,"instancePayloadSize":1,"instances":{}})", EDataBlockReason::invalid_input, "instancePayloadSize" },
        { R"({"fileType":"morphic-schema-data","version":2,"stripped":true,"instancePayloadSize":18446744073709551615,"instances":{"x":{}}})", EDataBlockReason::storage_limit, "instancePayloadSize" },
        { R"({"fileType":"morphic-schema-data","version":2,"stripped":true,"instancePayloadSize":2147483648,"instances":{"x":{}}})", EDataBlockReason::storage_limit, "" }
    };
    for (const auto& item : cases)
    {
        for (const auto kind : { EDataDomain::instances, EDataDomain::bulk_data })
        {
            std::string json = item.json;
            std::string property = item.property;
            //  Swap both vocabulary and identifiers to exercise symmetric rejection.
            if (kind == EDataDomain::bulk_data)
            {
                const auto swap_text = [&json](const std::string& a, const std::string& b)
                {
                    std::size_t pos{};
                    while ((pos = json.find(a, pos)) != std::string::npos) { json.replace(pos, a.size(), b); pos += b.size(); }
                };
                swap_text("instancePayloadSize", "bulkPayloadSize");
                if (property == "instancePayloadSize") property = "bulkPayloadSize";
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
    if (!fixture.build(envelope(EDataDomain::instances, "3", R"({"opaque":{}})"), 3u)) { TEST_EXPECT(ctx, false); return; }
    const auto kind = EDataDomain::instances;
    const auto payload_value = fixture.prefix.document().root();
    expect_failure(ctx, nullptr, 0u, kind, EDataBlockReason::invalid_input, EDataBlockLocationKind::none, {}, 0u, EDataBlockStage::preconditions);
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
        SSelectedBlockView output;
        SDataBlockDiagnostic diagnostic;
        TEST_EXPECT(ctx, validate_domain(padded.file.data(), padded.file.size(), kind, output, diagnostic));
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
        EDataBlockLocationKind::source_baked_value, empty.prefix.document().root());

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
        TEST_EXPECT(ctx, data_block_layout(item.document, item.payload, 0u, layout, reason) == (item.reason == EDataBlockReason::none));
        TEST_EXPECT(ctx, reason == item.reason && layout.instance_offset == item.offset && layout.file_size == item.size);
    }
}

struct SSchemaFixture
{
    CLiveTypes live;
    CBakedDocumentBlock storage;
    CBakedTypes baked;

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
    using Block = std::conditional_t<Instances, TDomainBlock<true>, TDomainBlock<false>>;
    using Diagnostic = std::conditional_t<Instances, SInstancesBlockDiagnostic, SBulkDataBlockDiagnostic>;
    using Reason = std::conditional_t<Instances, EInstanceLoadReason, EBulkDataLoadReason>;
    static constexpr EDataDomain kind = Instances ? EDataDomain::instances : EDataDomain::bulk_data;

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
    static bool attach(Role& role, const Block& block, TSchema& schema, RoleDiagnostic& diagnostic)
    {
        return role.set_document(block.document()) && role.attach_types(schema, block.payload_view(), diagnostic);
    }

    static std::string entry(const unsigned offset)
    {
        const std::string locator = "\"locator\":{\"offset\":" + std::to_string(offset) + ",\"valid\":true" +
            (Instances ? "}" : ",\"count\":1,\"size\":1}");
        return "{" + locator + (Instances ? ",\"declaration\":true" : "") + "}";
    }

    static std::string section()
    {
        //  Leading/inter-entry gaps and trailing storage remain opaque payload bytes.
        return "{\"u8\":{\"one\":" + entry(2u) + ",\"two\":" + entry(6u) + "}}";
    }

    template<class TSchema>
    static void check_entry(TTestContext& ctx, const Block& block, TSchema& schema, const std::uint8_t* const payload)
    {
        Role role;
        RoleDiagnostic diagnostic;
        TEST_EXPECT(ctx, attach(role, block, schema, diagnostic));
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
    if (!fixture.build(envelope(T::kind, "9", T::section()), 9u)) { TEST_EXPECT(ctx, false); return; }
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
    struct SCase { std::string section; typename T::Reason reason; };
    const SCase cases[] = {
        { R"({"Unknown":{"one":{}}})", T::Reason::unknown_type },
        { "{\"u8\":{\"one\":" + T::entry(9u) + "}}", T::Reason::invalid_range },
        { "{\"u8\":{\"one\":" + T::entry(2u) + ",\"two\":" + T::entry(2u) + "}}", T::Reason::overlap }
    };
    for (const auto& item : cases)
    {
        SFixture fixture;
        if (!fixture.build(envelope(T::kind, "9", item.section), 9u)) { TEST_EXPECT(ctx, false); continue; }
        const auto* const original = fixture.file.data();
        const std::string snapshot{ reinterpret_cast<const char*>(original), fixture.file.size() };
        CBakedDocumentBlock source;
        TEST_EXPECT(ctx, source.adopt_buffer(std::move(fixture.file)));
        typename T::Block block;
        typename T::Diagnostic diagnostic;
        TEST_EXPECT(ctx, block.adopt_local(std::move(source), diagnostic));
        typename T::Role role;
        typename T::RoleDiagnostic error;
        TEST_EXPECT(ctx, !T::attach(role, block, schema, error));
        TEST_EXPECT(ctx, block.loaded_ready() && block.file_bytes().data() == original && !role.loaded_ready());
        TEST_EXPECT(ctx, schema.reference_count() == 0u && std::memcmp(original, snapshot.data(), snapshot.size()) == 0);
        TEST_EXPECT(ctx, error.reason == item.reason && T::query(block).contains(error.occurrence));

    }

    for (const bool zero_type : { false, true })
    {
        SFixture fixture;
        const std::string zero = Instances ?
            R"({"Zero":{"one":{"locator":{"offset":0,"valid":true}}}})" :
            R"({"Zero":{"one":{"locator":{"offset":0,"valid":true,"count":1,"size":0}}}})";
        TEST_EXPECT(ctx, fixture.build(envelope(T::kind, "0", zero_type ? zero : "{}", ",\"stripped\":true")));
        CBakedDocumentBlock source;
        TEST_EXPECT(ctx, source.adopt_buffer(std::move(fixture.file)));
        {
            typename T::Block block;
            typename T::Diagnostic diagnostic;
            TEST_EXPECT(ctx, block.adopt_local(std::move(source), diagnostic));
            TEST_EXPECT(ctx, block.loaded_ready() && T::stripped(block) &&
                block.payload_view().size() == 0u && schema.reference_count() == 0u);
        }
        TEST_EXPECT(ctx, schema.reference_count() == 0u);
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
        TEST_EXPECT(ctx, role.set_document(block.document()) && !role.attach_types(schema, block.payload_view(), error));
        TEST_EXPECT(ctx, schemas.build());
        TEST_EXPECT(ctx, role.attach_types(schema, block.payload_view(), error) && schema.reference_count() == 1u);
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
    CBakedTypes* schema{};
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
    TEST_EXPECT(ctx, fixture.build(envelope(T::kind, "9", T::section()), 9u));
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
        values[0] = 9u;
        TEST_EXPECT(ctx, source.overwrite(base, CByteConstView{ values, 1u, 128u }, EInstanceDescendantPolicy::preserve, diagnostic));
        CInstanceEditToken token;
        SMutableInstanceEntryView entry;
        if (!source.begin_edit(child, token, entry, diagnostic)) { TEST_EXPECT(ctx, false); return false; }
        entry.bytes.data()[0] = 11u;
        TEST_EXPECT(ctx, source.complete_edit(token, EInstanceDescendantPolicy::propagate, diagnostic));
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

template<bool Instances, class TSourceSchema, class TDestinationSchema>
static void test_completed_bake(TTestContext& ctx, TSourceSchema& source_schema, TDestinationSchema& destination_schema)
{
    using T = SBlockTraits<Instances>;
    for (unsigned content = 0u; content != 3u; ++content)
    {
        typename T::Live source;
        if (!populate_bake_source<Instances>(ctx, source, source_schema, content)) continue;
        const auto original_query = source.document_query();
        const auto original_root = original_query.root();
        const auto original_bytes = source.payload_view();
        const std::string snapshot = byte_snapshot(original_bytes);
        CLiveDocument baseline_description;
        CBakedDocumentBlock baseline_document;
        CByteBuffer baseline_payload;
        typename T::Role baseline;
        typename T::RoleDiagnostic baseline_diagnostic;
        TEST_EXPECT(ctx, source.prepare_output(baseline_description, baseline_payload, baseline_diagnostic) &&
            document_translation::bake(baseline_description, baseline_document) && baseline.set_document(baseline_document.document()) &&
            baseline.attach_types(destination_schema, baseline_payload.const_view(), baseline_diagnostic));
        typename T::Block result;
        typename T::Diagnostic diagnostic;
        seed(diagnostic.block);
        diagnostic.role.reason = T::Reason::unknown_type;
        const bool baked = source.bake(result, diagnostic);
        TEST_EXPECT(ctx, baked);
        if (!baked)
        {
            std::cerr << "Completed bake: instances=" << Instances
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
        SSelectedBlockView view;
        SDataBlockDiagnostic format_diagnostic;
        TEST_EXPECT(ctx, validate_domain(bytes.data(), bytes.size(), T::kind, view, format_diagnostic));
        TEST_EXPECT(ctx, view.stripped);
        //  Newly packed gaps are zero; complete records retain their source bytes.
        TEST_EXPECT(ctx, tests::same_bytes(view.payload, byte_snapshot(baseline_payload.const_view())));
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
                TEST_EXPECT(ctx, static_cast<bool>(query.object_child(base, CStringView{ "declaration" })) == true);
                TEST_EXPECT(ctx, static_cast<bool>(query.object_child(child, CStringView{ "declaration" })) == true);
            }
            else
            {
                const auto handle = result_role.find_entry(CStringView{ "u8" }, CStringView{ "one" });
                SBulkDataEntryView entry;
                TEST_EXPECT(ctx, result_role.entry(handle, entry));
                TEST_EXPECT(ctx, entry.count == 2u && entry.bytes[0] == 9u && entry.bytes[1] == 8u);
                TEST_EXPECT(ctx, static_cast<bool>(query.object_child(handle, CStringView{ "data" })) == false);
                const auto zero = result_role.find_entry(CStringView{ "Zero" }, CStringView{ "zero" });
                TEST_EXPECT(ctx, result_role.entry(zero, entry) && entry.count == 3u && entry.byte_count == 0u && entry.bytes == nullptr);
            }
        }
        TEST_EXPECT(ctx, source.loaded_ready() && source.document_query().root() == original_root &&
            source.payload_view().data() == original_bytes.data() && tests::same_bytes(source.payload_view(), snapshot));
        TEST_EXPECT(ctx, !source.bake(result, diagnostic) &&
            diagnostic.block.reason == EDataBlockReason::destination_not_empty && result.file_bytes().data() == bytes.data());
        source.clear();
        TEST_EXPECT(ctx, result.loaded_ready() && tests::same_bytes(result.payload_view(), completed_snapshot));
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
        TEST_EXPECT(ctx, independent.loaded_ready() && tests::same_bytes(independent.payload_view(), completed_snapshot));
        TEST_EXPECT(ctx, independent.release_local(owner) && owner.buffer_capacity() == bytes.size());
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
    TEST_EXPECT(ctx, !source.bake(destination, diagnostic) && !destination.loaded_ready());
    if (!populate_bake_source<Instances>(ctx, source, schemas.live, 2u)) return;
    const auto snapshot = byte_snapshot(source.payload_view());
    CLiveDocument empty_types;
    CLiveTypes incompatible;
    SDiagnostic types_error;
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{}})", empty_types) && incompatible.try_adopt(std::move(empty_types)) && incompatible.resolve(types_error));
    typename T::RoleDiagnostic role_error;
    TEST_EXPECT(ctx, !source.rebind_types(incompatible, ETypeBindMode::checked, role_error) &&
        role_error.reason == T::Reason::incompatible_schema && source.document_query().contains(role_error.occurrence));
    TEST_EXPECT(ctx, incompatible.reference_count() == 0u && tests::same_bytes(source.payload_view(), snapshot));

    // Binary persistence does not require a faithful authoring literal.
    typename T::Live noncanonical;
    TEST_EXPECT(ctx, noncanonical.initialise(schemas.live));
    alignas(128) const std::uint8_t boolean[]{ 2u };
    if constexpr (Instances)
        TEST_EXPECT(ctx, noncanonical.capture_base(CStringView{ "b8" }, CStringView{ "bad" }, CByteConstView{ boolean, 1u, 128u }, role_error));
    else
        TEST_EXPECT(ctx, noncanonical.capture(CStringView{ "b8" }, CStringView{ "bad" }, CByteConstView{ boolean, 1u, 128u }, 1u, role_error));
    TEST_EXPECT(ctx, noncanonical.bake(destination, diagnostic) && destination.payload_view().data()[0] == 2u);
    CSchema exported;
    SSchemaDiagnostic export_error;
    TEST_EXPECT(ctx, !exported.import_from(noncanonical, export_error) && !exported.document_ready());
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
            completed = source.bake(output, diagnostic);
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
            TEST_EXPECT(ctx, source.loaded_ready() && schemas.live.reference_count() == 1u && tests::same_bytes(source.payload_view(), snapshot));
        }
        TEST_EXPECT(ctx, context.is_attribution_empty() && schemas.baked.reference_count() == 0u);
    }
    TEST_EXPECT(ctx, completed);
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
        for (unsigned repetition = 0u; repetition < 2u; ++repetition)
        {
            typename T::Block output, repeated;
            typename T::Diagnostic diagnostic;
            TEST_EXPECT(ctx, source.bake(output, diagnostic));
            const auto bytes = output.payload_view();
            TEST_EXPECT(ctx, bytes.size() == 12u && bytes.data()[0] == 1u && std::memcmp(bytes.data() + 4u, padded, sizeof(padded)) == 0);
            for (std::size_t i = 1u; i != 4u; ++i) TEST_EXPECT(ctx, bytes.data()[i] == 0u);
            TEST_EXPECT(ctx, source.bake(repeated, diagnostic) && tests::same_bytes(repeated.file_bytes(), byte_snapshot(output.file_bytes())));
            TEST_EXPECT(ctx, tests::same_bytes(source.payload_view(), original));

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
template<bool Instances, class TSchema>
static void test_common_block_attachment(TTestContext& ctx, TSchema& schema)
{
    using T = SBlockTraits<Instances>;
    typename T::Live source;
    TEST_EXPECT(ctx, populate_bake_source<Instances>(ctx, source, schema, 2u));
    typename T::Block block;
    typename T::Diagnostic build_error;
    TEST_EXPECT(ctx, source.bake(block, build_error));
    source.clear();
    const auto snapshot = byte_snapshot(block.file_bytes());
    typename T::Role role;
    typename T::RoleDiagnostic diagnostic;
    TEST_EXPECT(ctx, role.attach_block(block, schema, diagnostic) && role.loaded_ready() && schema.reference_count() == 1u);
    TEST_EXPECT(ctx, role.payload_view().data() == block.payload_view().data());
    TEST_EXPECT(ctx, !role.attach_block(block, schema, diagnostic) && role.loaded_ready());
    TEST_EXPECT(ctx, tests::same_bytes(block.file_bytes(), snapshot));
    role.clear();
    TEST_EXPECT(ctx, schema.reference_count() == 0u);

    SFixture invalid;
    TEST_EXPECT(ctx, invalid.build(envelope(T::kind, "0", R"({"Missing":{"one":{}}})")));
    CBakedDocumentBlock storage;
    typename T::Block bad;
    TEST_EXPECT(ctx, storage.adopt_buffer(std::move(invalid.file)) && bad.adopt_local(std::move(storage), build_error));
    TEST_EXPECT(ctx, !role.attach_block(bad, schema, diagnostic) && diagnostic.reason == T::Reason::unknown_type);
    TEST_EXPECT(ctx, !role.document_ready() && !role.loaded_ready() && schema.reference_count() == 0u);
    const auto query = T::query(bad);
    TEST_EXPECT(ctx, query.contains(diagnostic.occurrence) && query.name(diagnostic.occurrence) == CStringView{ "Missing" });

    CLiveDocument empty;
    CBakedDataBlock absent;
    SDataBlockDiagnostic storage_error;
    TEST_EXPECT(ctx, tests::parse_document("{}", empty) && absent.assemble(empty, {}, storage_error));
    TEST_EXPECT(ctx, role.attach_block(absent, schema, diagnostic) && role.loaded_ready() && role.payload_view().size() == 0u);
    role.clear();
    TEST_EXPECT(ctx, schema.reference_count() == 0u);
}

template<bool Instances>
static void test_common_root_diagnostic_order(TTestContext& ctx, CBakedTypes& schema)
{
    using T = SBlockTraits<Instances>;
    for (const char* text : {
        R"({"fileType":"morphic-schema-data","version":2,"types":{}})",
        R"({"fileType":"morphic-schema-data","version":2,"stripped":false,"types":{}})",
        R"({"fileType":"morphic-schema-data","version":2,"stripped":true,"types":{},"instances":{}})",
        R"({"fileType":"morphic-schema-data","version":2,"stripped":true,"types":{},"data":{}})" })
    {
        CLiveDocument source;
        CBakedDocumentBlock baked;
        TEST_EXPECT(ctx, tests::parse_document(text, source) && document_translation::bake(source, baked));
        const auto snapshot = byte_snapshot(baked.bytes());
        typename T::Role role;
        typename T::RoleDiagnostic diagnostic;
        TEST_EXPECT(ctx, role.set_document(baked.document()) && !role.attach_types(schema, CByteConstView{}, diagnostic));
        TEST_EXPECT(ctx, !role.loaded_ready() && schema.reference_count() == 0u && role.document_query().contains(diagnostic.occurrence));
        CResolvedTypes types;
        SDiagnostic types_error;
        TEST_EXPECT(ctx, !types.resolve(baked.document(), types_error) && CTypesDocumentQuery{ baked.document() }.contains(types_error.occurrence));
        TEST_EXPECT(ctx, tests::same_bytes(baked.bytes(), snapshot));
    }
}

static void test_common_block_roles(TTestContext& ctx)
{
    SSchemaFixture schemas;
    TEST_EXPECT(ctx, schemas.build());
    test_common_block_attachment<true>(ctx, schemas.live);
    test_common_block_attachment<false>(ctx, schemas.live);
    test_common_block_attachment<true>(ctx, schemas.baked);
    test_common_block_attachment<false>(ctx, schemas.baked);
    test_common_root_diagnostic_order<true>(ctx, schemas.baked);
    test_common_root_diagnostic_order<false>(ctx, schemas.baked);
}

//  Retain the previous staging route as a byte-equivalence reference. Only the
//  unchanged final metadata/framing writer is shared with the new assembly path.
template<class TDocument>
static bool assemble_with_previous_staging(const TDocument& source, const SDataBlockPayloads& payloads,
    CBakedDataBlock& output, SDataBlockDiagnostic& error)
{
    CBakedDocumentBlock copied;
    if constexpr (std::is_same_v<TDocument, CLiveDocument>)
    {
        if (!document_translation::bake(source, copied)) return false;
    }
    else
    {
        CLiveDocument promoted;
        if (!document_translation::promote(source, promoted) || !document_translation::bake(promoted, copied)) return false;
    }
    CLiveDocument description;
    if (!document_translation::promote(copied.document(), description)) return false;
    copied.deallocate();
    CBakedDocumentBlock complete;
    return assemble_data_block(description, payloads.instances, payloads.bulk_data, complete, error) &&
        output.adopt_local(std::move(complete), error);
}

template<class TDocument>
static void expect_assembly_equivalent(TTestContext& ctx, const TDocument& source, const SDataBlockPayloads& payloads)
{
    CBakedDataBlock previous, output;
    SDataBlockDiagnostic error;
    TEST_EXPECT(ctx, assemble_with_previous_staging(source, payloads, previous, error));
    TEST_EXPECT(ctx, output.assemble(source, payloads, error));
    TEST_EXPECT(ctx, error.stage == EDataBlockStage::none && error.reason == EDataBlockReason::none &&
        error.location.kind == EDataBlockLocationKind::none);
    TEST_EXPECT(ctx, tests::same_bytes(output.file_bytes(), previous.file_bytes()));
}

template<class TDocument>
static void test_assembly_allocation_failures(TTestContext& ctx, const TDocument& source,
    const SDataBlockPayloads& payloads, const CBakedDocumentBlock& source_snapshot, const CByteConstView source_bytes = {})
{
    CBakedDataBlock expected;
    SDataBlockDiagnostic expected_error;
    TEST_EXPECT(ctx, assemble_with_previous_staging(source, payloads, expected, expected_error));
    const auto instance_snapshot = byte_snapshot(*payloads.instances);
    const auto bulk_snapshot = byte_snapshot(*payloads.bulk_data);
    bool completed{}, preparation_failed{};
    for (std::size_t fail_on = 0u; fail_on < 1024u && !completed; ++fail_on)
    {
        tests::TAllocationFailureFixture failures{ 0u, fail_on };
        memory::CMemoryAllocator allocator{ &failures, &tests::allocate_with_failure, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            const tests::TMemoryContextScope scope{ &context };
            CBakedDataBlock candidate;
            SDataBlockDiagnostic error;
            completed = candidate.assemble(source, payloads, error);
            if (completed)
            {
                TEST_EXPECT(ctx, tests::same_bytes(candidate.file_bytes(), expected.file_bytes()));
            }
            else
            {
                TEST_EXPECT(ctx, !candidate.loaded_ready() && error.reason != EDataBlockReason::none &&
                    error.location.kind == EDataBlockLocationKind::none);
                if constexpr (std::is_same_v<TDocument, CLiveDocument>)
                {
                    if (error.stage == EDataBlockStage::description_preparation)
                    {
                        TEST_EXPECT(ctx, error.reason == EDataBlockReason::description_preparation_failed);
                        preparation_failed = true;
                    }
                }
                else if (error.stage == EDataBlockStage::document_promotion)
                {
                    TEST_EXPECT(ctx, error.reason == EDataBlockReason::document_promotion_failed);
                    preparation_failed = true;
                }
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
        TEST_EXPECT(ctx, tests::same_bytes(*payloads.instances, instance_snapshot) && tests::same_bytes(*payloads.bulk_data, bulk_snapshot));
        if constexpr (std::is_same_v<TDocument, CLiveDocument>)
        {
            TEST_EXPECT(ctx, tests::same_baked_document(source, source_snapshot));
        }
        else
        {
            TEST_EXPECT(ctx, tests::same_bytes(source_bytes, source_snapshot.bytes()));
        }
    }
    TEST_EXPECT(ctx, completed && preparation_failed);
}

static void test_assembly_staging(TTestContext& ctx)
{
    CLiveDocument source;
    TEST_EXPECT(ctx, tests::parse_document(R"({"bulkPayloadSize":1,"data":{"opaque":[3,2,1]},"version":2,
        "stripped":true,"types":{},"instancePayloadSize":1,"instances":{"z":"zeta","a":"alpha"},
        "fileType":"morphic-schema-data"})", source));
    const CNodeKey root = source.root();
    const CNodeKey section = source.object_child(root, CStringView{ "types" });
    const CNodeKey detached = source.create_string(CStringView{ "unreferenced text" }, CStringView{ "unused name" });
    TEST_EXPECT(ctx, detached && source.is_detached(detached));
    const CNodeKey placeholder = tests::add_document_child(ctx, source, section, source.create_empty(CStringView{ "pending" }));
    const CNodeKey text = tests::add_document_child(ctx, source, section,
        source.create_string(CStringView{ "line one\nline two" }, CStringView{ "lines" }));
    TEST_EXPECT(ctx, source.set_newline_escaping_suppressed(text, true));
    const CIntegerMetadata metadata{ EIntegerDomain::signed_value, EIntegerWidth::bits_8,
        EIntegerNotation::hexadecimal_8, EIntegerPrefix::alternate };
    tests::add_document_child(ctx, source, section, source.create_signed_integer(-7, metadata, CStringView{ "number" }));
    tests::add_document_child(ctx, source, section, source.create_floating_point(-0.0, CStringView{ "negativeZero" }));
    const CNodeKey array = tests::add_document_child(ctx, source, section, source.create_array(CStringView{ "mixed" }));
    tests::add_document_child(ctx, source, array, source.create_string(CStringView{ "alpha" }, CStringView{ "named" }));
    tests::add_document_child(ctx, source, array, source.create_boolean(true));
    tests::add_document_child(ctx, source, array, source.create_object());
    tests::add_document_child(ctx, source, section, source.create_null(CStringView{ "" }));
    CBakedDocumentBlock baked, snapshot;
    TEST_EXPECT(ctx, document_translation::bake(source, baked) && document_translation::bake(source, snapshot));
    std::uint8_t first_bytes[]{ 0x80u, 0x00u, 0xffu }, second_bytes[]{ 0x11u, 0x22u, 0x00u, 0x00u, 0xfeu };
    const CByteConstView first{ first_bytes, sizeof(first_bytes), 1u }, second{ second_bytes, sizeof(second_bytes), 1u }, empty;
    for (const CByteConstView* instances : { &empty, &first })
    {
        for (const CByteConstView* bulk : { &empty, &second })
        {
            expect_assembly_equivalent(ctx, source, { instances, bulk });
            expect_assembly_equivalent(ctx, baked.document(), { instances, bulk });
        }
    }
    test_assembly_allocation_failures(ctx, source, { &first, &second }, snapshot);
    test_assembly_allocation_failures(ctx, baked.document(), { &first, &second }, snapshot, baked.bytes());
    TEST_EXPECT(ctx, source.root() == root && source.object_child(root, CStringView{ "types" }) == section &&
        source.value_type(placeholder) == ELiveValueType::empty && source.is_detached(detached));

    //  Output owns the document and both binaries after all inputs disappear.
    CBakedDataBlock from_live, from_baked;
    SDataBlockDiagnostic error;
    TEST_EXPECT(ctx, from_live.assemble(source, { &first, &second }, error) &&
        from_baked.assemble(baked.document(), { &first, &second }, error));
    const auto expected = byte_snapshot(from_live.file_bytes());
    source.deallocate();
    baked.deallocate();
    std::memset(first_bytes, 0, sizeof(first_bytes));
    std::memset(second_bytes, 0, sizeof(second_bytes));
    TEST_EXPECT(ctx, tests::same_bytes(from_live.file_bytes(), expected) && tests::same_bytes(from_baked.file_bytes(), expected));
    SDataBlockView view;
    TEST_EXPECT(ctx, validate_data_block(from_live.file_bytes().data(), from_live.file_bytes().size(), view, error));

    //  Whole-document translation supports opaque trees beyond subtree-copy depth.
    CLiveDocument deep;
    TEST_EXPECT(ctx, deep.initialise());
    CNodeKey parent = tests::add_document_child(ctx, deep, deep.root(), deep.create_object(CStringView{ "types" }));
    for (unsigned depth = 0u; depth < 512u; ++depth)
    {
        parent = tests::add_document_child(ctx, deep, parent, deep.create_object(CStringView{ "nested" }));
    }
    tests::add_document_child(ctx, deep, parent, deep.create_empty(CStringView{ "leaf" }));
    CBakedDocumentBlock deep_baked;
    TEST_EXPECT(ctx, document_translation::bake(deep, deep_baked));
    expect_assembly_equivalent(ctx, deep, {});
    expect_assembly_equivalent(ctx, deep_baked.document(), {});
}

template<class TDocument>
static void test_assembly_diagnostic_order(TTestContext& ctx, const TDocument& source)
{
    SDataBlockDiagnostic error;
    CBakedDataBlock output;
    const CByteConstView empty;
    //  Original malformed metadata wins over the missing supplied-payload section.
    TEST_EXPECT(ctx, !output.assemble(source, { &empty, nullptr }, error) && !output.loaded_ready());
    TEST_EXPECT(ctx, error.stage == EDataBlockStage::format_validation && error.reason == EDataBlockReason::wrong_content_type);
    const auto file_type = source.object_child(source.root(), CStringView{ "fileType" });
    if constexpr (std::is_same_v<TDocument, CLiveDocument>)
    {
        TEST_EXPECT(ctx, error.location.kind == EDataBlockLocationKind::source_live_node && error.location.live_node == file_type);
    }
    else
    {
        TEST_EXPECT(ctx, error.location.kind == EDataBlockLocationKind::source_baked_value && error.location.baked_value == file_type);
    }
    CLiveDocument valid;
    TEST_EXPECT(ctx, valid.initialise() && output.assemble(valid, {}, error));
    const auto snapshot = byte_snapshot(output.file_bytes());
    TEST_EXPECT(ctx, !output.assemble(source, {}, error) && error.stage == EDataBlockStage::preconditions &&
        error.reason == EDataBlockReason::destination_not_empty && error.location.kind == EDataBlockLocationKind::none);
    TEST_EXPECT(ctx, tests::same_bytes(output.file_bytes(), snapshot));
}

static void test_assembly_diagnostics(TTestContext& ctx)
{
    CLiveDocument invalid;
    CBakedDocumentBlock baked;
    TEST_EXPECT(ctx, tests::parse_document(R"({"fileType":"wrong","version":1})", invalid) && document_translation::bake(invalid, baked));
    test_assembly_diagnostic_order(ctx, invalid);
    test_assembly_diagnostic_order(ctx, baked.document());
    for (const char* const text : { "{}", R"({"instances":{},"data":{}})" })
    {
        CLiveDocument source;
        CBakedDocumentBlock source_baked;
        TEST_EXPECT(ctx, tests::parse_document(text, source) && document_translation::bake(source, source_baked));
        const std::uint8_t byte = 1u;
        const CByteConstView bytes{ &byte, 1u, 1u }, empty;
        const auto check = [&](const auto& description)
        {
            for (bool bulk : { false, true })
            {
                for (const auto* payload : { &empty, &bytes })
                {
                    CBakedDataBlock output;
                    SDataBlockDiagnostic error;
                    const bool has_section = source.child_count(source.root()) != 0u;
                    const bool success = output.assemble(description, { bulk ? nullptr : payload, bulk ? payload : nullptr }, error);
                    TEST_EXPECT(ctx, !success);
                    if (!success)
                    {
                        TEST_EXPECT(ctx, !output.loaded_ready() && error.location.kind != EDataBlockLocationKind::none);
                        TEST_EXPECT(ctx, error.stage == EDataBlockStage::preconditions && error.reason == EDataBlockReason::missing_property);
                    }
                }
            }
        };
        check(source);
        check(source_baked.document());
    }
}

static void test_common_blocks(TTestContext& ctx)
{
    //  Every section combination, with independent absence/supplied-zero bytes.
    for (unsigned mask = 0u; mask < 8u; ++mask)
    {
        std::string text = "{";
        if (mask & 1u) text += "\"types\":{},";
        if (mask & 2u) text += "\"instances\":{},";
        if (mask & 4u) text += "\"data\":{},";
        if (text.back() == ',') text.pop_back();
        text += "}";
        CLiveDocument source;
        TEST_EXPECT(ctx, tests::parse_document(text, source));
        CBakedDocumentBlock source_baked;
        TEST_EXPECT(ctx, document_translation::bake(source, source_baked));
        const CByteConstView empty;
        for (unsigned supplied = 0u; supplied < 4u; ++supplied)
        {
            CBakedDataBlock output;
            SDataBlockDiagnostic error;
            const bool valid = ((supplied & 1u) != 0u) == ((mask & 2u) != 0u) && ((supplied & 2u) != 0u) == ((mask & 4u) != 0u);
            const SDataBlockPayloads payloads{ (supplied & 1u) ? &empty : nullptr, (supplied & 2u) ? &empty : nullptr };
            const bool accepted = output.assemble(source, payloads, error);
            CBakedDataBlock baked_output;
            SDataBlockDiagnostic baked_error;
            TEST_EXPECT(ctx, baked_output.assemble(source_baked.document(), payloads, baked_error) == valid);
            TEST_EXPECT(ctx, accepted == valid);
            if (!accepted)
            {
                TEST_EXPECT(ctx, !output.loaded_ready() && error.reason != EDataBlockReason::none);
                continue;
            }
            CBakedDataBlock previous;
            TEST_EXPECT(ctx, assemble_with_previous_staging(source, payloads, previous, error));
            TEST_EXPECT(ctx, tests::same_bytes(output.file_bytes(), previous.file_bytes()) &&
                tests::same_bytes(output.file_bytes(), baked_output.file_bytes()));
            TEST_EXPECT(ctx, output.has_types() == ((mask & 1u) != 0u) &&
                output.has_instances() == ((mask & 2u) != 0u) && output.has_bulk_data() == ((mask & 4u) != 0u));
            TEST_EXPECT(ctx, output.instance_binary_available() == ((supplied & 1u) != 0u) &&
                output.bulk_binary_available() == ((supplied & 2u) != 0u));
            TEST_EXPECT(ctx, output.file_bytes().size() == output.document().byte_count());
        }
        CSchema schema;
        SSchemaDiagnostic diagnostic;
        CBakedDataBlock materialised;
        TEST_EXPECT(ctx, schema.copy_from(CSchemaDocumentQuery{ source }, diagnostic));
        TEST_EXPECT(ctx, schema.bake(materialised, diagnostic) == ((mask & 1u) != 0u));
    }

    CSchema aggregate;
    CLiveDocument input;
    SSchemaDiagnostic build_error;
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{"structures":{"Value":{"members":[{"n":{"type":"u8","default":2}}]}}},
        "instances":{"Value":{"answer":{"declaration":{"n":7}}}},"data":{"u32":{"code":{"data":[1,2,3]}}}})", input) &&
        aggregate.try_adopt(std::move(input), build_error));
    CBakedDataBlock full, restored;
    TEST_EXPECT(ctx, aggregate.bake(full, build_error));
    const auto expected = byte_snapshot(full.file_bytes());
    TEST_EXPECT(ctx, !aggregate.bake(full, build_error) && tests::same_bytes(full.file_bytes(), expected));
    CBakedTypes types;
    SDiagnostic types_error;
    TEST_EXPECT(ctx, types.set_block(full) && types.resolve(types_error));
    CBakedInstances instances;
    CBakedBulkData bulk;
    SInstanceDiagnostic instance_error;
    SBulkDataDiagnostic bulk_error;
    TEST_EXPECT(ctx, instances.attach_block(full, types, instance_error) && bulk.attach_block(full, types, bulk_error));
    TEST_EXPECT(ctx, full.instance_payload_view().size() == 1u && full.instance_payload_view().data()[0] == 7u && full.bulk_payload_view().size() == 12u);
    instances.clear();
    bulk.clear();
    const auto first = full.instance_payload_view(), second = full.bulk_payload_view();
    SDataBlockDiagnostic storage_error;
    TEST_EXPECT(ctx, !restored.assemble(full.document(), { &first, nullptr }, storage_error) && !restored.loaded_ready());
    TEST_EXPECT(ctx, restored.assemble(full.document(), { &first, &second }, storage_error));
    TEST_EXPECT(ctx, tests::same_bytes(restored.instance_payload_view(), first) && tests::same_bytes(restored.bulk_payload_view(), second));
    aggregate.clear();
    TEST_EXPECT(ctx, tests::same_bytes(full.file_bytes(), expected));

    // Both alignment gaps are validated; allocation capacity is not file length.
    CBakedDocumentBlock owned;
    TEST_EXPECT(ctx, restored.release_local(owned));
    auto bytes = owned.buffer_bytes();
    SDataBlockLayout layout;
    EDataBlockReason reason;
    TEST_EXPECT(ctx, data_block_layout(owned.document().byte_count(), 1u, 12u, layout, reason));
    for (const std::size_t position : { owned.document().byte_count(), layout.instance_offset + 1u })
    {
        const std::size_t limit = position == owned.document().byte_count() ? layout.instance_offset : layout.bulk_offset;
        if (position == limit) continue;
        bytes.data()[position] = 1u;
        CBakedDataBlock rejected;
        TEST_EXPECT(ctx, !rejected.adopt_local(std::move(owned), storage_error) &&
            storage_error.reason == EDataBlockReason::nonzero_padding && storage_error.location.byte_offset == position);
        TEST_EXPECT(ctx, owned.is_ready() && !rejected.loaded_ready());
        bytes.data()[position] = 0u;
    }
    TEST_EXPECT(ctx, restored.adopt_local(std::move(owned), storage_error));
    TEST_EXPECT(ctx, !data_block_layout(128u, memory::k_byte_size_ceiling - 128u, 1u, layout, reason) && reason == EDataBlockReason::storage_limit);
    TEST_EXPECT(ctx, !data_block_layout(128u, 1u, UINT64_MAX, layout, reason) && reason == EDataBlockReason::size_overflow);
}

}   // namespace

void run_schema_data_block_cases(tests::TTestContext& context)
{
    test_assembly_staging(context);
    test_assembly_diagnostics(context);
    test_common_blocks(context);
    test_common_block_roles(context);
    test_valid(context);
    test_metadata(context);
    test_bounds(context);
    test_layout(context);
    test_blocks(context);
    test_completed_bakes(context);
}
