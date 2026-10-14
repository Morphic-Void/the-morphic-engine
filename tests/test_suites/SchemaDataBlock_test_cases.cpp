//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SchemaDataBlock_test_cases.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Persistence envelopes assembled without a schema block writer.

#include "tests/test_suites/SchemaDataBlock_test_cases.hpp"
#include "schema/data_block_format.hpp"
#include "schema/baked_instances_block.hpp"
#include "schema/baked_bulk_data_block.hpp"
#include "system/transported_types.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"

#include <cstring>
#include <limits>
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
static_assert(std::is_same_v<decltype(std::declval<CBakedInstancesBlock&>().instances()), const CBakedInstances&>);
static_assert(std::is_same_v<decltype(std::declval<CBakedBulkDataBlock&>().bulk_data()), const CBakedBulkData&>);

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

static void expect_failure(TTestContext& ctx, const void* bytes, const std::size_t size,
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
    TEST_EXPECT(ctx, !SDataBlockLoadOptions{}.compare_embedded);
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

    bool build()
    {
        const std::string json = R"({"types":{"structures":{"Zero":{"members":[]}}}})";
        CLiveDocument document;
        SDiagnostic diagnostic;
        return document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(json.data()), json.size(), 1u }, document).accepted() &&
            live.try_adopt(std::move(document)) && live.resolve(diagnostic) && live.bake(storage, baked) && baked.resolve(diagnostic);
    }
};

template<bool Instances>
struct SBlockTraits
{
    using Block = std::conditional_t<Instances, CBakedInstancesBlock, CBakedBulkDataBlock>;
    using Diagnostic = std::conditional_t<Instances, SInstancesBlockDiagnostic, SBulkDataBlockDiagnostic>;
    using Reason = std::conditional_t<Instances, EInstanceLoadReason, EBulkDataLoadReason>;
    static constexpr EDataBlockKind kind = Instances ? EDataBlockKind::instances : EDataBlockKind::bulk_data;

    static const auto& role(const Block& block)
    {
        if constexpr (Instances) return block.instances();
        else return block.bulk_data();
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

    static void check_entry(TTestContext& ctx, const Block& block, const std::uint8_t* payload)
    {
        if constexpr (Instances)
        {
            SInstanceEntryView entry;
            const auto handle = block.instances().find_base(CStringView{ "u8" }, CStringView{ "one" });
            TEST_EXPECT(ctx, handle && block.instances().entry(handle, entry));
            TEST_EXPECT(ctx, entry.offset == 2u && entry.byte_count == 1u && entry.bytes == payload + 2u);
        }
        else
        {
            SBulkDataEntryView entry;
            const auto handle = block.bulk_data().find_entry(CStringView{ "u8" }, CStringView{ "one" });
            TEST_EXPECT(ctx, handle && block.bulk_data().entry(handle, entry));
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
        block.file_bytes().data() == nullptr && !T::role(block).document_ready());
    CBakedDocumentBlock extracted;
    TEST_EXPECT(ctx, !block.release_local(extracted));
    seed(diagnostic.block);
    diagnostic.role.reason = T::Reason::unknown_type;
    TEST_EXPECT(ctx, block.adopt_local(std::move(source), schema, { true }, diagnostic));
    TEST_EXPECT(ctx, diagnostic.block.reason == EDataBlockReason::none && diagnostic.role.reason == T::Reason::none &&
        diagnostic.block.location.kind == EDataBlockLocationKind::none);
    TEST_EXPECT(ctx, !source.is_ready() && source.buffer_capacity() == 0u);
    TEST_EXPECT(ctx, schema.reference_count() == 1u && block.loaded_ready() && block.backing() == EEntityBacking::local && !block.asset_id());
    TEST_EXPECT(ctx, block.file_bytes().data() == original && block.file_bytes().size() == file_size);
    T::check_entry(ctx, block, original + fixture.payload_offset);

    SFixture replacement;
    TEST_EXPECT(ctx, replacement.build(envelope(T::kind)));
    TEST_EXPECT(ctx, source.adopt_buffer(std::move(replacement.file)));
    const auto* const other = source.buffer_bytes().data();
    TEST_EXPECT(ctx, !block.adopt_local(std::move(source), schema, {}, diagnostic));
    TEST_EXPECT(ctx, diagnostic.block.stage == EDataBlockStage::preconditions &&
        diagnostic.block.reason == EDataBlockReason::destination_not_empty && diagnostic.role.reason == T::Reason::none);
    TEST_EXPECT(ctx, source.buffer_bytes().data() == other && block.file_bytes().data() == original && schema.reference_count() == 1u);
    TEST_EXPECT(ctx, !block.attach_asset({}, {}, schema, {}, diagnostic) &&
        diagnostic.block.reason == EDataBlockReason::destination_not_empty);
    TEST_EXPECT(ctx, !block.release_local(source) && source.buffer_bytes().data() == other && block.loaded_ready());
    TEST_EXPECT(ctx, block.release_local(extracted));
    TEST_EXPECT(ctx, schema.reference_count() == 0u && block.backing() == EEntityBacking::none &&
        !T::role(block).document_ready() && !block.file_bytes().is_ready());
    TEST_EXPECT(ctx, extracted.buffer_bytes().data() == original && extracted.buffer_bytes().size() == file_size &&
        std::memcmp(original, snapshot.data(), file_size) == 0);
    TEST_EXPECT(ctx, block.adopt_local(std::move(extracted), schema, {}, diagnostic));
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
    TEST_EXPECT(ctx, !block.attach_asset({}, borrowed, schema, {}, diagnostic) &&
        diagnostic.block.reason == EDataBlockReason::invalid_input && block.backing() == EEntityBacking::none);
    TEST_EXPECT(ctx, !block.attach_asset(id, {}, schema, {}, diagnostic) && assets.resolve(id) != nullptr &&
        schema.reference_count() == 0u);
    TEST_EXPECT(ctx, block.attach_asset(id, borrowed, schema, {}, diagnostic));
    TEST_EXPECT(ctx, block.backing() == EEntityBacking::asset && block.asset_id() == id && block.loaded_ready() &&
        block.file_bytes().data() == other && schema.reference_count() == 1u);
    TEST_EXPECT(ctx, !block.release_local(extracted) && !extracted.is_ready());
    block.clear();
    TEST_EXPECT(ctx, !block.asset_id() && !block.file_bytes().is_ready() && schema.reference_count() == 0u && assets.resolve(id) != nullptr);
    {
        typename T::Block temporary;
        TEST_EXPECT(ctx, temporary.attach_asset(id, borrowed, schema, {}, diagnostic));
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
        TEST_EXPECT(ctx, !block.adopt_local(std::move(source), schema, { item.compare }, diagnostic));
        TEST_EXPECT(ctx, block.backing() == EEntityBacking::none && !T::role(block).document_ready() && !block.loaded_ready());
        TEST_EXPECT(ctx, source.buffer_bytes().data() == original && schema.reference_count() == 0u &&
            std::memcmp(original, snapshot.data(), snapshot.size()) == 0);
        TEST_EXPECT(ctx, diagnostic.block.stage == EDataBlockStage::specialised_loading &&
            diagnostic.block.reason == EDataBlockReason::specialised_failure && diagnostic.role.reason == item.reason);
        //  Temporary preparation is gone; both locations still address source bytes.
        const auto query = std::conditional_t<Instances, CInstanceDocumentQuery, CBulkDataDocumentQuery>{ source.document() };
        TEST_EXPECT(ctx, query.contains(diagnostic.role.occurrence) &&
            diagnostic.block.location.kind == EDataBlockLocationKind::source_baked_value);
        TEST_EXPECT(ctx, source.document().value_type(diagnostic.block.location.baked_value) != EBakedValueType::invalid);
        if (item.compare)
        {
            TEST_EXPECT(ctx, block.adopt_local(std::move(source), schema, {}, diagnostic));
            TEST_EXPECT(ctx, block.loaded_ready() && diagnostic.role.reason == T::Reason::none);
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
                TEST_EXPECT(ctx, block.adopt_local(std::move(source), schema, { true }, diagnostic));
                TEST_EXPECT(ctx, block.loaded_ready() && T::role(block).values_stripped() == stripped &&
                    T::role(block).payload_view().size() == 0u && schema.reference_count() == 1u);
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
    TEST_EXPECT(ctx, !block.adopt_local(std::move(source), schema, {}, diagnostic));
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
    {
        SSchemaFixture schemas;
        auto& schema = [&]() -> auto& { if constexpr (Baked) return schemas.baked; else return schemas.live; }();
        TEST_EXPECT(ctx, !block.adopt_local(std::move(source), schema, {}, diagnostic));
        TEST_EXPECT(ctx, source.is_ready() && block.backing() == EEntityBacking::none &&
            diagnostic.block.stage == EDataBlockStage::schema_binding &&
            diagnostic.block.reason == EDataBlockReason::schema_unavailable && diagnostic.role.reason == T::Reason::none);
        TEST_EXPECT(ctx, schemas.build());
        TEST_EXPECT(ctx, block.adopt_local(std::move(source), schema, {}, diagnostic));
        TEST_EXPECT(ctx, schema.reference_count() == 1u);
    }
    TEST_EXPECT(ctx, !block.loaded_ready() && block.backing() == EEntityBacking::local && block.file_bytes().data() == original);
    SSchemaFixture replacement;
    TEST_EXPECT(ctx, replacement.build());
    TEST_EXPECT(ctx, !block.adopt_local(std::move(source), replacement.live, {}, diagnostic) &&
        diagnostic.block.reason == EDataBlockReason::destination_not_empty && replacement.live.reference_count() == 0u);
    TEST_EXPECT(ctx, block.release_local(source) && source.buffer_bytes().data() == original);
    TEST_EXPECT(ctx, block.adopt_local(std::move(source), replacement.baked, {}, diagnostic));
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
        TEST_EXPECT(ctx, block.attach_asset(id, bytes, schema, {}, diagnostic));
        T::check_entry(ctx, block, original + fixture.payload_offset);
    }
    TEST_EXPECT(ctx, !block.loaded_ready() && block.backing() == EEntityBacking::asset &&
        block.asset_id() == id && block.file_bytes().data() == original && block.file_bytes().size() == bytes.size());
    TEST_EXPECT(ctx, !block.release_local(source) && !source.is_ready());
    TEST_EXPECT(ctx, !block.attach_asset(id, bytes, replacement.live, {}, diagnostic) &&
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

static bool MV_STD_ABI_CALL deallocate_observed_block(void* state, const std::size_t alignment, void* pointer) noexcept
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
            TEST_EXPECT(ctx, block.adopt_local(std::move(source), schemas.baked, {}, diagnostic));
            TEST_EXPECT(ctx, schemas.baked.reference_count() == 1u && !observer.released);
        }
        TEST_EXPECT(ctx, observer.released && observer.unbound_before_release);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

struct SAllocationFailures
{
    std::size_t calls{}, fail_on{};
};

static void* MV_STD_ABI_CALL allocate_block_test(void* state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    auto& failures = *static_cast<SAllocationFailures*>(state);
    if (failures.calls++ == failures.fail_on) return nullptr;
    return tests::allocate_test_memory(nullptr, alignment, bytes);
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
    bool role_failure{};
    //  Fail each preparation allocation in turn, including opt-in comparison.
    for (std::size_t fail_on = 0u; fail_on != 128u && !completed; ++fail_on)
    {
        SAllocationFailures failures{ 0u, fail_on };
        memory::CMemoryAllocator allocator{ &failures, &allocate_block_test, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            const tests::TMemoryContextScope scope{ &context };
            typename T::Block block;
            typename T::Diagnostic diagnostic;
            completed = block.adopt_local(std::move(source), schemas.baked, { true }, diagnostic);
            if (completed)
            {
                TEST_EXPECT(ctx, block.release_local(source));
            }
            else
            {
                TEST_EXPECT(ctx, block.backing() == EEntityBacking::none && !T::role(block).document_ready());
                TEST_EXPECT(ctx, diagnostic.block.reason == EDataBlockReason::invalid_document ||
                    (diagnostic.block.reason == EDataBlockReason::specialised_failure && diagnostic.role.reason == T::Reason::allocation_failed));
                role_failure |= diagnostic.role.reason == T::Reason::allocation_failed;
            }
            TEST_EXPECT(ctx, source.buffer_bytes().data() == original && schemas.baked.reference_count() == 0u);
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, completed && role_failure);
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
}   // namespace

void run_schema_data_block_cases(tests::TTestContext& context)
{
    test_valid(context);
    test_metadata(context);
    test_bounds(context);
    test_layout(context);
    test_blocks(context);
}
