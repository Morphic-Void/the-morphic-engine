
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    LiveDocument_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    1 Sep 26

#include "tests/test_suites/LiveDocument_test_suite.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>

#include "data_model/live_document.hpp"
#include "memory/memory_context.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"

struct SLiveDocumentTestAccess
{
    [[nodiscard]] static bool append_orphan_property_name(
        CLiveDocument& document, const CStringView& value) noexcept
    {
        return document.m_property_names.append(value.string(), value.length()) != CStableStrings::k_invalid_id;
    }

    [[nodiscard]] static bool append_orphan_string_value(
        CLiveDocument& document, const CStringView& value) noexcept
    {
        return document.m_string_values.append(value.string(), value.length()) != CStableStrings::k_invalid_id;
    }

    static void replace_self_with_root(CLiveDocument& document, const CNodeKey key) noexcept
    {
        CLiveDocument::SLiveNode* const node = document.m_nodes.get_slot(key);
        if (node != nullptr)
        {
            node->self = document.m_root;
        }
    }

    static void set_next_monotonic_node_key(CLiveDocument& document, const std::uint64_t key) noexcept
    {
        document.m_next_monotonic_node_key = key;
    }
};

namespace
{

using TTestContext = tests::TTestContext;

struct SFailingAllocator
{
    std::size_t attempt{ 0u };
    std::size_t fail_on{ std::numeric_limits<std::size_t>::max() };
    bool reject_all{ false };
};

void* MV_STD_ABI_CALL allocate_with_failure(
    void* const state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    SFailingAllocator& fixture = *static_cast<SFailingAllocator*>(state);
    const std::size_t attempt = fixture.attempt++;
    if (fixture.reject_all || (attempt == fixture.fail_on))
    {
        return nullptr;
    }
    return tests::allocate_test_memory(nullptr, alignment, bytes);
}

bool MV_STD_ABI_CALL deallocate_with_failure(
    void* const, const std::size_t alignment, void* const pointer) noexcept
{
    return tests::deallocate_test_memory(nullptr, alignment, pointer);
}

CStringView bytes_view(const std::uint8_t* const bytes, const std::size_t size) noexcept
{
    return CStringView{ bytes, size };
}

void test_initialisation_root_and_empty_domains(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, !document.is_ready());
    TEST_EXPECT(ctx, !document.check_integrity());
    TEST_EXPECT(ctx, document.initialise());
    TEST_EXPECT(ctx, document.is_ready());
    TEST_EXPECT(ctx, document.check_integrity());
    TEST_EXPECT(ctx, document.is_canonical());
    TEST_EXPECT(ctx, document.root().query_value() == 1u);
    TEST_EXPECT(ctx, document.value_count() == 1u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, document.value_type(document.root()) == ELiveValueType::object);
    TEST_EXPECT(ctx, !document.is_object_entry(document.root()));
    TEST_EXPECT(ctx, !document.is_detached(document.root()));
    TEST_EXPECT(ctx, !document.parent(document.root()).is_valid());
    TEST_EXPECT(ctx, document.child_count(document.root()) == 0u);
    TEST_EXPECT(ctx, !document.first_child(document.root()).is_valid());
    TEST_EXPECT(ctx, !document.last_child(document.root()).is_valid());

    const CPropertyNameId root_name = document.name_id(document.root());
    TEST_EXPECT(ctx, root_name.is_valid());
    TEST_EXPECT(ctx, root_name.is_empty());
    TEST_EXPECT(ctx, root_name.query_value() == 0u);
    TEST_EXPECT(ctx, document.referenced_property_name_count() == 0u);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 0u);
    TEST_EXPECT(ctx, document.name(document.root()).string() == nullptr);
    TEST_EXPECT(ctx, document.name(document.root()).length() == 0u);

    const CPropertyNameId invalid_name;
    const CStringValueId invalid_string;
    TEST_EXPECT(ctx, !invalid_name.is_valid());
    TEST_EXPECT(ctx, !invalid_name.is_empty());
    TEST_EXPECT(ctx, !invalid_string.is_valid());
    TEST_EXPECT(ctx, !invalid_string.is_empty());
    TEST_EXPECT(ctx, !document.name_id(CNodeKey{}).is_valid());
    TEST_EXPECT(ctx, !document.string_value_id(document.root()).is_valid());

    const CNodeKey empty_string = document.create_string(CStringView{});
    TEST_EXPECT(ctx, empty_string.is_valid());
    const CStringValueId empty_string_id = document.string_value_id(empty_string);
    TEST_EXPECT(ctx, empty_string_id.is_valid());
    TEST_EXPECT(ctx, empty_string_id.is_empty());
    TEST_EXPECT(ctx, empty_string_id.query_value() == 0u);
    TEST_EXPECT(ctx, document.string_value(empty_string).string() == nullptr);
    TEST_EXPECT(ctx, document.string_value(empty_string).length() == 0u);
    TEST_EXPECT(ctx, document.value_count() == 1u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 0u);
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_detached_creation_and_accessors(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());

    const CStringView truth_name{ reinterpret_cast<const std::uint8_t*>("truth"), 5u };
    const CNodeKey null_value = document.create_null();
    const CNodeKey boolean = document.create_boolean(true, truth_name);
    const CNodeKey signed_integer = document.create_signed_integer(-129);
    const CNodeKey unsigned_integer = document.create_unsigned_integer(std::numeric_limits<std::uint64_t>::max());
    const CNodeKey floating = document.create_floating_point(1.25);
    const CNodeKey string = document.create_string(CStringView{ reinterpret_cast<const std::uint8_t*>("value"), 5u });
    const CNodeKey array = document.create_array();
    const CNodeKey object = document.create_object(CStringView{ reinterpret_cast<const std::uint8_t*>("object"), 6u });

    TEST_EXPECT(ctx, null_value.query_value() == 3u);
    TEST_EXPECT(ctx, boolean.query_value() == 4u);
    TEST_EXPECT(ctx, signed_integer.query_value() == 5u);
    TEST_EXPECT(ctx, unsigned_integer.query_value() == 6u);
    TEST_EXPECT(ctx, floating.query_value() == 7u);
    TEST_EXPECT(ctx, string.query_value() == 8u);
    TEST_EXPECT(ctx, array.query_value() == 9u);
    TEST_EXPECT(ctx, object.query_value() == 11u);
    TEST_EXPECT(ctx, document.value_count() == 1u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, document.referenced_property_name_count() == 0u);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 0u);

    TEST_EXPECT(ctx, document.value_type(null_value) == ELiveValueType::null_value);
    TEST_EXPECT(ctx, document.value_type(boolean) == ELiveValueType::boolean);
    TEST_EXPECT(ctx, document.value_type(signed_integer) == ELiveValueType::integer);
    TEST_EXPECT(ctx, document.value_type(floating) == ELiveValueType::floating_point);
    TEST_EXPECT(ctx, document.value_type(string) == ELiveValueType::string);
    TEST_EXPECT(ctx, document.value_type(array) == ELiveValueType::array);
    TEST_EXPECT(ctx, document.value_type(object) == ELiveValueType::object);
    TEST_EXPECT(ctx, document.is_object_entry(boolean));
    TEST_EXPECT(ctx, document.is_object_entry(object));
    TEST_EXPECT(ctx, !document.is_object_entry(array));
    TEST_EXPECT(ctx, document.is_detached(null_value));
    TEST_EXPECT(ctx, document.is_detached(array));
    TEST_EXPECT(ctx, !document.parent(array).is_valid());
    TEST_EXPECT(ctx, !document.previous_sibling(array).is_valid());
    TEST_EXPECT(ctx, !document.next_sibling(array).is_valid());
    TEST_EXPECT(ctx, document.child_count(array) == 0u);
    TEST_EXPECT(ctx, document.child_count(object) == 0u);

    bool bool_result = false;
    std::int64_t signed_result = 0;
    std::uint64_t unsigned_result = 0u;
    double float_result = 0.0;
    TEST_EXPECT(ctx, document.boolean_value(boolean, bool_result) && bool_result);
    TEST_EXPECT(ctx, document.signed_integer_value(signed_integer, signed_result) && (signed_result == -129));
    TEST_EXPECT(ctx, document.unsigned_integer_value(unsigned_integer, unsigned_result) &&
        (unsigned_result == std::numeric_limits<std::uint64_t>::max()));
    TEST_EXPECT(ctx, document.floating_point_value(floating, float_result) && (float_result == 1.25));
    const CStringView expected_string{ reinterpret_cast<const std::uint8_t*>("value"), 5u };
    TEST_EXPECT(ctx, document.string_value(string) == expected_string);
    TEST_EXPECT(ctx, document.name(boolean) == truth_name);
    TEST_EXPECT(ctx, !document.boolean_value(null_value, bool_result));
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_utf8_normalisation_and_rejection(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());

    const std::uint8_t literal_nul[]{ 'a', 0u, 'b' };
    const std::uint8_t modified_nul[]{ 'a', 0xc0u, 0x80u, 'b' };
    const CNodeKey literal = document.create_string(bytes_view(literal_nul, sizeof(literal_nul)));
    const CNodeKey modified = document.create_string(bytes_view(modified_nul, sizeof(modified_nul)));
    TEST_EXPECT(ctx, literal.is_valid() && modified.is_valid());
    TEST_EXPECT(ctx, document.string_value_id(literal) == document.string_value_id(modified));
    TEST_EXPECT(ctx, document.string_value(literal).length() == sizeof(modified_nul));
    TEST_EXPECT(ctx, std::memcmp(document.string_value(literal).string(), modified_nul, sizeof(modified_nul)) == 0);

    const CNodeKey named_literal = document.create_null(bytes_view(literal_nul, sizeof(literal_nul)));
    const CNodeKey named_modified = document.create_boolean(false, bytes_view(modified_nul, sizeof(modified_nul)));
    TEST_EXPECT(ctx, named_literal.is_valid() && named_modified.is_valid());
    TEST_EXPECT(ctx, document.name_id(named_literal) == document.name_id(named_modified));

    const std::uint8_t malformed_overlong[]{ 0xc0u, 0x81u };
    const std::uint8_t malformed_truncated[]{ 0xe2u, 0x82u };
    const std::uint8_t malformed_surrogate[]{ 0xedu, 0xa0u, 0x80u };
    const std::uint8_t malformed_high[]{ 0xf4u, 0x90u, 0x80u, 0x80u };
    const std::uint32_t values_before = document.value_count();
    TEST_EXPECT(ctx, !document.create_string(bytes_view(malformed_overlong, sizeof(malformed_overlong))).is_valid());
    TEST_EXPECT(ctx, !document.create_string(bytes_view(malformed_truncated, sizeof(malformed_truncated))).is_valid());
    TEST_EXPECT(ctx, !document.create_string(bytes_view(malformed_surrogate, sizeof(malformed_surrogate))).is_valid());
    TEST_EXPECT(ctx, !document.create_null(bytes_view(malformed_high, sizeof(malformed_high))).is_valid());
    TEST_EXPECT(ctx, document.value_count() == values_before);
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_aliased_string_admission(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());

    const CStringView property_source{
        reinterpret_cast<const std::uint8_t*>("property-source"), 15u };
    const CNodeKey named = document.create_null(property_source);
    TEST_EXPECT(ctx, named.is_valid());

    const CStringView stored_property = document.name(named);
    const CStringView property_substring{ stored_property.string() + 9u, 6u };
    const CStringView expected_substring{
        reinterpret_cast<const std::uint8_t*>("source"), 6u };
    const CNodeKey substring_named = document.create_boolean(true, property_substring);
    TEST_EXPECT(ctx, substring_named.is_valid());
    TEST_EXPECT(ctx, document.name(substring_named) == expected_substring);

    const CStringView cross_domain_source = document.name(named);
    const CStringView new_property{
        reinterpret_cast<const std::uint8_t*>("new-property-for-cross-domain-growth"), 36u };
    const CNodeKey cross_domain = document.create_string(cross_domain_source, new_property);
    TEST_EXPECT(ctx, cross_domain.is_valid());
    TEST_EXPECT(ctx, document.string_value(cross_domain) == property_source);
    TEST_EXPECT(ctx, document.name(cross_domain) == new_property);

    const CStringView string_source{
        reinterpret_cast<const std::uint8_t*>("string-value-source"), 19u };
    const CNodeKey string = document.create_string(string_source);
    TEST_EXPECT(ctx, string.is_valid());
    const CStringView stored_string = document.string_value(string);
    const CStringView string_substring{ stored_string.string() + 13u, 6u };
    const CNodeKey substring_string = document.create_string(string_substring);
    TEST_EXPECT(ctx, substring_string.is_valid());
    TEST_EXPECT(ctx, document.string_value(substring_string) == expected_substring);
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_integrity_rejects_orphan_strings_and_key_corruption(TTestContext& ctx)
{
    const CStringView property_name{
        reinterpret_cast<const std::uint8_t*>("property"), 8u };
    const CStringView string_value{
        reinterpret_cast<const std::uint8_t*>("string"), 6u };
    const CStringView orphan{
        reinterpret_cast<const std::uint8_t*>("orphan"), 6u };

    CLiveDocument orphan_property_document;
    TEST_EXPECT(ctx, orphan_property_document.initialise());
    TEST_EXPECT(ctx, orphan_property_document.create_null(property_name).is_valid());
    TEST_EXPECT(ctx, SLiveDocumentTestAccess::append_orphan_property_name(
        orphan_property_document, orphan));
    TEST_EXPECT(ctx, !orphan_property_document.check_integrity());

    CLiveDocument orphan_string_document;
    TEST_EXPECT(ctx, orphan_string_document.initialise());
    TEST_EXPECT(ctx, orphan_string_document.create_string(string_value).is_valid());
    TEST_EXPECT(ctx, SLiveDocumentTestAccess::append_orphan_string_value(
        orphan_string_document, orphan));
    TEST_EXPECT(ctx, !orphan_string_document.check_integrity());

    CLiveDocument self_document;
    TEST_EXPECT(ctx, self_document.initialise());
    const CNodeKey detached = self_document.create_null();
    TEST_EXPECT(ctx, detached.is_valid());
    SLiveDocumentTestAccess::replace_self_with_root(self_document, detached);
    TEST_EXPECT(ctx, !self_document.check_integrity());

    CLiveDocument monotonic_document;
    TEST_EXPECT(ctx, monotonic_document.initialise());
    const CNodeKey highest = monotonic_document.create_null();
    TEST_EXPECT(ctx, highest.is_valid());
    SLiveDocumentTestAccess::set_next_monotonic_node_key(
        monotonic_document, highest.query_value());
    TEST_EXPECT(ctx, !monotonic_document.check_integrity());
}

void test_numeric_boundaries_metadata_and_negative_zero(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());

    const std::int64_t signed_values[]{ -129, -128, 127, 128, -32769, 32767, 32768,
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) - 1,
        std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max() };
    for (const std::int64_t value : signed_values)
    {
        const CNodeKey key = document.create_signed_integer(value);
        CIntegerMetadata metadata;
        std::int64_t recovered = 0;
        TEST_EXPECT(ctx, key.is_valid());
        TEST_EXPECT(ctx, document.integer_metadata(key, metadata));
        TEST_EXPECT(ctx, metadata.domain == EIntegerDomain::signed_value);
        TEST_EXPECT(ctx, metadata.width == live_signed_integer_smallest_width(value));
        TEST_EXPECT(ctx, document.signed_integer_value(key, recovered) && (recovered == value));
    }

    const std::uint64_t unsigned_values[]{ 0u, 0xffu, 0x100u, 0xffffu, 0x10000u,
        0xffffffffu, 0x100000000ull, std::numeric_limits<std::uint64_t>::max() };
    for (const std::uint64_t value : unsigned_values)
    {
        const CNodeKey key = document.create_unsigned_integer(value);
        CIntegerMetadata metadata;
        std::uint64_t recovered = 0u;
        TEST_EXPECT(ctx, key.is_valid());
        TEST_EXPECT(ctx, document.integer_metadata(key, metadata));
        TEST_EXPECT(ctx, metadata.domain == EIntegerDomain::unsigned_value);
        TEST_EXPECT(ctx, metadata.width == live_unsigned_integer_smallest_width(value));
        TEST_EXPECT(ctx, document.unsigned_integer_value(key, recovered) && (recovered == value));
    }

    const CIntegerMetadata alternate_hex{
        EIntegerDomain::signed_value, EIntegerWidth::bits_8,
        EIntegerNotation::hexadecimal, EIntegerPrefix::alternate };
    TEST_EXPECT(ctx, document.create_signed_integer(42, alternate_hex).is_valid());

    const std::uint32_t before_invalid = document.value_count();
    CIntegerMetadata invalid_width = alternate_hex;
    invalid_width.width = EIntegerWidth::bits_16;
    CIntegerMetadata invalid_prefix = alternate_hex;
    invalid_prefix.notation = EIntegerNotation::binary;
    TEST_EXPECT(ctx, !document.create_signed_integer(42, invalid_width).is_valid());
    TEST_EXPECT(ctx, !document.create_signed_integer(42, invalid_prefix).is_valid());
    TEST_EXPECT(ctx, !document.create_unsigned_integer(42u, alternate_hex).is_valid());
    TEST_EXPECT(ctx, document.value_count() == before_invalid);

    const double negative_zero = live_floating_point_from_bits(0x8000000000000000ull);
    const CNodeKey negative_zero_key = document.create_floating_point(negative_zero);
    double recovered_zero = 1.0;
    TEST_EXPECT(ctx, negative_zero_key.is_valid());
    TEST_EXPECT(ctx, document.floating_point_value(negative_zero_key, recovered_zero));
    TEST_EXPECT(ctx, live_floating_point_bits(recovered_zero) == 0x8000000000000000ull);
    TEST_EXPECT(ctx, !document.create_floating_point(std::numeric_limits<double>::infinity()).is_valid());
    TEST_EXPECT(ctx, !document.create_floating_point(-std::numeric_limits<double>::infinity()).is_valid());
    TEST_EXPECT(ctx, !document.create_floating_point(std::numeric_limits<double>::quiet_NaN()).is_valid());
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_container_pair_failure_atomicity_and_key_gaps(TTestContext& ctx)
{
    SFailingAllocator fixture;
    memory::CMemoryAllocator allocator{ &fixture, &allocate_with_failure, &deallocate_with_failure };
    memory::CMemoryContext context{ allocator };
    tests::TMemoryContextScope scope{ &context };

    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise(32u));
    CNodeKey last;
    for (std::uint32_t index = 0u; index < 29u; ++index)
    {
        last = document.create_null();
        TEST_EXPECT(ctx, last.is_valid());
    }
    TEST_EXPECT(ctx, last.query_value() == 31u);
    TEST_EXPECT(ctx, document.value_count() == 1u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);

    fixture.reject_all = true;
    const CNodeKey failed = document.create_array();
    fixture.reject_all = false;
    TEST_EXPECT(ctx, !failed.is_valid());
    TEST_EXPECT(ctx, document.value_count() == 1u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, document.check_integrity());

    const CNodeKey successful = document.create_array();
    TEST_EXPECT(ctx, successful.query_value() == 34u);
    TEST_EXPECT(ctx, document.value_count() == 1u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, document.check_integrity());
    document.deallocate();
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

void test_initialisation_failure_sweep(TTestContext& ctx)
{
    bool reached_success = false;
    for (std::size_t fail_on = 0u; fail_on < 32u; ++fail_on)
    {
        SFailingAllocator fixture;
        fixture.fail_on = fail_on;
        memory::CMemoryAllocator allocator{ &fixture, &allocate_with_failure, &deallocate_with_failure };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CLiveDocument document;
            const bool success = document.initialise();
            if (success)
            {
                TEST_EXPECT(ctx, document.check_integrity());
                reached_success = true;
            }
            else
            {
                TEST_EXPECT(ctx, !document.is_ready());
                TEST_EXPECT(ctx, document.value_count() == 0u);
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
        if (reached_success)
        {
            break;
        }
    }
    TEST_EXPECT(ctx, reached_success);
}

void test_string_creation_failure_sweep(TTestContext& ctx)
{
    bool reached_success = false;
    for (std::size_t failure_offset = 0u; failure_offset < 32u; ++failure_offset)
    {
        SFailingAllocator fixture;
        memory::CMemoryAllocator allocator{ &fixture, &allocate_with_failure, &deallocate_with_failure };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            CLiveDocument document;
            TEST_EXPECT(ctx, document.initialise());
            const std::uint32_t values_before = document.value_count();
            fixture.fail_on = fixture.attempt + failure_offset;

            const CStringView name{ reinterpret_cast<const std::uint8_t*>("name"), 4u };
            const CStringView value{ reinterpret_cast<const std::uint8_t*>("payload"), 7u };
            const CNodeKey created = document.create_string(value, name);
            if (created.is_valid())
            {
                TEST_EXPECT(ctx, document.value_count() == values_before);
                TEST_EXPECT(ctx, document.name_id(created).query_value() == 1u);
                TEST_EXPECT(ctx, document.string_value_id(created).query_value() == 1u);
                TEST_EXPECT(ctx, document.referenced_property_name_count() == 0u);
                TEST_EXPECT(ctx, document.referenced_string_value_count() == 0u);
                TEST_EXPECT(ctx, document.check_integrity());
                reached_success = true;
            }
            else
            {
                TEST_EXPECT(ctx, document.value_count() == values_before);
                TEST_EXPECT(ctx, document.check_integrity());

                fixture.fail_on = std::numeric_limits<std::size_t>::max();
                const CNodeKey after_failure = document.create_null();
                TEST_EXPECT(ctx, after_failure.is_valid());
                TEST_EXPECT(ctx, (after_failure.query_value() == 3u) ||
                    (after_failure.query_value() == 4u));
                TEST_EXPECT(ctx, document.check_integrity());
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
        if (reached_success)
        {
            break;
        }
    }
    TEST_EXPECT(ctx, reached_success);
}

void test_move_reset_and_retained_attribution(TTestContext& ctx)
{
    static_assert(std::is_nothrow_move_constructible_v<CLiveDocument>);
    static_assert(std::is_nothrow_move_assignable_v<CLiveDocument>);

    SFailingAllocator fixture;
    memory::CMemoryAllocator allocator{ &fixture, &allocate_with_failure, &deallocate_with_failure };
    memory::CMemoryContext source_context{ allocator };
    memory::CMemoryContext other_context{ allocator };

    CLiveDocument source;
    CNodeKey original_string;
    {
        tests::TMemoryContextScope source_scope{ &source_context };
        TEST_EXPECT(ctx, source.initialise());
        original_string = source.create_string(
            CStringView{ reinterpret_cast<const std::uint8_t*>("payload"), 7u });
        TEST_EXPECT(ctx, original_string.is_valid());
    }

    const CNodeKey original_root = source.root();
    const CStringValueId original_string_id = source.string_value_id(original_string);
    const std::uint8_t* const original_string_address = source.string_value(original_string).string();
    const std::uint32_t allocation_count = source.memory_allocation_count();
    const std::uint64_t allocation_size = source.memory_allocation_size();
    TEST_EXPECT(ctx, allocation_count == source_context.get_live_allocation_count());
    TEST_EXPECT(ctx, allocation_size == source_context.get_live_allocated_bytes());
    TEST_EXPECT(ctx, other_context.is_attribution_empty());

    {
        tests::TMemoryContextScope other_scope{ &other_context };
        CLiveDocument moved{ std::move(source) };
        TEST_EXPECT(ctx, !source.is_ready());
        TEST_EXPECT(ctx, moved.root() == original_root);
        TEST_EXPECT(ctx, moved.contains(original_string));
        TEST_EXPECT(ctx, moved.string_value_id(original_string) == original_string_id);
        TEST_EXPECT(ctx, moved.string_value(original_string).string() == original_string_address);
        TEST_EXPECT(ctx, moved.check_integrity());
        TEST_EXPECT(ctx, source_context.get_live_allocation_count() == allocation_count);
        TEST_EXPECT(ctx, source_context.get_live_allocated_bytes() == allocation_size);
        TEST_EXPECT(ctx, other_context.is_attribution_empty());

        CLiveDocument assigned;
        TEST_EXPECT(ctx, assigned.initialise());
        const std::uint32_t other_allocations = other_context.get_live_allocation_count();
        assigned = std::move(moved);
        TEST_EXPECT(ctx, !moved.is_ready());
        TEST_EXPECT(ctx, assigned.root() == original_root);
        TEST_EXPECT(ctx, assigned.contains(original_string));
        TEST_EXPECT(ctx, assigned.string_value_id(original_string) == original_string_id);
        TEST_EXPECT(ctx, assigned.string_value(original_string).string() == original_string_address);
        TEST_EXPECT(ctx, assigned.check_integrity());
        TEST_EXPECT(ctx, other_context.get_live_allocation_count() < other_allocations);
        TEST_EXPECT(ctx, source_context.get_live_allocation_count() == allocation_count);

        assigned = std::move(assigned);
        TEST_EXPECT(ctx, assigned.check_integrity());
        assigned.deallocate();
        TEST_EXPECT(ctx, source_context.is_attribution_empty());
        TEST_EXPECT(ctx, other_context.is_attribution_empty());

        TEST_EXPECT(ctx, moved.initialise());
        TEST_EXPECT(ctx, moved.root().query_value() == 1u);
        TEST_EXPECT(ctx, moved.reset());
        TEST_EXPECT(ctx, moved.root().query_value() == 1u);
        TEST_EXPECT(ctx, moved.check_integrity());
    }
    TEST_EXPECT(ctx, source_context.is_attribution_empty());
    TEST_EXPECT(ctx, other_context.is_attribution_empty());
}

} // namespace

int run_live_document_tests()
{
    TTestContext ctx;
    test_initialisation_root_and_empty_domains(ctx);
    test_detached_creation_and_accessors(ctx);
    test_utf8_normalisation_and_rejection(ctx);
    test_aliased_string_admission(ctx);
    test_integrity_rejects_orphan_strings_and_key_corruption(ctx);
    test_numeric_boundaries_metadata_and_negative_zero(ctx);
    test_container_pair_failure_atomicity_and_key_gaps(ctx);
    test_initialisation_failure_sweep(ctx);
    test_string_creation_failure_sweep(ctx);
    test_move_reset_and_retained_attribution(ctx);

    std::cout << "LiveDocument: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
