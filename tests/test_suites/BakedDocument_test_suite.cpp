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

#include "containers/ByteBuffers.hpp"
#include "data_model/baked_document.hpp"
#include "data_model/baked_document_format.hpp"
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
constexpr std::uint32_t k_values_offset = 32u;
constexpr std::uint32_t k_property_references_offset = k_values_offset + (k_value_count * 32u);
constexpr std::uint32_t k_string_references_offset = k_property_references_offset + (k_property_reference_count * 8u);
constexpr std::uint32_t k_property_bytes_offset = k_string_references_offset + (k_string_reference_count * 8u);
constexpr std::uint32_t k_property_byte_count = 13u;
constexpr std::uint32_t k_string_bytes_offset = k_property_bytes_offset + k_property_byte_count;
constexpr std::uint32_t k_string_byte_count = 6u;
constexpr std::uint32_t k_total_size = k_string_bytes_offset + k_string_byte_count;
constexpr std::uint32_t k_root_only_size = 82u;

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
        records[6u].value_type = EBakedValueType::recovered_array;
        records[6u].first_child_index = 7u;
        records[6u].child_count = 1u;
        records[7u].value_type = EBakedValueType::null_value;

        for (std::uint32_t index = 1u; index <= 6u; ++index)
        {
            records[index].parent_index = 0u;
            records[index].property_name_index = index;
        }
        records[7u].parent_index = 6u;
        records[1u].value_flags |= baked_document_format::k_first_sibling_flag;
        records[6u].value_flags |= baked_document_format::k_last_sibling_flag;
        records[7u].value_flags |= baked_document_format::k_sibling_position_flags;

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
    TEST_EXPECT(ctx, !unavailable.is_canonical());
    TEST_EXPECT(ctx, !unavailable.contains_recovered_content());

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
    TEST_EXPECT(ctx, !document.is_canonical());
    TEST_EXPECT(ctx, document.contains_recovered_content());

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

    SBakedFixture canonical_fixture;
    TEST_EXPECT(ctx, canonical_fixture.initialise());
    canonical_fixture.values()[6u].value_type = EBakedValueType::array;
    CBakedDocument canonical{ canonical_fixture.bytes.data(), canonical_fixture.bytes.size() };
    TEST_EXPECT(ctx, canonical.is_ready());
    TEST_EXPECT(ctx, canonical.is_canonical());
    TEST_EXPECT(ctx, !canonical.contains_recovered_content());

    CByteBuffer root_only_bytes;
    TEST_EXPECT(ctx, initialise_root_only(root_only_bytes));
    CBakedDocument root_only{ root_only_bytes.data(), root_only_bytes.size() };
    TEST_EXPECT(ctx, root_only.is_ready() && root_only.check_integrity());
    TEST_EXPECT(ctx, root_only.value_count() == 1u);
    TEST_EXPECT(ctx, root_only.property_name_count() == 0u);
    TEST_EXPECT(ctx, root_only.string_value_count() == 0u);
    TEST_EXPECT(ctx, root_only.is_canonical());

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
    const CBakedValueIndex recovered_array = document.next_sibling(string_value);
    const CBakedValueIndex recovered_child = document.first_child(recovered_array);

    TEST_EXPECT(ctx, document.contains(root));
    TEST_EXPECT(ctx, document.contains(recovered_child));
    TEST_EXPECT(ctx, !document.contains(invalid));
    TEST_EXPECT(ctx, document.value_type(root) == EBakedValueType::object);
    TEST_EXPECT(ctx, document.value_type(null_value) == EBakedValueType::null_value);
    TEST_EXPECT(ctx, document.value_type(boolean_value) == EBakedValueType::boolean);
    TEST_EXPECT(ctx, document.value_type(integer_value) == EBakedValueType::integer);
    TEST_EXPECT(ctx, document.value_type(floating_value) == EBakedValueType::floating_point);
    TEST_EXPECT(ctx, document.value_type(string_value) == EBakedValueType::string);
    TEST_EXPECT(ctx, document.value_type(recovered_array) == EBakedValueType::recovered_array);
    TEST_EXPECT(ctx, document.value_type(recovered_child) == EBakedValueType::null_value);
    TEST_EXPECT(ctx, !document.is_object_entry(root));
    TEST_EXPECT(ctx, document.is_object_entry(null_value));
    TEST_EXPECT(ctx, !document.is_object_entry(recovered_child));
    TEST_EXPECT(ctx, document.name_id(root).is_empty());
    TEST_EXPECT(ctx, document.name_id(integer_value) == document.property_name_id_at_rank(3u));
    const CStringView expected_name{ reinterpret_cast<const std::uint8_t*>("c"), 1u };
    TEST_EXPECT(ctx, document.name(integer_value) == expected_name);

    TEST_EXPECT(ctx, !document.parent(root).is_valid());
    TEST_EXPECT(ctx, document.parent(null_value) == root);
    TEST_EXPECT(ctx, document.parent(recovered_child) == recovered_array);
    TEST_EXPECT(ctx, !document.previous_sibling(null_value).is_valid());
    TEST_EXPECT(ctx, document.next_sibling(null_value) == boolean_value);
    TEST_EXPECT(ctx, document.previous_sibling(recovered_array) == string_value);
    TEST_EXPECT(ctx, !document.next_sibling(recovered_array).is_valid());
    TEST_EXPECT(ctx, !document.previous_sibling(recovered_child).is_valid());
    TEST_EXPECT(ctx, !document.next_sibling(recovered_child).is_valid());
    TEST_EXPECT(ctx, document.child_count(root) == 6u);
    TEST_EXPECT(ctx, document.child_count(recovered_array) == 1u);
    TEST_EXPECT(ctx, document.child_count(null_value) == 0u);
    TEST_EXPECT(ctx, document.first_child(root) == null_value);
    TEST_EXPECT(ctx, document.last_child(root) == recovered_array);
    TEST_EXPECT(ctx, document.last_child(recovered_array) == recovered_child);
    TEST_EXPECT(ctx, !document.first_child(null_value).is_valid());
    TEST_EXPECT(ctx, document.array_at(recovered_array, 0u) == recovered_child);
    TEST_EXPECT(ctx, !document.array_at(recovered_array, 1u).is_valid());
    TEST_EXPECT(ctx, !document.array_at(root, 0u).is_valid());

    TEST_EXPECT(ctx, document.object_child(root, document.property_name_id_at_rank(3u)) == integer_value);
    TEST_EXPECT(ctx, !document.object_child(root, CPropertyNameId{}).is_valid());
    TEST_EXPECT(ctx, !document.object_child(recovered_array, document.property_name_id_at_rank(3u)).is_valid());
    TEST_EXPECT(ctx, document.object_child(root, expected_name) == integer_value);
    const CStringView first_name{ reinterpret_cast<const std::uint8_t*>("a"), 1u };
    const CStringView last_name{ reinterpret_cast<const std::uint8_t*>("f"), 1u };
    const CStringView before_first_name{ reinterpret_cast<const std::uint8_t*>("0"), 1u };
    const CStringView missing_name{ reinterpret_cast<const std::uint8_t*>("missing"), 7u };
    const CStringView empty_name{ reinterpret_cast<const std::uint8_t*>(""), 0u };
    TEST_EXPECT(ctx, document.object_child(root, first_name) == null_value);
    TEST_EXPECT(ctx, document.object_child(root, last_name) == recovered_array);
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
    fixture.values()[3u].value_flags = 0x01u;
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
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().version = 2u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().header_size = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { --value.header().total_size; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().value_count = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().value_count = std::numeric_limits<std::uint32_t>::max(); });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().property_name_reference_count = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().property_name_byte_count = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().string_value_reference_count = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.header().string_value_byte_count = 0u; });
}

void test_value_and_topology_rejections(TTestContext& ctx)
{
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[0u].parent_index = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[0u].property_name_index = 1u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[0u].value_type = EBakedValueType::array; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].value_type = EBakedValueType::invalid; });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.values()[1u].value_type = static_cast<EBakedValueType>(9u);
    });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].parent_index = 1u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].property_name_index = k_property_reference_count; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].first_child_index = 0u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].reserved_16 = 1u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[1u].reserved_32 = 1u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[2u].payload_bits = 2u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[3u].value_flags = 0x02u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[3u].value_flags = 0x18u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[3u].value_flags = 0x30u; });
    expect_rejected(ctx, [](SBakedFixture& value) { value.values()[3u].value_flags = 0x40u; });
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
            static_cast<std::uint8_t>(~baked_document_format::k_first_sibling_flag);
    });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.values()[2u].value_flags |= baked_document_format::k_first_sibling_flag;
    });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.values()[6u].value_flags &=
            static_cast<std::uint8_t>(~baked_document_format::k_last_sibling_flag);
    });
    expect_rejected(ctx, [](SBakedFixture& value)
    {
        value.values()[7u].value_flags &=
            static_cast<std::uint8_t>(~baked_document_format::k_last_sibling_flag);
    });
}

void test_recovered_array_forms(TTestContext& ctx)
{
    SBakedFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    fixture.values()[7u].value_type = EBakedValueType::recovered_array;

    CBakedDocument document{ fixture.bytes.data(), fixture.bytes.size() };
    TEST_EXPECT(ctx, document.is_ready() && document.check_integrity());
    TEST_EXPECT(ctx, document.contains_recovered_content());
    const CBakedValueIndex outer = document.last_child(document.root());
    const CBakedValueIndex nested = document.array_at(outer, 0u);
    TEST_EXPECT(ctx, document.value_type(nested) == EBakedValueType::recovered_array);
    TEST_EXPECT(ctx, document.child_count(nested) == 0u);
    TEST_EXPECT(ctx, !document.first_child(nested).is_valid());
    TEST_EXPECT(ctx, !document.array_at(nested, 0u).is_valid());
}

void test_numeric_canonical_acceptance(TTestContext& ctx)
{
    SBakedFixture fixture;
    TEST_EXPECT(ctx, fixture.initialise());
    fixture.values()[3u].payload_bits = std::numeric_limits<std::uint64_t>::max();
    fixture.values()[3u].value_flags = 0x2fu;
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
    fixture.values()[3u].value_flags = 0x12u;
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

} // namespace

int run_baked_document_tests()
{
    TTestContext ctx;
    test_checked_binding_and_foundational_observations(ctx);
    test_query_surface(ctx);
    test_header_layout_and_alignment_rejections(ctx);
    test_value_and_topology_rejections(ctx);
    test_recovered_array_forms(ctx);
    test_numeric_canonical_acceptance(ctx);
    test_string_table_and_coverage_rejections(ctx);
    test_validation_allocation_failure(ctx);

    std::cout << "BakedDocument: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
