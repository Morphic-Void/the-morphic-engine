//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    BakedDocument_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Date:    7 Sep 26

#include "tests/test_suites/BakedDocument_test_suite.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include "containers/ByteBuffers.hpp"
#include "data_model/baked_document.hpp"
#include "data_model/baked_document_format.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/live_document.hpp"
#include "memory/memory_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"

namespace
{

using TTestContext = tests::TTestContext;

constexpr std::uint32_t k_value_count = 8u;
constexpr std::uint32_t k_property_reference_count = 7u;
constexpr std::uint32_t k_string_reference_count = 2u;
constexpr std::uint32_t k_values_offset = 64u;
constexpr std::uint32_t k_property_references_offset = k_values_offset + (k_value_count * 32u);
constexpr std::uint32_t k_string_references_offset = k_property_references_offset + (k_property_reference_count * 8u);
constexpr std::uint32_t k_property_bytes_offset = k_string_references_offset + (k_string_reference_count * 8u);
constexpr std::uint32_t k_property_byte_count = 13u;
constexpr std::uint32_t k_string_bytes_offset = k_property_bytes_offset + k_property_byte_count;
constexpr std::uint32_t k_string_byte_count = 6u;
constexpr std::uint32_t k_total_size = k_string_bytes_offset + k_string_byte_count;
constexpr std::uint32_t k_root_only_size = 114u;

[[nodiscard]] CStringView text(const char* const value) noexcept
{
    return CStringView{
        reinterpret_cast<const std::uint8_t*>(value),
        std::strlen(value) };
}

[[nodiscard]] bool initialise_root_only(CByteBuffer& bytes) noexcept
{
    if (!bytes.resize(k_root_only_size, baked_document_format::k_block_alignment))
    {
        return false;
    }
    bytes.zero_fill();
    SBakedDocumentHeader& header = *reinterpret_cast<SBakedDocumentHeader*>(bytes.data());
    header.magic = baked_document_format::k_magic;
    header.version = baked_document_format::k_version;
    header.header_size = baked_document_format::k_header_size;
    header.total_size = k_root_only_size;
    header.value_count = 1u;
    header.property_name_reference_count = 1u;
    header.property_name_byte_count = 1u;
    header.string_value_reference_count = 1u;
    header.string_value_byte_count = 1u;
    header.values_offset = 64u;
    header.property_name_references_offset = 96u;
    header.string_value_references_offset = 104u;
    header.property_name_bytes_offset = 112u;
    header.string_value_bytes_offset = 113u;
    SBakedValueRecord& root = *reinterpret_cast<SBakedValueRecord*>(bytes.data() + sizeof(SBakedDocumentHeader));
    root.parent_index = baked_document_format::k_invalid_index;
    root.first_child_index = baked_document_format::k_invalid_index;
    root.value_type = EBakedValueType::object;
    return true;
}

struct SBakedFixture
{
    CByteBuffer bytes;

    [[nodiscard]] bool initialise() noexcept
    {
        if (!bytes.resize(k_total_size, baked_document_format::k_block_alignment))
        {
            return false;
        }
        bytes.zero_fill();

        SBakedDocumentHeader& header = *reinterpret_cast<SBakedDocumentHeader*>(bytes.data());
        header.magic = baked_document_format::k_magic;
        header.version = baked_document_format::k_version;
        header.header_size = baked_document_format::k_header_size;
        header.total_size = k_total_size;
        header.value_count = k_value_count;
        header.property_name_reference_count = k_property_reference_count;
        header.property_name_byte_count = k_property_byte_count;
        header.string_value_reference_count = k_string_reference_count;
        header.string_value_byte_count = k_string_byte_count;
        header.values_offset = k_values_offset;
        header.property_name_references_offset = k_property_references_offset;
        header.string_value_references_offset = k_string_references_offset;
        header.property_name_bytes_offset = k_property_bytes_offset;
        header.string_value_bytes_offset = k_string_bytes_offset;

        SBakedValueRecord* const records = values();
        for (std::uint32_t index = 0u; index < k_value_count; ++index)
        {
            records[index].parent_index = baked_document_format::k_invalid_index;
            records[index].first_child_index = baked_document_format::k_invalid_index;
        }

        records[0u].value_type = EBakedValueType::object;
        records[0u].first_child_index = 1u;
        records[0u].child_count = 6u;

        records[1u].value_type = EBakedValueType::null_value;
        records[2u].value_type = EBakedValueType::boolean;
        records[2u].payload_bits = 1u;
        records[3u].value_type = EBakedValueType::integer;
        records[3u].payload_bits = live_signed_integer_bits(127);
        records[4u].value_type = EBakedValueType::floating_point;
        records[4u].payload_bits = live_floating_point_bits(1.25);
        records[5u].value_type = EBakedValueType::string;
        records[5u].payload_bits = 1u;
        records[6u].value_type = EBakedValueType::array;
        records[6u].first_child_index = 7u;
        records[6u].child_count = 1u;
        records[7u].value_type = EBakedValueType::null_value;

        for (std::uint32_t index = 1u; index <= 6u; ++index)
        {
            records[index].parent_index = 0u;
            records[index].property_name_index = index;
            records[index].value_flags = document_value_flags::k_name_present_flag;
        }
        records[7u].parent_index = 6u;
        records[1u].value_flags |= document_value_flags::k_first_sibling_flag;
        records[6u].value_flags |= document_value_flags::k_last_sibling_flag;
        records[7u].value_flags |= document_value_flags::k_sibling_position_flags;

        SBakedStringReference* const property_references = references(k_property_references_offset);
        property_references[0u] = SBakedStringReference{ 0u, 0u };
        std::uint8_t* const property_bytes = bytes.data() + k_property_bytes_offset;
        property_bytes[0u] = 0u;
        for (std::uint32_t index = 1u; index < k_property_reference_count; ++index)
        {
            property_references[index] = SBakedStringReference{ (index * 2u) - 1u, 1u };
            property_bytes[(index * 2u) - 1u] = static_cast<std::uint8_t>('a' + index - 1u);
            property_bytes[index * 2u] = 0u;
        }

        SBakedStringReference* const string_references = references(k_string_references_offset);
        string_references[0u] = SBakedStringReference{ 0u, 0u };
        string_references[1u] = SBakedStringReference{ 1u, 4u };
        std::uint8_t* const string_bytes = bytes.data() + k_string_bytes_offset;
        string_bytes[0u] = 0u;
        std::memcpy(string_bytes + 1u, "text", 4u);
        string_bytes[5u] = 0u;
        return true;
    }

    [[nodiscard]] SBakedDocumentHeader& header() noexcept
    {
        return *reinterpret_cast<SBakedDocumentHeader*>(bytes.data());
    }

    [[nodiscard]] SBakedValueRecord* values() noexcept
    {
        return reinterpret_cast<SBakedValueRecord*>(bytes.data() + k_values_offset);
    }

    [[nodiscard]] SBakedStringReference* references(const std::uint32_t offset) noexcept
    {
        return reinterpret_cast<SBakedStringReference*>(bytes.data() + offset);
    }
};

template<typename TMutation>
void expect_rejected(TTestContext& ctx, TMutation&& mutation)
{
    SBakedFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    mutation(fixture);
    CBakedDocument document;
    TEST_EXPECT(ctx, !document.reset(fixture.bytes.data(), fixture.bytes.size()));
    TEST_EXPECT(ctx, !document.is_ready());
}

void test_fixed_layout_value_editing(TTestContext& ctx)
{
    static_assert(std::is_same_v<decltype(std::declval<CMutableBakedDocument&>().baked()), const CBakedDocument&>);
    static_assert(std::is_same_v<decltype(std::declval<const CMutableBakedDocument&>().baked()), const CBakedDocument&>);
    static_assert(!std::is_convertible_v<CMutableBakedDocument&, CBakedDocument&>);
    static_assert(!std::is_constructible_v<CMutableBakedDocument, const CBakedDocument&>);
    static_assert(!std::is_constructible_v<CMutableBakedDocument, const CBakedDocumentBlock&>);
    static_assert(!std::is_constructible_v<CMutableBakedDocument, const CByteConstView&>);
    static_assert(!std::is_invocable_v<decltype(&CMutableBakedDocument::reset), CMutableBakedDocument&, const CByteConstView&>);

    SBakedFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    fixture.values()[1u].value_type = EBakedValueType::string;
    fixture.values()[1u].payload_bits = 1u;
    CIntegerMetadata unsigned_metadata;
    unsigned_metadata.domain = EIntegerDomain::unsigned_value;
    unsigned_metadata.width = EIntegerWidth::bits_64;
    unsigned_metadata.notation = EIntegerNotation::hexadecimal;
    unsigned_metadata.prefix = EIntegerPrefix::alternate;
    fixture.values()[7u].value_type = EBakedValueType::integer;
    fixture.values()[7u].payload_bits = std::numeric_limits<std::uint64_t>::max();
    fixture.values()[7u].value_flags |= document_value_flags::encode_integer_metadata(unsigned_metadata);
    fixture.values()[5u].value_flags |= document_value_flags::k_suppress_newline_escaping_flag;

    CMutableBakedDocument editable{ fixture.bytes.view() };
    const CBakedDocument& document = editable.baked();
    TEST_EXPECT(ctx, document.is_ready() && editable.is_ready());
    if (!document.is_ready()) return;
    const CBakedValueIndex boolean = document.object_child(document.root(), text("b"));
    const CBakedValueIndex integer = document.object_child(document.root(), text("c"));
    const CBakedValueIndex floating = document.object_child(document.root(), text("d"));
    const CBakedValueIndex string = document.object_child(document.root(), text("e"));
    const CBakedValueIndex array = document.object_child(document.root(), text("f"));
    const CBakedValueIndex unsigned_integer = document.array_at(array, 0u);
    const CStringValueId empty_string = document.string_value_id_at_rank(0u);
    const CStringValueId original_string = document.string_value_id(string);
    const auto backing = fixture.bytes.data();
    const auto capacity = fixture.bytes.capacity();
    const std::vector<std::uint8_t> before(backing, backing + fixture.bytes.size());
    const CBakedDocument observer{ fixture.bytes.data(), fixture.bytes.size() };

    tests::TAllocatorFixture allocator_fixture{ true };
    memory::CMemoryAllocator allocator{ &allocator_fixture, &tests::allocate_test_memory, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        const tests::TMemoryContextScope scope{ &context };
        TEST_EXPECT(ctx, editable.set_boolean_value(boolean, false));
        TEST_EXPECT(ctx, editable.set_signed_integer_value(integer, -128));
        TEST_EXPECT(ctx, editable.set_unsigned_integer_value(unsigned_integer, std::uint64_t{ 1u } << 32u));
        TEST_EXPECT(ctx, editable.set_floating_point_value(floating, -2.5));
        TEST_EXPECT(ctx, editable.set_string_value(string, empty_string));
        TEST_EXPECT(ctx, document.string_value(string) == text(""));
        //  Reassignment is supported while another value retains the old string.
        TEST_EXPECT(ctx, editable.set_string_value(string, original_string));
        TEST_EXPECT(ctx, document.string_value(string) == text("text"));
        TEST_EXPECT(ctx, editable.set_string_value(string, empty_string));
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    bool boolean_result = true;
    std::int64_t signed_result = 0;
    std::uint64_t unsigned_result = 0u;
    double floating_result = 0.0;
    CIntegerMetadata metadata;
    TEST_EXPECT(ctx, observer.boolean_value(boolean, boolean_result) && !boolean_result);
    TEST_EXPECT(ctx, observer.signed_integer_value(integer, signed_result) && signed_result == -128);
    TEST_EXPECT(ctx, observer.unsigned_integer_value(unsigned_integer, unsigned_result) && unsigned_result == (std::uint64_t{ 1u } << 32u));
    TEST_EXPECT(ctx, observer.floating_point_value(floating, floating_result) && floating_result == -2.5);
    TEST_EXPECT(ctx, observer.integer_metadata(unsigned_integer, metadata) && metadata == unsigned_metadata);
    TEST_EXPECT(ctx, observer.suppresses_newline_escaping(string));
    TEST_EXPECT(ctx, observer.check_integrity());
    TEST_EXPECT(ctx, fixture.bytes.data() == backing && fixture.bytes.capacity() == capacity);
    TEST_EXPECT(ctx, fixture.bytes.size() == before.size());
    //  Only the five selected payloads may differ; this includes every header,
    //  link, type, name, formatting flag, string reference and stored text byte.
    for (std::size_t offset = 0u; offset < before.size(); ++offset)
    {
        bool editable_payload = false;
        for (const auto value : { boolean, integer, unsigned_integer, floating, string })
        {
            const std::size_t start = k_values_offset + value.query_value() * sizeof(SBakedValueRecord);
            editable_payload |= (offset >= start) && (offset < start + sizeof(std::uint64_t));
        }
        if (!editable_payload) TEST_EXPECT(ctx, backing[offset] == before[offset]);
    }

    const std::vector<std::uint8_t> edited(backing, backing + fixture.bytes.size());
    const auto expect_unchanged = [&]
    {
        TEST_EXPECT(ctx, std::memcmp(backing, edited.data(), edited.size()) == 0);
    };
    const auto reject = [&](const bool result)
    {
        TEST_EXPECT(ctx, !result);
        expect_unchanged();
    };
    reject(editable.set_signed_integer_value(integer, 128));
    reject(editable.set_signed_integer_value(integer, -129));
    reject(editable.set_unsigned_integer_value(integer, 1u));
    reject(editable.set_signed_integer_value(unsigned_integer, 1));
    reject(editable.set_unsigned_integer_value(unsigned_integer, 1u)); // Would narrow the metadata.
    reject(editable.set_floating_point_value(floating, std::numeric_limits<double>::infinity()));
    reject(editable.set_floating_point_value(floating, std::numeric_limits<double>::quiet_NaN()));
    reject(editable.set_string_value(string, CStringValueId{}));
    reject(editable.set_string_value(document.object_child(document.root(), text("a")), empty_string));
    //  Valid IDs are document-local: an out-of-table ID from another document is rejected.
    CLiveDocument other;
    TEST_EXPECT(ctx, other.initialise());
    const CNodeKey other_a = other.create_string(text("a"));
    const CNodeKey other_b = other.create_string(text("b"));
    TEST_EXPECT(ctx, other_a.is_valid() && other_b.is_valid());
    reject(editable.set_string_value(string, other.string_value_id(other_b)));
    for (const auto target : { CBakedValueIndex{}, document.root(), array })
    {
        reject(editable.set_boolean_value(target, true));
        reject(editable.set_signed_integer_value(target, 1));
        reject(editable.set_unsigned_integer_value(target, 1u));
        reject(editable.set_floating_point_value(target, 1.0));
        reject(editable.set_string_value(target, original_string));
    }
    reject(editable.set_boolean_value(integer, true));
    reject(editable.set_floating_point_value(integer, 1.0));
    reject(editable.set_string_value(boolean, original_string));

    const auto reject_unattached = [&](const CMutableBakedDocument& view)
    {
        reject(view.set_boolean_value(boolean, true));
        reject(view.set_signed_integer_value(integer, 1));
        reject(view.set_unsigned_integer_value(unsigned_integer, std::numeric_limits<std::uint64_t>::max()));
        reject(view.set_floating_point_value(floating, 3.0));
        reject(view.set_string_value(string, original_string));
    };
    reject_unattached(CMutableBakedDocument{});
    CMutableBakedDocument copied = editable;
    TEST_EXPECT(ctx, copied.is_ready());
    TEST_EXPECT(ctx, copied.set_boolean_value(boolean, true));
    TEST_EXPECT(ctx, copied.set_boolean_value(boolean, false));
    //  A copied reading view can be rebound without changing the editor's target.
    SBakedFixture other_fixture;
    TEST_EXPECT(ctx, other_fixture.initialise());
    const std::vector<std::uint8_t> other_before(other_fixture.bytes.data(), other_fixture.bytes.data() + other_fixture.bytes.size());
    CBakedDocument reading_copy = editable.baked();
    TEST_EXPECT(ctx, reading_copy.reset(other_fixture.bytes.data(), other_fixture.bytes.size()));
    TEST_EXPECT(ctx, editable.set_boolean_value(boolean, true));
    TEST_EXPECT(ctx, document.boolean_value(boolean, boolean_result) && boolean_result);
    TEST_EXPECT(ctx, editable.set_boolean_value(boolean, false));
    TEST_EXPECT(ctx, reading_copy.boolean_value(reading_copy.object_child(reading_copy.root(), text("b")), boolean_result) && boolean_result);
    TEST_EXPECT(ctx, std::memcmp(other_before.data(), other_fixture.bytes.data(), other_before.size()) == 0);
    expect_unchanged();
    TEST_EXPECT(ctx, editable.reset(fixture.bytes.view()));
    TEST_EXPECT(ctx, editable.is_ready());
    editable.clear();
    TEST_EXPECT(ctx, !document.is_ready() && !editable.is_ready());
    reject_unattached(editable);
    TEST_EXPECT(ctx, editable.reset(fixture.bytes.view()));
    TEST_EXPECT(ctx, !editable.reset(CByteView{}));
    TEST_EXPECT(ctx, !document.is_ready() && !editable.is_ready());
    reject_unattached(editable);

    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, block.adopt(std::move(fixture.bytes)));
    CMutableBakedDocument writable{ block };
    TEST_EXPECT(ctx, writable.is_ready());
    TEST_EXPECT(ctx, writable.set_boolean_value(boolean, true));
    TEST_EXPECT(ctx, writable.set_boolean_value(boolean, false));
    const CBakedDocument constant = static_cast<const CBakedDocumentBlock&>(block).document();
    TEST_EXPECT(ctx, constant.check_integrity());
    CBakedDocumentBlock empty_block;
    CMutableBakedDocument empty{ empty_block };
    TEST_EXPECT(ctx, !empty.is_ready());
    reject_unattached(empty);

    //  A malformed mutable attachment must also clear a previously writable editor.
    TEST_EXPECT(ctx, copied.reset(other_fixture.bytes.view()));
    other_fixture.header().magic = 0u;
    TEST_EXPECT(ctx, !copied.reset(other_fixture.bytes.view()));
    TEST_EXPECT(ctx, !copied.baked().is_ready() && !copied.is_ready());
    reject_unattached(copied);
}

void test_existing_string_reassignment(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    TEST_EXPECT(ctx, live.set_root_type(ELiveValueType::array));
    for (const char* value : { "first", "first", "second" })
    {
        TEST_EXPECT(ctx, live.append_child(live.root(), live.create_string(text(value))).succeeded());
    }
    TEST_EXPECT(ctx, live.append_child(live.root(), live.create_null()).succeeded());
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    const CMutableBakedDocument editable{ block };
    const CBakedDocument& document = editable.baked();
    const auto first = document.array_at(document.root(), 0u);
    const auto duplicate = document.array_at(document.root(), 1u);
    const auto second = document.array_at(document.root(), 2u);
    const auto null = document.array_at(document.root(), 3u);
    const auto first_id = document.string_value_id(first);
    const auto second_id = document.string_value_id(second);
    TEST_EXPECT(ctx, editable.set_string_value(first, second_id));
    TEST_EXPECT(ctx, document.string_value(first) == text("second"));
    TEST_EXPECT(ctx, document.string_value(duplicate) == text("first"));
    TEST_EXPECT(ctx, document.check_integrity());
    const std::vector<std::uint8_t> edited(block.bytes().data(), block.bytes().data() + block.bytes().size());
    TEST_EXPECT(ctx, !editable.set_string_value(duplicate, second_id)); // Last reference to "first".
    TEST_EXPECT(ctx, !editable.set_boolean_value(null, true));
    TEST_EXPECT(ctx, !editable.set_signed_integer_value(null, 1));
    TEST_EXPECT(ctx, !editable.set_unsigned_integer_value(null, 1u));
    TEST_EXPECT(ctx, !editable.set_floating_point_value(null, 1.0));
    TEST_EXPECT(ctx, !editable.set_string_value(null, first_id));
    TEST_EXPECT(ctx, std::memcmp(edited.data(), block.bytes().data(), edited.size()) == 0);
    TEST_EXPECT(ctx, editable.set_string_value(duplicate, first_id)); // Same ID, even for a sole reference.
    TEST_EXPECT(ctx, editable.set_string_value(first, first_id));
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_checked_binding_and_foundational_observations(TTestContext& ctx)
{
    static_assert(std::is_nothrow_copy_constructible_v<CBakedDocument>);
    static_assert(std::is_nothrow_copy_assignable_v<CBakedDocument>);

    CBakedDocument unavailable;
    TEST_EXPECT(ctx, !unavailable.is_ready());
    TEST_EXPECT(ctx, !unavailable.check_integrity());
    TEST_EXPECT(ctx, !unavailable.root().is_valid());
    TEST_EXPECT(ctx, unavailable.byte_count() == 0u);
    TEST_EXPECT(ctx, unavailable.value_count() == 0u);
    TEST_EXPECT(ctx, unavailable.property_name_count() == 0u);
    TEST_EXPECT(ctx, unavailable.string_value_count() == 0u);

    SBakedFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    TEST_EXPECT(ctx,
        (reinterpret_cast<std::uintptr_t>(fixture.bytes.data()) &
            (baked_document_format::k_block_alignment - 1u)) == 0u);
    CBakedDocument document{ fixture.bytes.data(), fixture.bytes.size() };
    TEST_EXPECT(ctx, document.is_ready());
    TEST_EXPECT(ctx, document.check_integrity());
    TEST_EXPECT(ctx, document.byte_count() == k_total_size);
    TEST_EXPECT(ctx, document.root().is_valid() && (document.root().query_value() == 0u));
    TEST_EXPECT(ctx, document.value_count() == k_value_count);
    TEST_EXPECT(ctx, document.property_name_count() == 6u);
    TEST_EXPECT(ctx, document.string_value_count() == 1u);

    const CPropertyNameId empty_name = document.property_name_id_at_rank(0u);
    const CPropertyNameId first_name = document.property_name_id_at_rank(1u);
    const CStringValueId empty_string = document.string_value_id_at_rank(0u);
    const CStringValueId first_string = document.string_value_id_at_rank(1u);
    TEST_EXPECT(ctx, empty_name.is_empty());
    TEST_EXPECT(ctx, first_name.is_valid() && (first_name.query_value() == 1u));
    TEST_EXPECT(ctx, empty_string.is_empty());
    TEST_EXPECT(ctx, first_string.is_valid() && (first_string.query_value() == 1u));
    TEST_EXPECT(ctx, !document.property_name_id_at_rank(7u).is_valid());
    TEST_EXPECT(ctx, !document.string_value_id_at_rank(2u).is_valid());
    TEST_EXPECT(ctx, document.property_name(empty_name).length() == 0u);
    TEST_EXPECT(ctx, document.property_name(empty_name).string() != nullptr);
    const CStringView expected_name{ reinterpret_cast<const std::uint8_t*>("a"), 1u };
    const CStringView expected_string{ reinterpret_cast<const std::uint8_t*>("text"), 4u };
    TEST_EXPECT(ctx, document.property_name(first_name) == expected_name);
    TEST_EXPECT(ctx, document.string_value(first_string) == expected_string);

    const CBakedDocument copy = document;
    TEST_EXPECT(ctx, copy.is_ready() && copy.check_integrity());
    document.clear();
    TEST_EXPECT(ctx, !document.is_ready());
    TEST_EXPECT(ctx, copy.is_ready());

    CByteBuffer root_only_bytes;
    TEST_EXPECT(ctx, initialise_root_only(root_only_bytes));
    CBakedDocument root_only{ root_only_bytes.data(), root_only_bytes.size() };
    TEST_EXPECT(ctx, root_only.is_ready() && root_only.check_integrity());
    TEST_EXPECT(ctx, root_only.value_count() == 1u);
    TEST_EXPECT(ctx, root_only.property_name_count() == 0u);
    TEST_EXPECT(ctx, root_only.string_value_count() == 0u);

    TEST_EXPECT(ctx, document.reset(fixture.bytes.data(), fixture.bytes.size()));
    TEST_EXPECT(ctx, document.is_ready());
    TEST_EXPECT(ctx, !document.reset(nullptr, 0u));
    TEST_EXPECT(ctx, !document.is_ready());

    CBakedDocument corrupted{ fixture.bytes.data(), fixture.bytes.size() };
    TEST_EXPECT(ctx, corrupted.is_ready());
    fixture.values()[1u].reserved_32 = 1u;
    TEST_EXPECT(ctx, !corrupted.check_integrity());
}

void test_query_surface(TTestContext& ctx)
{
    CBakedDocument unavailable;
    const CBakedValueIndex invalid;
    TEST_EXPECT(ctx, !unavailable.contains(invalid));
    TEST_EXPECT(ctx, unavailable.value_type(invalid) == EBakedValueType::invalid);
    TEST_EXPECT(ctx, !unavailable.is_object_entry(invalid));
    TEST_EXPECT(ctx, !unavailable.name_id(invalid).is_valid());
    TEST_EXPECT(ctx, unavailable.name(invalid).string() == nullptr);
    TEST_EXPECT(ctx, !unavailable.parent(invalid).is_valid());
    TEST_EXPECT(ctx, !unavailable.previous_sibling(invalid).is_valid());
    TEST_EXPECT(ctx, !unavailable.next_sibling(invalid).is_valid());
    TEST_EXPECT(ctx, unavailable.child_count(invalid) == 0u);
    TEST_EXPECT(ctx, !unavailable.first_child(invalid).is_valid());
    TEST_EXPECT(ctx, !unavailable.last_child(invalid).is_valid());
    TEST_EXPECT(ctx, !unavailable.array_at(invalid, 0u).is_valid());
    TEST_EXPECT(ctx, !unavailable.object_child(invalid, CPropertyNameId{}).is_valid());

    bool boolean_result = true;
    std::int64_t signed_result = -1;
    std::uint64_t unsigned_result = 1u;
    double floating_result = -1.0;
    CIntegerMetadata metadata{
        EIntegerDomain::unsigned_value,
        EIntegerWidth::bits_64,
        EIntegerNotation::binary,
        EIntegerPrefix::standard };
    TEST_EXPECT(ctx, !unavailable.boolean_value(invalid, boolean_result) && boolean_result);
    TEST_EXPECT(ctx, !unavailable.signed_integer_value(invalid, signed_result) && (signed_result == -1));
    TEST_EXPECT(ctx, !unavailable.unsigned_integer_value(invalid, unsigned_result) && (unsigned_result == 1u));
    TEST_EXPECT(ctx, !unavailable.integer_metadata(invalid, metadata));
    TEST_EXPECT(ctx, metadata.domain == EIntegerDomain::unsigned_value);
    TEST_EXPECT(ctx, !unavailable.floating_point_value(invalid, floating_result) && (floating_result == -1.0));
    TEST_EXPECT(ctx, !unavailable.string_value_id(invalid).is_valid());
    TEST_EXPECT(ctx, unavailable.string_value(invalid).string() == nullptr);

    SBakedFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    CBakedDocument document{ fixture.bytes.data(), fixture.bytes.size() };
    TEST_EXPECT(ctx, document.is_ready());

    const CBakedValueIndex root = document.root();
    const CBakedValueIndex null_value = document.first_child(root);
    const CBakedValueIndex boolean_value = document.next_sibling(null_value);
    const CBakedValueIndex integer_value = document.next_sibling(boolean_value);
    const CBakedValueIndex floating_value = document.next_sibling(integer_value);
    const CBakedValueIndex string_value = document.next_sibling(floating_value);
    const CBakedValueIndex nested_array = document.next_sibling(string_value);
    const CBakedValueIndex nested_child = document.first_child(nested_array);

    TEST_EXPECT(ctx, document.contains(root));
    TEST_EXPECT(ctx, document.contains(nested_child));
    TEST_EXPECT(ctx, !document.contains(invalid));
    TEST_EXPECT(ctx, document.value_type(root) == EBakedValueType::object);
    TEST_EXPECT(ctx, document.value_type(null_value) == EBakedValueType::null_value);
    TEST_EXPECT(ctx, document.value_type(boolean_value) == EBakedValueType::boolean);
    TEST_EXPECT(ctx, document.value_type(integer_value) == EBakedValueType::integer);
    TEST_EXPECT(ctx, document.value_type(floating_value) == EBakedValueType::floating_point);
    TEST_EXPECT(ctx, document.value_type(string_value) == EBakedValueType::string);
    TEST_EXPECT(ctx, document.value_type(nested_array) == EBakedValueType::array);
    TEST_EXPECT(ctx, document.value_type(nested_child) == EBakedValueType::null_value);
    TEST_EXPECT(ctx, !document.is_object_entry(root));
    TEST_EXPECT(ctx, document.is_object_entry(null_value));
    TEST_EXPECT(ctx, !document.is_object_entry(nested_child));
    TEST_EXPECT(ctx, document.name_id(root).is_empty());
    TEST_EXPECT(ctx, document.name_id(integer_value) == document.property_name_id_at_rank(3u));
    const CStringView expected_name{ reinterpret_cast<const std::uint8_t*>("c"), 1u };
    TEST_EXPECT(ctx, document.name(integer_value) == expected_name);

    TEST_EXPECT(ctx, !document.parent(root).is_valid());
    TEST_EXPECT(ctx, document.parent(null_value) == root);
    TEST_EXPECT(ctx, document.parent(nested_child) == nested_array);
    TEST_EXPECT(ctx, !document.previous_sibling(null_value).is_valid());
    TEST_EXPECT(ctx, document.next_sibling(null_value) == boolean_value);
    TEST_EXPECT(ctx, document.previous_sibling(nested_array) == string_value);
    TEST_EXPECT(ctx, !document.next_sibling(nested_array).is_valid());
    TEST_EXPECT(ctx, !document.previous_sibling(nested_child).is_valid());
    TEST_EXPECT(ctx, !document.next_sibling(nested_child).is_valid());
    TEST_EXPECT(ctx, document.child_count(root) == 6u);
    TEST_EXPECT(ctx, document.child_count(nested_array) == 1u);
    TEST_EXPECT(ctx, document.child_count(null_value) == 0u);
    TEST_EXPECT(ctx, document.first_child(root) == null_value);
    TEST_EXPECT(ctx, document.last_child(root) == nested_array);
    TEST_EXPECT(ctx, document.last_child(nested_array) == nested_child);
    TEST_EXPECT(ctx, !document.first_child(null_value).is_valid());
    TEST_EXPECT(ctx, document.array_at(nested_array, 0u) == nested_child);
    TEST_EXPECT(ctx, !document.array_at(nested_array, 1u).is_valid());
    TEST_EXPECT(ctx, !document.array_at(root, 0u).is_valid());

    TEST_EXPECT(ctx, document.object_child(root, document.property_name_id_at_rank(3u)) == integer_value);
    TEST_EXPECT(ctx, !document.object_child(root, CPropertyNameId{}).is_valid());
    TEST_EXPECT(ctx, !document.object_child(nested_array, document.property_name_id_at_rank(3u)).is_valid());
    TEST_EXPECT(ctx, document.object_child(root, expected_name) == integer_value);
    const CStringView first_name{ reinterpret_cast<const std::uint8_t*>("a"), 1u };
    const CStringView last_name{ reinterpret_cast<const std::uint8_t*>("f"), 1u };
    const CStringView before_first_name{ reinterpret_cast<const std::uint8_t*>("0"), 1u };
    const CStringView missing_name{ reinterpret_cast<const std::uint8_t*>("missing"), 7u };
    const CStringView empty_name{ reinterpret_cast<const std::uint8_t*>(""), 0u };
    TEST_EXPECT(ctx, document.object_child(root, first_name) == null_value);
    TEST_EXPECT(ctx, document.object_child(root, last_name) == nested_array);
    TEST_EXPECT(ctx, !document.object_child(root, before_first_name).is_valid());
    TEST_EXPECT(ctx, !document.object_child(root, missing_name).is_valid());
    TEST_EXPECT(ctx, !document.object_child(root, empty_name).is_valid());
    TEST_EXPECT(ctx, !document.object_child(root, CStringView{}).is_valid());

    boolean_result = false;
    TEST_EXPECT(ctx, document.boolean_value(boolean_value, boolean_result) && boolean_result);
    boolean_result = true;
    TEST_EXPECT(ctx, !document.boolean_value(null_value, boolean_result) && boolean_result);
    signed_result = 0;
    TEST_EXPECT(ctx, document.signed_integer_value(integer_value, signed_result) && (signed_result == 127));
    unsigned_result = 11u;
    TEST_EXPECT(ctx, !document.unsigned_integer_value(integer_value, unsigned_result) && (unsigned_result == 11u));
    TEST_EXPECT(ctx, document.integer_metadata(integer_value, metadata));
    TEST_EXPECT(ctx, metadata.domain == EIntegerDomain::signed_value);
    TEST_EXPECT(ctx, metadata.width == EIntegerWidth::bits_8);
    TEST_EXPECT(ctx, metadata.notation == EIntegerNotation::decimal);
    TEST_EXPECT(ctx, metadata.prefix == EIntegerPrefix::standard);
    floating_result = 0.0;
    TEST_EXPECT(ctx, document.floating_point_value(floating_value, floating_result) && (floating_result == 1.25));
    TEST_EXPECT(ctx, document.string_value_id(string_value) == document.string_value_id_at_rank(1u));
    const CStringView expected_string{ reinterpret_cast<const std::uint8_t*>("text"), 4u };
    TEST_EXPECT(ctx, document.string_value(string_value) == expected_string);
    TEST_EXPECT(ctx, !document.string_value_id(null_value).is_valid());

    TEST_EXPECT(ctx, fixture.initialise());
    fixture.values()[3u].payload_bits = 255u;
    fixture.values()[3u].value_flags |= 0x01u;
    TEST_EXPECT(ctx, document.reset(fixture.bytes.data(), fixture.bytes.size()));
    const CBakedValueIndex unsigned_value =
        document.object_child(document.root(), document.property_name_id_at_rank(3u));
    unsigned_result = 0u;
    TEST_EXPECT(ctx, document.unsigned_integer_value(unsigned_value, unsigned_result) && (unsigned_result == 255u));
    signed_result = -1;
    TEST_EXPECT(ctx, !document.signed_integer_value(unsigned_value, signed_result) && (signed_result == -1));

    TEST_EXPECT(ctx, fixture.initialise());
    fixture.values()[6u].value_type = EBakedValueType::array;
    TEST_EXPECT(ctx, document.reset(fixture.bytes.data(), fixture.bytes.size()));
    const CBakedValueIndex ordinary_array =
        document.object_child(document.root(), document.property_name_id_at_rank(6u));
    TEST_EXPECT(ctx, document.array_at(ordinary_array, 0u).query_value() == 7u);
}

void test_header_layout_and_alignment_rejections(TTestContext& ctx)
{
    CBakedDocument document;
    TEST_EXPECT(ctx, !document.reset(nullptr, 0u));

    SBakedFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    TEST_EXPECT(ctx, !document.reset(fixture.bytes.data() + 1u, fixture.bytes.size() - 1u));
    TEST_EXPECT(ctx, !document.reset(fixture.bytes.data(), sizeof(SBakedDocumentHeader) - 1u));

    expect_rejected(ctx, [](SBakedFixture& value) { value.header().magic = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().version = 1u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().version = 2u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().version = 3u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().header_size = 32u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().header_size = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { --value.header().total_size; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().value_count = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().value_count = std::numeric_limits<std::uint32_t>::max(); });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().property_name_reference_count = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().property_name_byte_count = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().string_value_reference_count = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().string_value_byte_count = 0u; });

    std::uint32_t SBakedDocumentHeader::* const offsets[]{
        &SBakedDocumentHeader::values_offset,
        &SBakedDocumentHeader::property_name_references_offset,
        &SBakedDocumentHeader::string_value_references_offset,
        &SBakedDocumentHeader::property_name_bytes_offset,
        &SBakedDocumentHeader::string_value_bytes_offset };
    for (const auto member : offsets)
    {
        for (const std::uint32_t offset : { 0u, 32u, 0x80000000u, UINT32_MAX })
        {
            expect_rejected(ctx, [member, offset](SBakedFixture& value) { value.header().*member = offset; });
        }
        expect_rejected(ctx, [member](SBakedFixture& value) { ++(value.header().*member); });
        expect_rejected(ctx, [member](SBakedFixture& value) { --(value.header().*member); });
        //  Aligned gaps/overlaps are invalid too, not just misaligned offsets.
        expect_rejected(ctx, [member](SBakedFixture& value) { (value.header().*member) += 32u; });
        expect_rejected(ctx, [member](SBakedFixture& value) { (value.header().*member) -= 32u; });
    }
    for (std::size_t index = 0u; index < 3u; ++index)
    {
        expect_rejected(ctx, [index](SBakedFixture& value) { value.header().reserved[index] = 1u; });
    }
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().property_name_reference_count = UINT32_MAX; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().string_value_reference_count = UINT32_MAX; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().property_name_byte_count = UINT32_MAX; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().string_value_byte_count = UINT32_MAX; });
}

void test_value_and_topology_rejections(TTestContext& ctx)
{
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[0u].parent_index = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[0u].property_name_index = 1u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[0u].value_type = EBakedValueType::string; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[0u].value_flags = document_value_flags::k_name_present_flag; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].value_flags &= static_cast<std::uint16_t>(~document_value_flags::k_name_present_flag); });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].value_flags |= document_value_flags::k_suppress_newline_escaping_flag; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.bytes.data()[k_property_bytes_offset + 1u] = '\n'; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].value_type = EBakedValueType::invalid; });
    //  The retired recovery tag is invalid even under the current version.
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[6u].value_type = static_cast<EBakedValueType>(8u); });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.values()[1u].value_type = static_cast<EBakedValueType>(9u);
    });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].parent_index = 1u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].property_name_index = k_property_reference_count; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].first_child_index = 0u; });
    for (std::uint16_t bit = 0x0400u; bit != 0u; bit = static_cast<std::uint16_t>(bit << 1u))
    {
        expect_rejected(ctx, [bit](SBakedFixture& value) { value.values()[1u].value_flags |= bit; });
    }
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].reserved_8 = 1u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].reserved_32 = 1u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[2u].payload_bits = 2u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[3u].value_flags |= 0x02u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[3u].value_flags |= 0x18u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[3u].value_flags |= 0x30u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[3u].value_flags |= 0x40u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[4u].payload_bits = 0x7ff0000000000000ull; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[5u].payload_bits = 2u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[5u].payload_bits = 0x100000001ull; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[6u].payload_bits = 1u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[6u].first_child_index = 6u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[6u].child_count = 2u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[7u].parent_index = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[7u].property_name_index = 1u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[6u].property_name_index = 5u; });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.values()[1u].value_flags &=
            static_cast<std::uint16_t>(~document_value_flags::k_first_sibling_flag);
    });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.values()[2u].value_flags |= document_value_flags::k_first_sibling_flag;
    });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.values()[6u].value_flags &=
            static_cast<std::uint16_t>(~document_value_flags::k_last_sibling_flag);
    });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.values()[7u].value_flags &=
            static_cast<std::uint16_t>(~document_value_flags::k_last_sibling_flag);
    });
}

void test_nested_array_forms(TTestContext& ctx)
{
    SBakedFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    fixture.values()[7u].value_type = EBakedValueType::array;

    CBakedDocument document{ fixture.bytes.data(), fixture.bytes.size() };
    TEST_EXPECT(ctx, document.is_ready() && document.check_integrity());
    const CBakedValueIndex outer = document.last_child(document.root());
    const CBakedValueIndex nested = document.array_at(outer, 0u);
    TEST_EXPECT(ctx, document.value_type(nested) == EBakedValueType::array);
    TEST_EXPECT(ctx, document.child_count(nested) == 0u);
    TEST_EXPECT(ctx, !document.first_child(nested).is_valid());
    TEST_EXPECT(ctx, !document.array_at(nested, 0u).is_valid());
}

void test_numeric_canonical_acceptance(TTestContext& ctx)
{
    SBakedFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    fixture.values()[3u].payload_bits = std::numeric_limits<std::uint64_t>::max();
    fixture.values()[3u].value_flags |= 0x2fu;
    fixture.values()[4u].payload_bits = live_floating_point_bits(-0.0);
    CBakedDocument document{ fixture.bytes.data(), fixture.bytes.size() };
    TEST_EXPECT(ctx, document.is_ready() && document.check_integrity());
    const CBakedValueIndex unsigned_value =
        document.object_child(document.root(), document.property_name_id_at_rank(3u));
    std::uint64_t unsigned_result = 0u;
    CIntegerMetadata metadata;
    TEST_EXPECT(ctx,
        document.unsigned_integer_value(unsigned_value, unsigned_result) &&
        (unsigned_result == std::numeric_limits<std::uint64_t>::max()));
    TEST_EXPECT(ctx, document.integer_metadata(unsigned_value, metadata));
    TEST_EXPECT(ctx, metadata.width == EIntegerWidth::bits_64);
    TEST_EXPECT(ctx, metadata.notation == EIntegerNotation::hexadecimal);
    TEST_EXPECT(ctx, metadata.prefix == EIntegerPrefix::alternate);
    const CBakedValueIndex floating_value =
        document.object_child(document.root(), document.property_name_id_at_rank(4u));
    double floating_result = 1.0;
    TEST_EXPECT(ctx, document.floating_point_value(floating_value, floating_result));
    TEST_EXPECT(ctx, live_floating_point_bits(floating_result) == live_floating_point_bits(-0.0));

    TEST_EXPECT(ctx, fixture.initialise());
    fixture.values()[3u].payload_bits = live_signed_integer_bits(-129);
    fixture.values()[3u].value_flags |= 0x12u;
    TEST_EXPECT(ctx, document.reset(fixture.bytes.data(), fixture.bytes.size()));
    TEST_EXPECT(ctx, document.check_integrity());
    const CBakedValueIndex signed_value =
        document.object_child(document.root(), document.property_name_id_at_rank(3u));
    std::int64_t signed_result = 0;
    TEST_EXPECT(ctx, document.signed_integer_value(signed_value, signed_result) && (signed_result == -129));
    TEST_EXPECT(ctx, document.integer_metadata(signed_value, metadata));
    TEST_EXPECT(ctx, metadata.width == EIntegerWidth::bits_16);
    TEST_EXPECT(ctx, metadata.notation == EIntegerNotation::binary);
}

void test_string_table_and_coverage_rejections(TTestContext& ctx)
{
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.references(k_property_references_offset)[0u].length = 1u;
    });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        ++value.references(k_property_references_offset)[2u].offset;
    });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.bytes.data()[k_property_bytes_offset + 2u] = 1u;
    });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.bytes.data()[k_property_bytes_offset + 1u] = 0x80u;
    });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.bytes.data()[k_property_bytes_offset + 1u] = static_cast<std::uint8_t>('z');
    });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.values()[5u].payload_bits = 0u;
    });
}

void test_validation_allocation_failure(TTestContext& ctx)
{
    SBakedFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());

    tests::TAllocatorFixture allocator_fixture;
    allocator_fixture.reject_allocation = true;
    memory::CMemoryAllocator allocator{
        &allocator_fixture,
        &tests::allocate_test_memory,
        &tests::deallocate_test_memory };
    memory::CMemoryContext memory_context{ allocator };
    {
        tests::TMemoryContextScope scope{ &memory_context };
        CBakedDocument document;
        TEST_EXPECT(ctx, !document.reset(fixture.bytes.data(), fixture.bytes.size()));
        TEST_EXPECT(ctx, !document.is_ready());
    }
    TEST_EXPECT(ctx, memory_context.is_attribution_empty());
}

void test_live_document_bake(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());

    const CNodeKey zeta = live.create_string(text("pear"), text("zeta"));
    const CNodeKey alpha = live.create_array(text("alpha"));
    const CNodeKey recovery = live.create_array(text("recovery"));
    const CNodeKey empty = live.create_empty();
    const CNodeKey boolean = live.create_boolean(true);
    const CIntegerMetadata signed_metadata{
        EIntegerDomain::signed_value,
        EIntegerWidth::bits_8,
        EIntegerNotation::hexadecimal,
        EIntegerPrefix::alternate };
    const CNodeKey signed_integer = live.create_signed_integer(-42, signed_metadata);
    const CIntegerMetadata unsigned_metadata{
        EIntegerDomain::unsigned_value,
        EIntegerWidth::bits_64,
        EIntegerNotation::hexadecimal,
        EIntegerPrefix::alternate };
    const CNodeKey unsigned_integer = live.create_unsigned_integer(
        std::numeric_limits<std::uint64_t>::max(),
        unsigned_metadata);
    const CNodeKey floating_point = live.create_floating_point(-0.0);
    const CNodeKey apple = live.create_string(text("apple"));
    const CNodeKey nested_object = live.create_object();
    const CNodeKey nested_null = live.create_null(text("nested"));
    const CNodeKey recovered_string = live.create_string(text("pear"));
    const CNodeKey recovered_empty_string = live.create_string(CStringView{});
    const CNodeKey unused = live.create_string(text("unused"), text("unused"));

    TEST_EXPECT(ctx,
        zeta.is_valid() && alpha.is_valid() && recovery.is_valid() && empty.is_valid() &&
        boolean.is_valid() && signed_integer.is_valid() && unsigned_integer.is_valid() &&
        floating_point.is_valid() && apple.is_valid() && nested_object.is_valid() &&
        nested_null.is_valid() && recovered_string.is_valid() &&
        recovered_empty_string.is_valid() && unused.is_valid());
    TEST_EXPECT(ctx, live.append_child(live.root(), zeta).succeeded());
    TEST_EXPECT(ctx, live.append_child(live.root(), alpha).succeeded());
    TEST_EXPECT(ctx, live.append_child(live.root(), recovery).succeeded());
    TEST_EXPECT(ctx, live.append_child(alpha, empty).succeeded());
    TEST_EXPECT(ctx, live.append_child(alpha, boolean).succeeded());
    TEST_EXPECT(ctx, live.append_child(alpha, signed_integer).succeeded());
    TEST_EXPECT(ctx, live.append_child(alpha, unsigned_integer).succeeded());
    TEST_EXPECT(ctx, live.append_child(alpha, floating_point).succeeded());
    TEST_EXPECT(ctx, live.append_child(alpha, apple).succeeded());
    TEST_EXPECT(ctx, live.append_child(alpha, nested_object).succeeded());
    TEST_EXPECT(ctx, live.append_child(nested_object, nested_null).succeeded());
    TEST_EXPECT(ctx, live.append_child(recovery, recovered_string).succeeded());
    TEST_EXPECT(ctx, live.append_child(recovery, recovered_empty_string).succeeded());
    TEST_EXPECT(ctx, live.check_integrity());

    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    TEST_EXPECT(ctx, block.is_ready());
    TEST_EXPECT(ctx, block.document().check_integrity());
    TEST_EXPECT(ctx, block.memory_attribution().token_count == 1u);
    TEST_EXPECT(ctx, block.memory_attribution().allocation_count == 1u);
    TEST_EXPECT(ctx, block.memory_attribution().allocation_size >= block.document().byte_count());
    TEST_EXPECT(ctx, block.bytes().is_ready());
    TEST_EXPECT(ctx, block.bytes().size() == block.document().byte_count());
    TEST_EXPECT(ctx, block.bytes().align() == baked_document_format::k_block_alignment);
    TEST_EXPECT(ctx,
        (reinterpret_cast<std::uintptr_t>(block.bytes().data()) %
            baked_document_format::k_block_alignment) == 0u);

    const CBakedDocument& baked = block.document();
    TEST_EXPECT(ctx, baked.value_count() == 14u);
    TEST_EXPECT(ctx, baked.property_name_count() == 4u);
    TEST_EXPECT(ctx, baked.string_value_count() == 2u);
    TEST_EXPECT(ctx, !baked.object_child(baked.root(), text("unused")).is_valid());

    const CBakedValueIndex baked_alpha = baked.object_child(baked.root(), text("alpha"));
    const CBakedValueIndex baked_recovery = baked.object_child(baked.root(), text("recovery"));
    const CBakedValueIndex baked_zeta = baked.object_child(baked.root(), text("zeta"));
    TEST_EXPECT(ctx, baked.first_child(baked.root()) == baked_zeta);
    TEST_EXPECT(ctx, baked.next_sibling(baked_zeta) == baked_alpha);
    TEST_EXPECT(ctx, baked.next_sibling(baked_alpha) == baked_recovery);
    TEST_EXPECT(ctx, baked.last_child(baked.root()) == baked_recovery);
    TEST_EXPECT(ctx, !baked.previous_sibling(baked_zeta).is_valid());
    TEST_EXPECT(ctx, !baked.next_sibling(baked_recovery).is_valid());
    TEST_EXPECT(ctx, baked.value_type(baked.array_at(baked_alpha, 0u)) == EBakedValueType::null_value);

    bool boolean_result = false;
    TEST_EXPECT(ctx,
        baked.boolean_value(baked.array_at(baked_alpha, 1u), boolean_result) && boolean_result);
    std::int64_t signed_result = 0;
    CIntegerMetadata baked_metadata;
    const CBakedValueIndex baked_signed = baked.array_at(baked_alpha, 2u);
    TEST_EXPECT(ctx, baked.signed_integer_value(baked_signed, signed_result) && (signed_result == -42));
    TEST_EXPECT(ctx, baked.integer_metadata(baked_signed, baked_metadata));
    TEST_EXPECT(ctx, baked_metadata == signed_metadata);
    std::uint64_t unsigned_result = 0u;
    const CBakedValueIndex baked_unsigned = baked.array_at(baked_alpha, 3u);
    TEST_EXPECT(ctx,
        baked.unsigned_integer_value(baked_unsigned, unsigned_result) &&
        (unsigned_result == std::numeric_limits<std::uint64_t>::max()));
    TEST_EXPECT(ctx, baked.integer_metadata(baked_unsigned, baked_metadata));
    TEST_EXPECT(ctx, baked_metadata == unsigned_metadata);
    double floating_result = 1.0;
    TEST_EXPECT(ctx,
        baked.floating_point_value(baked.array_at(baked_alpha, 4u), floating_result) &&
        (live_floating_point_bits(floating_result) == live_floating_point_bits(-0.0)));
    TEST_EXPECT(ctx, baked.string_value(baked.array_at(baked_alpha, 5u)) == text("apple"));
    const CBakedValueIndex baked_object = baked.array_at(baked_alpha, 6u);
    TEST_EXPECT(ctx,
        baked.value_type(baked.object_child(baked_object, text("nested"))) == EBakedValueType::null_value);
    TEST_EXPECT(ctx, baked.string_value(baked.array_at(baked_recovery, 0u)) == text("pear"));
    TEST_EXPECT(ctx, baked.string_value(baked.array_at(baked_recovery, 1u)).length() == 0u);
    TEST_EXPECT(ctx, baked.string_value(baked_zeta) == text("pear"));

    CBakedDocumentBlock moved{ std::move(block) };
    TEST_EXPECT(ctx, moved.is_ready() && moved.document().check_integrity());
    TEST_EXPECT(ctx, !block.is_ready() && !block.bytes().is_ready());
    CBakedDocumentBlock assigned;
    assigned = std::move(moved);
    TEST_EXPECT(ctx, assigned.is_ready() && assigned.document().check_integrity());
    TEST_EXPECT(ctx, !moved.is_ready() && !moved.bytes().is_ready());

    CLiveDocument promoted;
    TEST_EXPECT(ctx, document_translation::promote(assigned.document(), promoted));
    TEST_EXPECT(ctx, promoted.check_integrity());
    TEST_EXPECT(ctx, promoted.value_count() == 14u);
    TEST_EXPECT(ctx, promoted.is_complete());
    TEST_EXPECT(ctx, !promoted.property_name_id_at_rank(5u).is_valid());
    TEST_EXPECT(ctx, !promoted.string_value_id_at_rank(3u).is_valid());

    CBakedDocumentBlock round_trip;
    TEST_EXPECT(ctx, document_translation::bake(promoted, round_trip));
    TEST_EXPECT(ctx, round_trip.bytes().size() == assigned.bytes().size());
    TEST_EXPECT(ctx,
        std::memcmp(
            round_trip.bytes().data(),
            assigned.bytes().data(),
            assigned.bytes().size()) == 0);

    CLiveDocument unavailable;
    const CByteConstView before_failed_rebuild = assigned.bytes();
    TEST_EXPECT(ctx, !document_translation::bake(unavailable, assigned));
    TEST_EXPECT(ctx, assigned.is_ready());
    TEST_EXPECT(ctx, assigned.bytes().data() == before_failed_rebuild.data());

    const CNodeKey promoted_root = promoted.root();
    CBakedDocument unavailable_baked;
    TEST_EXPECT(ctx, !document_translation::promote(unavailable_baked, promoted));
    TEST_EXPECT(ctx, promoted.root() == promoted_root);
    TEST_EXPECT(ctx, promoted.check_integrity());

    tests::TAllocatorFixture promotion_allocator_fixture;
    promotion_allocator_fixture.reject_allocation = true;
    memory::CMemoryAllocator promotion_allocator{
        &promotion_allocator_fixture,
        &tests::allocate_test_memory,
        &tests::deallocate_test_memory };
    memory::CMemoryContext promotion_context{ promotion_allocator };
    {
        tests::TMemoryContextScope scope{ &promotion_context };
        TEST_EXPECT(ctx, !document_translation::promote(assigned.document(), promoted));
        TEST_EXPECT(ctx, promoted.root() == promoted_root);
        TEST_EXPECT(ctx, promoted.check_integrity());
    }
    TEST_EXPECT(ctx, promotion_context.is_attribution_empty());
    assigned.deallocate();
    TEST_EXPECT(ctx, !assigned.is_ready());
    TEST_EXPECT(ctx, assigned.memory_attribution().allocation_count == 0u);
}

void test_bake_root_only_and_allocation_failure(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());

    CBakedDocumentBlock root_only;
    TEST_EXPECT(ctx, document_translation::bake(live, root_only));
    TEST_EXPECT(ctx, root_only.document().byte_count() == k_root_only_size);
    TEST_EXPECT(ctx, root_only.document().value_count() == 1u);

    CLiveDocument promoted_root_only;
    TEST_EXPECT(ctx, document_translation::promote(root_only.document(), promoted_root_only));
    TEST_EXPECT(ctx, promoted_root_only.check_integrity());
    TEST_EXPECT(ctx, promoted_root_only.value_count() == 1u);
    TEST_EXPECT(ctx, promoted_root_only.child_count(promoted_root_only.root()) == 0u);

    tests::TAllocatorFixture allocator_fixture;
    allocator_fixture.reject_allocation = true;
    memory::CMemoryAllocator allocator{
        &allocator_fixture,
        &tests::allocate_test_memory,
        &tests::deallocate_test_memory };
    memory::CMemoryContext memory_context{ allocator };
    {
        tests::TMemoryContextScope scope{ &memory_context };
        const std::uint8_t* const retained_bytes = root_only.bytes().data();
        TEST_EXPECT(ctx, !document_translation::bake(live, root_only));
        TEST_EXPECT(ctx, root_only.is_ready());
        TEST_EXPECT(ctx, root_only.bytes().data() == retained_bytes);

        CBakedDocumentBlock rejected;
        TEST_EXPECT(ctx, !document_translation::bake(live, rejected));
        TEST_EXPECT(ctx, !rejected.is_ready());
        TEST_EXPECT(ctx, rejected.memory_attribution().allocation_count == 0u);
    }
    TEST_EXPECT(ctx, memory_context.is_attribution_empty());
}

} // namespace

namespace baked_document_phase2_tests
{

static void test_shared_integer_flags_round_trip(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    TEST_EXPECT(ctx, live.set_root_type(ELiveValueType::array));
    constexpr std::int64_t signed_values[]{ -1, -129, -32769, -2147483649ll };
    constexpr std::uint64_t unsigned_values[]{ 255u, 256u, 65536u, 4294967296ull };
    constexpr EIntegerNotation notations[]{ EIntegerNotation::decimal, EIntegerNotation::hexadecimal,
        EIntegerNotation::hexadecimal, EIntegerNotation::binary };
    CIntegerMetadata expected[64u]{};
    std::uint32_t count = 0u;
    for (std::uint32_t domain = 0u; domain < 2u; ++domain)
    {
        for (std::uint32_t width = 0u; width < 4u; ++width)
        {
            for (std::uint32_t spelling = 0u; spelling < 4u; ++spelling)
            {
                const CIntegerMetadata metadata{ static_cast<EIntegerDomain>(domain),
                    static_cast<EIntegerWidth>(width), notations[spelling],
                    (spelling == 2u) ? EIntegerPrefix::alternate : EIntegerPrefix::standard };
                for (std::uint32_t named = 0u; named < 2u; ++named)
                {
                    const CStringView name = (named != 0u) ? CStringView{ "" } : CStringView{};
                    const CNodeKey value = (domain != 0u) ?
                        live.create_unsigned_integer(unsigned_values[width], metadata, name) :
                        live.create_signed_integer(signed_values[width], metadata, name);
                    TEST_EXPECT(ctx, live.append_child(live.root(), value).succeeded());
                    expected[count++] = metadata;
                }
            }
        }
    }
    TEST_EXPECT(ctx, count == 64u);
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    CLiveDocument promoted;
    TEST_EXPECT(ctx, document_translation::promote(block.document(), promoted));
    TEST_EXPECT(ctx, promoted.check_integrity());
    const auto* const records = reinterpret_cast<const SBakedValueRecord*>(
        block.bytes().data() + sizeof(SBakedDocumentHeader));
    CNodeKey promoted_value = promoted.first_child(promoted.root());
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        const CBakedValueIndex baked_value = block.document().array_at(block.document().root(), index);
        CIntegerMetadata baked_metadata;
        CIntegerMetadata promoted_metadata;
        TEST_EXPECT(ctx, block.document().integer_metadata(baked_value, baked_metadata));
        TEST_EXPECT(ctx, promoted.integer_metadata(promoted_value, promoted_metadata));
        TEST_EXPECT(ctx, baked_metadata == expected[index]);
        TEST_EXPECT(ctx, promoted_metadata == expected[index]);
        const bool named = (index & 1u) != 0u;
        TEST_EXPECT(ctx, block.document().is_object_entry(baked_value) == named);
        TEST_EXPECT(ctx, promoted.is_object_entry(promoted_value) == named);
        TEST_EXPECT(ctx, records[index + 1u].reserved_8 == 0u);
        TEST_EXPECT(ctx, (records[index + 1u].value_flags & 0x0300u) == (named ? 0x0100u : 0u));
        const std::uint16_t position = ((index == 0u) ? 0x0040u : 0u) |
            ((index + 1u == count) ? 0x0080u : 0u);
        TEST_EXPECT(ctx, (records[index + 1u].value_flags & 0x00c0u) == position);
        promoted_value = promoted.next_sibling(promoted_value);
    }
    TEST_EXPECT(ctx, !promoted_value.is_valid());
    CBakedDocumentBlock rebaked;
    TEST_EXPECT(ctx, document_translation::bake(promoted, rebaked));
    TEST_EXPECT(ctx, block.bytes().size() == rebaked.bytes().size());
    if (block.bytes().size() == rebaked.bytes().size())
    {
        TEST_EXPECT(ctx, std::memcmp(block.bytes().data(), rebaked.bytes().data(), block.bytes().size()) == 0);
    }
}

static void test_native_names_and_metadata_round_trip(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    TEST_EXPECT(ctx, live.set_root_type(ELiveValueType::array));
    const CStringView empty{ "" };
    const CNodeKey entries[]{ live.create_empty(empty), live.create_null(empty),
        live.create_boolean(true, empty), live.create_signed_integer(-7, empty),
        live.create_floating_point(1.25, empty), live.create_string(CStringView{ "a\r\nb" }, empty),
        live.create_array(empty), live.create_object(empty) };
    for (const CNodeKey entry : entries)
    {
        const CNodeKey object = live.create_object();
        TEST_EXPECT(ctx, live.append_child(live.root(), object).succeeded());
        TEST_EXPECT(ctx, live.append_child(object, entry).succeeded());
        TEST_EXPECT(ctx, live.object_child(object, empty) == entry);
        TEST_EXPECT(ctx, live.object_child(object, live.name_id(entry)) == entry);
    }
    TEST_EXPECT(ctx, live.set_newline_escaping_suppressed(entries[5u], true));
    const CNodeKey named_in_array = live.create_string(CStringView{ "a\r\nb" }, empty);
    TEST_EXPECT(ctx, live.append_child(live.root(), named_in_array).succeeded());
    TEST_EXPECT(ctx, live.string_value_id(named_in_array) == live.string_value_id(entries[5u]));
    TEST_EXPECT(ctx, !live.suppresses_newline_escaping(named_in_array));
    TEST_EXPECT(ctx, live.check_integrity());
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(live, block));
    const CBakedDocument& baked = block.document();
    TEST_EXPECT(ctx, baked.value_type(baked.root()) == EBakedValueType::array);
    TEST_EXPECT(ctx, baked.name(baked.root()).string() == nullptr);
    TEST_EXPECT(ctx, baked.property_name_count() == 0u); // Count excludes canonical empty ID zero.
    for (std::uint32_t index = 0u; index < 8u; ++index)
    {
        const CBakedValueIndex object = baked.array_at(baked.root(), index);
        const CBakedValueIndex member = baked.first_child(object);
        TEST_EXPECT(ctx, baked.is_object_entry(member));
        TEST_EXPECT(ctx, baked.name_id(member).is_empty());
        TEST_EXPECT(ctx, baked.name(member).string() != nullptr);
        TEST_EXPECT(ctx, baked.name(member).length() == 0u);
        TEST_EXPECT(ctx, baked.object_child(object, empty) == member);
        TEST_EXPECT(ctx, baked.object_child(object, baked.name_id(member)) == member);
        TEST_EXPECT(ctx, !baked.object_child(object, CStringView{}).is_valid());
        TEST_EXPECT(ctx, baked.suppresses_newline_escaping(member) == (index == 5u));
    }
    TEST_EXPECT(ctx, baked.is_object_entry(baked.array_at(baked.root(), 8u)));
    TEST_EXPECT(ctx, !baked.suppresses_newline_escaping(baked.array_at(baked.root(), 8u)));
    CLiveDocument promoted;
    TEST_EXPECT(ctx, document_translation::promote(baked, promoted));
    TEST_EXPECT(ctx, promoted.value_type(promoted.root()) == ELiveValueType::array);
    TEST_EXPECT(ctx, promoted.name(promoted.root()).string() == nullptr);
    std::uint32_t index = 0u;
    for (CNodeKey child = promoted.first_child(promoted.root()); index < 8u; child = promoted.next_sibling(child), ++index)
    {
        const CNodeKey member = promoted.object_child(child, empty);
        TEST_EXPECT(ctx, member.is_valid());
        TEST_EXPECT(ctx, promoted.name_id(member).is_empty());
        TEST_EXPECT(ctx, promoted.name(member).string() != nullptr);
        TEST_EXPECT(ctx, promoted.suppresses_newline_escaping(member) == (index == 5u));
    }
    TEST_EXPECT(ctx, promoted.is_object_entry(promoted.last_child(promoted.root())));
    TEST_EXPECT(ctx, promoted.check_integrity());
    CLiveDocument moved{ std::move(promoted) };
    CBakedDocumentBlock rebaked;
    TEST_EXPECT(ctx, document_translation::bake(moved, rebaked));
    TEST_EXPECT(ctx, block.bytes().size() == rebaked.bytes().size());
    if (block.bytes().size() == rebaked.bytes().size())
    {
        TEST_EXPECT(ctx, std::memcmp(block.bytes().data(), rebaked.bytes().data(), block.bytes().size()) == 0);
    }
}

}   //  namespace baked_document_phase2_tests

namespace baked_document_storage_tests
{

static bool prepare_root(CByteBuffer& bytes)
{
    if (!bytes.reallocate(baked_document_format::k_min_block_capacity, baked_document_format::k_min_block_capacity, 32u)) return false;
    bytes.zero_fill();
    return initialise_root_only(bytes);
}

template<typename TChange>
static void expect_adoption_rejected(TTestContext& ctx, TChange&& change)
{
    CByteBuffer initial;
    CBakedDocumentBlock destination;
    TEST_EXPECT(ctx, prepare_root(initial));
    TEST_EXPECT(ctx, destination.adopt(std::move(initial)));
    const CByteConstView previous = destination.bytes();
    const auto previous_attribution = destination.memory_attribution();
    CByteBuffer source;
    TEST_EXPECT(ctx, prepare_root(source));
    change(source);
    if (source.capacity() > source.size())
    {
        std::memset(source.data() + source.size(), 0, source.capacity() - source.size());
    }
    const std::uint8_t* const pointer = source.data();
    const std::size_t size = source.size();
    const std::size_t capacity = source.capacity();
    const auto attribution = source.memory_attribution();
    std::vector<std::uint8_t> snapshot;
    if (capacity != 0u) snapshot.assign(pointer, pointer + capacity);
    TEST_EXPECT(ctx, !destination.adopt(std::move(source)));
    TEST_EXPECT(ctx, source.data() == pointer && source.size() == size && source.capacity() == capacity);
    TEST_EXPECT(ctx, source.memory_attribution().source == attribution.source);
    TEST_EXPECT(ctx, source.memory_attribution().allocation_size == attribution.allocation_size);
    TEST_EXPECT(ctx, snapshot.empty() || std::memcmp(source.data(), snapshot.data(), capacity) == 0);
    TEST_EXPECT(ctx, destination.bytes().data() == previous.data() && destination.bytes().size() == previous.size());
    TEST_EXPECT(ctx, destination.memory_attribution().source == previous_attribution.source);
    TEST_EXPECT(ctx, destination.document().check_integrity());
}

static void test_adoption_rejections(TTestContext& ctx)
{
    static_assert(baked_document_format::k_min_document_size == 114u);
    static_assert(baked_document_format::k_min_block_capacity == 128u);
    expect_adoption_rejected(ctx, [](CByteBuffer& bytes) { bytes.deallocate(); });
    expect_adoption_rejected(ctx, [&ctx](CByteBuffer& bytes) { TEST_EXPECT(ctx, bytes.reallocate(0u, 64u, 32u)); });
    expect_adoption_rejected(ctx, [&ctx](CByteBuffer& bytes) { TEST_EXPECT(ctx, bytes.reallocate(baked_document_format::k_min_document_size, (baked_document_format::k_min_block_capacity + 1u), 32u)); });
    expect_adoption_rejected(ctx, [&ctx](CByteBuffer& bytes) { TEST_EXPECT(ctx, bytes.set_size(0u)); });
    expect_adoption_rejected(ctx, [&ctx](CByteBuffer& bytes) { TEST_EXPECT(ctx, bytes.set_size(sizeof(SBakedDocumentHeader) - 1u)); });
    expect_adoption_rejected(ctx, [&ctx](CByteBuffer& bytes) { TEST_EXPECT(ctx, bytes.set_size((baked_document_format::k_min_document_size - 1u))); });
    expect_adoption_rejected(ctx, [](CByteBuffer& bytes) { ++reinterpret_cast<SBakedDocumentHeader*>(bytes.data())->magic; });
    expect_adoption_rejected(ctx, [](CByteBuffer& bytes) { ++reinterpret_cast<SBakedDocumentHeader*>(bytes.data())->version; });
    expect_adoption_rejected(ctx, [](CByteBuffer& bytes) { ++reinterpret_cast<SBakedDocumentHeader*>(bytes.data())->header_size; });
    expect_adoption_rejected(ctx, [](CByteBuffer& bytes) { reinterpret_cast<SBakedDocumentHeader*>(bytes.data())->total_size = (baked_document_format::k_min_document_size - 1u); });
    expect_adoption_rejected(ctx, [](CByteBuffer& bytes) { reinterpret_cast<SBakedDocumentHeader*>(bytes.data())->total_size = (baked_document_format::k_min_document_size + 1u); });
    expect_adoption_rejected(ctx, [](CByteBuffer& bytes) { reinterpret_cast<SBakedDocumentHeader*>(bytes.data())->total_size = 0x80000001u; });
    expect_adoption_rejected(ctx, [](CByteBuffer& bytes) { reinterpret_cast<SBakedDocumentHeader*>(bytes.data())->value_count = UINT32_MAX; });
    expect_adoption_rejected(ctx, [](CByteBuffer& bytes)
    {
        reinterpret_cast<SBakedValueRecord*>(bytes.data() + sizeof(SBakedDocumentHeader))->value_type = EBakedValueType::invalid;
    });

    std::uint32_t SBakedDocumentHeader::* const offsets[]{
        &SBakedDocumentHeader::values_offset,
        &SBakedDocumentHeader::property_name_references_offset,
        &SBakedDocumentHeader::string_value_references_offset,
        &SBakedDocumentHeader::property_name_bytes_offset,
        &SBakedDocumentHeader::string_value_bytes_offset };
    for (const auto member : offsets)
    {
        expect_adoption_rejected(ctx, [member](CByteBuffer& bytes)
        {
            reinterpret_cast<SBakedDocumentHeader*>(bytes.data())->*member = UINT32_MAX;
        });
    }
    for (std::size_t index = 0u; index < 3u; ++index)
    {
        expect_adoption_rejected(ctx, [index](CByteBuffer& bytes)
        {
            reinterpret_cast<SBakedDocumentHeader*>(bytes.data())->reserved[index] = 1u;
        });
    }
    expect_adoption_rejected(ctx, [](CByteBuffer& bytes) { reinterpret_cast<SBakedDocumentHeader*>(bytes.data())->version = 3u; });

    CByteBuffer valid;
    TEST_EXPECT(ctx, prepare_root(valid));
    CBakedDocument view{ valid.data(), valid.size() };
    TEST_EXPECT(ctx, view.is_ready());
    //  The over-limit extent must be rejected before inspecting beyond this tiny allocation.
    TEST_EXPECT(ctx, !view.reset(valid.data(), memory::k_byte_size_ceiling + 1u));
    TEST_EXPECT(ctx, !view.is_ready() && view.byte_count() == 0u);
    TEST_EXPECT(ctx, !view.reset(valid.data(), memory::k_byte_size_ceiling));

    tests::TAllocatorFixture fixture{ true };
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_test_memory, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    CByteBuffer initial;
    CBakedDocumentBlock destination;
    TEST_EXPECT(ctx, prepare_root(initial) && destination.adopt(std::move(initial)));
    const auto old_pointer = destination.bytes().data();
    const auto source_pointer = valid.data();
    {
        const tests::TMemoryContextScope scope{ &context };
        TEST_EXPECT(ctx, !destination.adopt(std::move(valid)));
    }
    TEST_EXPECT(ctx, valid.data() == source_pointer && valid.size() == baked_document_format::k_min_document_size);
    TEST_EXPECT(ctx, destination.bytes().data() == old_pointer && destination.document().check_integrity());
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

//  A truthful 16-byte allocator whose returned address is deliberately not 32-byte aligned.
static void* MV_STD_ABI_CALL allocate_offset(void*, const std::size_t alignment, const std::size_t bytes) noexcept
{
    if (alignment != 16u) return nullptr;
    void* const base = tests::allocate_test_memory(nullptr, 32u, bytes + 32u);
    return base ? static_cast<std::uint8_t*>(base) + 16u : nullptr;
}

static bool MV_STD_ABI_CALL deallocate_offset(void*, const std::size_t alignment, void* const pointer) noexcept
{
    return (alignment == 16u) && tests::deallocate_test_memory(nullptr, 32u, static_cast<std::uint8_t*>(pointer) - 16u);
}

static void test_misaligned_adoption(TTestContext& ctx)
{
    memory::CMemoryAllocator allocator{ nullptr, &allocate_offset, &deallocate_offset };
    memory::CMemoryContext context{ allocator };
    {
        const tests::TMemoryContextScope scope{ &context };
        CByteBuffer source;
        TEST_EXPECT(ctx, source.reallocate(baked_document_format::k_min_block_capacity, baked_document_format::k_min_block_capacity, 16u));
        if (!source.is_ready()) return;
        std::memset(source.data(), 0xff, source.size());
        TEST_EXPECT(ctx, (reinterpret_cast<std::uintptr_t>(source.data()) & (baked_document_format::k_block_alignment - 1u)) == 16u);
        const auto pointer = source.data();
        CBakedDocumentBlock destination;
        TEST_EXPECT(ctx, !destination.adopt(std::move(source)));
        TEST_EXPECT(ctx, source.is_ready() && source.data() == pointer && !destination.is_ready());
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

static void test_adoption_lifetime(TTestContext& ctx)
{
    tests::TAllocatorFixture fixture;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_test_memory, &tests::deallocate_test_memory };
    memory::CMemoryContext source_context{ allocator }, old_context{ allocator };
    {
        CByteBuffer source;
        CBakedDocumentBlock destination;
        {
            const tests::TMemoryContextScope scope{ &old_context };
            CByteBuffer initial;
            TEST_EXPECT(ctx, prepare_root(initial) && destination.adopt(std::move(initial)));
        }
        {
            const tests::TMemoryContextScope scope{ &source_context };
            TEST_EXPECT(ctx, prepare_root(source));
            TEST_EXPECT(ctx, source.reallocate(baked_document_format::k_min_block_capacity, 192u, 64u));
            //  Adoption derives extent from the header, not padding or its contents.
            std::memset(source.data() + baked_document_format::k_min_document_size, 0xa5, source.capacity() - baked_document_format::k_min_document_size);
        }
        const auto pointer = source.data();
        TEST_EXPECT(ctx, destination.adopt(std::move(source)));
        TEST_EXPECT(ctx, !source.is_ready() && source.size() == 0u && source.capacity() == 0u);
        TEST_EXPECT(ctx, old_context.is_attribution_empty());
        TEST_EXPECT(ctx, destination.bytes().data() == pointer && destination.bytes().size() == baked_document_format::k_min_document_size);
        TEST_EXPECT(ctx, destination.memory_attribution().source == &source_context);
        TEST_EXPECT(ctx, destination.memory_attribution().allocation_count == 1u);
        TEST_EXPECT(ctx, destination.memory_attribution().allocation_size == 192u);
        TEST_EXPECT(ctx, destination.document().check_integrity());
        CBakedDocumentBlock moved{ std::move(destination) };
        TEST_EXPECT(ctx, !destination.document().is_ready() && destination.bytes().is_empty());
        CBakedDocumentBlock& same = moved;
        moved = std::move(same);
        TEST_EXPECT(ctx, moved.bytes().data() == pointer);
        destination = std::move(moved);
        TEST_EXPECT(ctx, !moved.document().is_ready() && moved.bytes().is_empty());
        TEST_EXPECT(ctx, destination.bytes().data() == pointer);
        destination.deallocate();
        TEST_EXPECT(ctx, !destination.is_ready() && destination.bytes().is_empty());
        TEST_EXPECT(ctx, !destination.document().is_ready());
        TEST_EXPECT(ctx, source_context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, source_context.is_attribution_empty() && old_context.is_attribution_empty());
}

struct SRejectingViewAllocator
{
    std::size_t requests{ 0u };

    static void* MV_STD_ABI_CALL allocate(void* const state, const std::size_t, const std::size_t) noexcept
    {
        ++static_cast<SRejectingViewAllocator*>(state)->requests;
        return nullptr;
    }
};

static void test_views_from_validated_block(TTestContext& ctx)
{
    static_assert(std::is_same_v<decltype(std::declval<const CBakedDocumentBlock&>().document()), CBakedDocument>);
    static_assert(std::is_nothrow_constructible_v<CBakedDocument, const CBakedDocumentBlock&>);
    CByteBuffer source;
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, prepare_root(source));
    TEST_EXPECT(ctx, source.reallocate(baked_document_format::k_min_block_capacity, 192u, 32u));
    const auto pointer = source.data();
    TEST_EXPECT(ctx, block.adopt(std::move(source)));
    TEST_EXPECT(ctx, block.bytes().data() == pointer && block.bytes().size() == baked_document_format::k_min_document_size);
    TEST_EXPECT(ctx, block.memory_attribution().allocation_size == 192u);

    SRejectingViewAllocator fixture;
    memory::CMemoryAllocator allocator{ &fixture, &SRejectingViewAllocator::allocate, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        const tests::TMemoryContextScope scope{ &context };
        const CBakedDocument direct{ block };
        const CBakedDocument accessor = block.document();
        const CMutableBakedDocument editable{ block };
        TEST_EXPECT(ctx, direct.is_ready() && accessor.is_ready());
        TEST_EXPECT(ctx, editable.is_ready());
        TEST_EXPECT(ctx, editable.baked().root() == direct.root());
        TEST_EXPECT(ctx, direct.byte_count() == baked_document_format::k_min_document_size && accessor.byte_count() == baked_document_format::k_min_document_size);
        TEST_EXPECT(ctx, direct.value_count() == 1u && accessor.value_count() == 1u);
        TEST_EXPECT(ctx, direct.value_type(direct.root()) == EBakedValueType::object);
        TEST_EXPECT(ctx, accessor.value_type(accessor.root()) == EBakedValueType::object);
        TEST_EXPECT(ctx, fixture.requests == 0u);

        CBakedDocumentBlock moved{ std::move(block) };
        TEST_EXPECT(ctx, !CBakedDocument{ block }.is_ready() && !block.document().is_ready());
        TEST_EXPECT(ctx, CBakedDocument{ moved }.is_ready() && moved.document().is_ready());
        //  Moving the owner leaves the borrowed storage alive at the same address.
        TEST_EXPECT(ctx, direct.value_count() == 1u && accessor.value_count() == 1u);
        block = std::move(moved);
        TEST_EXPECT(ctx, !CBakedDocument{ moved }.is_ready() && !moved.document().is_ready());
        TEST_EXPECT(ctx, block.bytes().data() == pointer && block.bytes().align() == 32u);
        TEST_EXPECT(ctx, fixture.requests == 0u);

        //  Arbitrary-byte binding and explicit integrity checks still validate.
        const CBakedDocument checked{ block.bytes().data(), block.bytes().size() };
        TEST_EXPECT(ctx, !checked.is_ready() && fixture.requests == 1u);
        TEST_EXPECT(ctx, !direct.check_integrity() && fixture.requests == 2u);
        TEST_EXPECT(ctx, direct.is_ready() && accessor.is_ready());

        block.deallocate();
        TEST_EXPECT(ctx, !CBakedDocument{ block }.is_ready() && !block.document().is_ready());
        TEST_EXPECT(ctx, CBakedDocument{ block }.byte_count() == 0u && block.document().byte_count() == 0u);
        const CBakedDocumentBlock empty;
        TEST_EXPECT(ctx, !CBakedDocument{ empty }.is_ready() && !empty.document().is_ready());
        TEST_EXPECT(ctx, fixture.requests == 2u);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

struct SBakingAllocator
{
    bool reject_output{ false };
    std::size_t output_requests{ 0u };
    void* output{ nullptr };

    static void* MV_STD_ABI_CALL allocate(void* const state, const std::size_t alignment, const std::size_t bytes) noexcept
    {
        auto& fixture = *static_cast<SBakingAllocator*>(state);
        const bool is_output = (alignment == 32u) && (bytes == baked_document_format::k_min_block_capacity);
        if (is_output)
        {
            ++fixture.output_requests;
            if (fixture.reject_output) return nullptr;
        }
        void* const result = tests::allocate_test_memory(nullptr, alignment, bytes);
        if (result) std::memset(result, 0xcc, bytes);
        if (is_output) fixture.output = result;
        return result;
    }
};

static void test_baking_storage(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    SBakingAllocator fixture;
    memory::CMemoryAllocator allocator{ &fixture, &SBakingAllocator::allocate, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        const tests::TMemoryContextScope scope{ &context };
        CBakedDocumentBlock block;
        TEST_EXPECT(ctx, document_translation::bake(live, block));
        TEST_EXPECT(ctx, fixture.output_requests == 1u && block.bytes().data() == fixture.output);
        TEST_EXPECT(ctx, block.bytes().size() == baked_document_format::k_min_document_size && block.memory_attribution().allocation_size == baked_document_format::k_min_block_capacity);
        if (block.is_ready())
        {
            const auto& header = *reinterpret_cast<const SBakedDocumentHeader*>(block.bytes().data());
            TEST_EXPECT(ctx, header.version == 4u && header.header_size == 64u && header.total_size == 114u);
            TEST_EXPECT(ctx, header.values_offset == 64u);
            TEST_EXPECT(ctx, header.property_name_references_offset == 96u && header.string_value_references_offset == 104u);
            TEST_EXPECT(ctx, header.property_name_bytes_offset == 112u && header.string_value_bytes_offset == 113u);
            TEST_EXPECT(ctx, (reinterpret_cast<std::uintptr_t>(block.bytes().data() + header.values_offset) & 31u) == 0u);
            for (const std::uint32_t reserved : header.reserved) TEST_EXPECT(ctx, reserved == 0u);
            const auto allocation = static_cast<const std::uint8_t*>(fixture.output);
            for (std::size_t index = baked_document_format::k_min_document_size; index < baked_document_format::k_min_block_capacity; ++index) TEST_EXPECT(ctx, allocation[index] == 0u);
        }
        const auto pointer = block.bytes().data();
        fixture.reject_output = true;
        TEST_EXPECT(ctx, !document_translation::bake(live, block));
        TEST_EXPECT(ctx, fixture.output_requests == 2u);
        TEST_EXPECT(ctx, block.bytes().data() == pointer && block.document().check_integrity());
        TEST_EXPECT(ctx, context.get_live_allocation_count() == 1u);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

}   //  namespace baked_document_storage_tests

int run_baked_document_tests()
{
    TTestContext ctx;
    test_fixed_layout_value_editing(ctx);
    test_existing_string_reassignment(ctx);
    test_checked_binding_and_foundational_observations(ctx);
    baked_document_phase2_tests::test_shared_integer_flags_round_trip(ctx);
    baked_document_phase2_tests::test_native_names_and_metadata_round_trip(ctx);
    test_query_surface(ctx);
    test_header_layout_and_alignment_rejections(ctx);
    test_value_and_topology_rejections(ctx);
    test_nested_array_forms(ctx);
    test_numeric_canonical_acceptance(ctx);
    test_string_table_and_coverage_rejections(ctx);
    test_validation_allocation_failure(ctx);
    test_live_document_bake(ctx);
    test_bake_root_only_and_allocation_failure(ctx);
    baked_document_storage_tests::test_adoption_rejections(ctx);
    baked_document_storage_tests::test_misaligned_adoption(ctx);
    baked_document_storage_tests::test_adoption_lifetime(ctx);
    baked_document_storage_tests::test_views_from_validated_block(ctx);
    baked_document_storage_tests::test_baking_storage(ctx);

    std::cout << "BakedDocument: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
