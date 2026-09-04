
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
        CLiveNode* const node = document.m_nodes.get_slot(key);
        if (node != nullptr)
        {
            node->m_self = document.m_root;
        }
    }

    static void set_next_monotonic_node_key(CLiveDocument& document, const std::uint64_t key) noexcept
    {
        document.m_next_monotonic_node_key = key;
    }

    static void set_reachable_empty_count(CLiveDocument& document, const std::uint32_t count) noexcept
    {
        document.m_empty_value_count = count;
    }

    static void set_reachable_value_count(CLiveDocument& document, const std::uint32_t count) noexcept
    {
        document.m_value_count = count;
    }

    static void set_property_reference_count(
        CLiveDocument& document,
        const CPropertyNameId id,
        const std::uint32_t count) noexcept
    {
        if (id.is_valid() && (id.query_value() < document.m_property_name_counts.size()))
        {
            document.m_property_name_counts[id.query_value()] = count;
        }
    }

    static void set_string_reference_count(
        CLiveDocument& document,
        const CStringValueId id,
        const std::uint32_t count) noexcept
    {
        if (id.is_valid() && (id.query_value() < document.m_string_value_counts.size()))
        {
            document.m_string_value_counts[id.query_value()] = count;
        }
    }

    [[nodiscard]] static std::uint32_t occupied_node_count(const CLiveDocument& document) noexcept
    {
        return document.m_nodes.occupied_count();
    }

    [[nodiscard]] static std::uint64_t node_storage_allocation_size(
        const CLiveDocument& document) noexcept
    {
        return document.m_nodes.memory_allocation_size();
    }

    [[nodiscard]] static CPropertyNameId aggregate_name_id(
        const CLiveDocument& document, const CNodeKey owner) noexcept
    {
        const CLiveNode* const owner_record = document.value_node(owner);
        const CLiveNode* const aggregate = (owner_record != nullptr) ?
            document.node(owner_record->value_owned_aggregate_key()) : nullptr;
        return (aggregate != nullptr) ? aggregate->name_id() : CPropertyNameId{};
    }

    static void set_next_sibling(
        CLiveDocument& document, const CNodeKey value, const CNodeKey next) noexcept
    {
        CLiveNode* const record = document.value_node(value);
        if (record != nullptr)
        {
            record->m_relation_2 = next;
        }
    }

    static void copy_value_name(
        CLiveDocument& document, const CNodeKey source, const CNodeKey destination) noexcept
    {
        const CLiveNode* const source_record = document.value_node(source);
        CLiveNode* const destination_record = document.value_node(destination);
        if ((source_record != nullptr) && (destination_record != nullptr))
        {
            destination_record->m_name = source_record->m_name;
        }
    }

    static void set_aggregate_kind(
        CLiveDocument& document, const CNodeKey owner, const ELiveAggregateKind kind) noexcept
    {
        CLiveNode* const owner_record = document.value_node(owner);
        if (owner_record != nullptr)
        {
            CLiveNode* const aggregate =
                document.node(owner_record->value_owned_aggregate_key());
            if (aggregate != nullptr)
            {
                aggregate->m_usage.aggregate_kind = kind;
            }
        }
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

struct SRetiringAllocator
{
    struct SAllocation
    {
        void* pointer{ nullptr };
        std::size_t alignment{ 0u };
        std::size_t size{ 0u };
        bool retired{ false };
    };

    static constexpr std::size_t k_max_allocations = 128u;
    SAllocation allocations[k_max_allocations]{};

    void release() noexcept
    {
        for (SAllocation& allocation : allocations)
        {
            if (allocation.pointer != nullptr)
            {
                (void)tests::deallocate_test_memory(
                    nullptr, allocation.alignment, allocation.pointer);
                allocation = SAllocation{};
            }
        }
    }
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

void* MV_STD_ABI_CALL allocate_with_retirement(
    void* const state, const std::size_t alignment, const std::size_t bytes) noexcept
{
    SRetiringAllocator& fixture = *static_cast<SRetiringAllocator*>(state);
    for (SRetiringAllocator::SAllocation& allocation : fixture.allocations)
    {
        if (allocation.pointer == nullptr)
        {
            allocation.pointer = tests::allocate_test_memory(nullptr, alignment, bytes);
            if (allocation.pointer != nullptr)
            {
                allocation.alignment = alignment;
                allocation.size = bytes;
            }
            return allocation.pointer;
        }
    }
    return nullptr;
}

bool MV_STD_ABI_CALL deallocate_with_retirement(
    void* const state, const std::size_t, void* const pointer) noexcept
{
    SRetiringAllocator& fixture = *static_cast<SRetiringAllocator*>(state);
    for (SRetiringAllocator::SAllocation& allocation : fixture.allocations)
    {
        if ((allocation.pointer == pointer) && !allocation.retired)
        {
            std::memset(allocation.pointer, 0xa5, allocation.size);
            allocation.retired = true;
            return true;
        }
    }
    return false;
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
    TEST_EXPECT(ctx, document.is_complete());
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

void test_canonical_string_admission_avoids_normalisation_allocation(TTestContext& ctx)
{
    SFailingAllocator fixture;
    memory::CMemoryAllocator allocator{ &fixture, &allocate_with_failure, &deallocate_with_failure };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        CLiveDocument document;
        TEST_EXPECT(ctx, document.initialise(16u));

        const std::uint8_t literal_nul[]{ 'a', 0u, 'b' };
        const std::uint8_t modified_nul[]{ 'a', 0xc0u, 0x80u, 'b' };
        const CStringView name{ reinterpret_cast<const std::uint8_t*>("name"), 4u };
        TEST_EXPECT(ctx, document.create_string(
            bytes_view(modified_nul, sizeof(modified_nul)), name).is_valid());

        const std::size_t attempts_before = fixture.attempt;
        fixture.reject_all = true;
        const CNodeKey canonical = document.create_string(
            bytes_view(modified_nul, sizeof(modified_nul)), name);
        TEST_EXPECT(ctx, canonical.is_valid());
        TEST_EXPECT(ctx, fixture.attempt == attempts_before);

        const CNodeKey needs_normalisation = document.create_string(
            bytes_view(literal_nul, sizeof(literal_nul)), name);
        TEST_EXPECT(ctx, !needs_normalisation.is_valid());
        TEST_EXPECT(ctx, fixture.attempt == (attempts_before + 1u));
        fixture.reject_all = false;
        TEST_EXPECT(ctx, document.check_integrity());
        document.deallocate();
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
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

void test_cross_domain_alias_survives_property_relocation(TTestContext& ctx)
{
    SRetiringAllocator fixture;
    memory::CMemoryAllocator allocator{
        &fixture, &allocate_with_retirement, &deallocate_with_retirement };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        CLiveDocument document;
        TEST_EXPECT(ctx, document.initialise());

        const CStringView source{
            reinterpret_cast<const std::uint8_t*>("property-source"), 15u };
        const CNodeKey named = document.create_null(source);
        TEST_EXPECT(ctx, named.is_valid());

        const CStringView aliased_value = document.name(named);
        std::uint8_t growing_name_bytes[8192u];
        std::memset(growing_name_bytes, 'n', sizeof(growing_name_bytes));
        const CStringView growing_name{ growing_name_bytes, sizeof(growing_name_bytes) };
        const CNodeKey string = document.create_string(aliased_value, growing_name);
        TEST_EXPECT(ctx, string.is_valid());
        TEST_EXPECT(ctx, document.string_value(string) == source);
        TEST_EXPECT(ctx, document.name(string) == growing_name);
        TEST_EXPECT(ctx, document.check_integrity());
        document.deallocate();
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    fixture.release();
}

void test_integrity_rejects_string_key_and_reference_corruption(TTestContext& ctx)
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
    TEST_EXPECT(ctx, !self_document.contains(detached));
    TEST_EXPECT(ctx, !self_document.check_integrity());

    CLiveDocument monotonic_document;
    TEST_EXPECT(ctx, monotonic_document.initialise());
    const CNodeKey highest = monotonic_document.create_null();
    TEST_EXPECT(ctx, highest.is_valid());
    SLiveDocumentTestAccess::set_next_monotonic_node_key(
        monotonic_document, highest.query_value());
    TEST_EXPECT(ctx, !monotonic_document.check_integrity());

    CLiveDocument reference_document;
    TEST_EXPECT(ctx, reference_document.initialise());
    const CNodeKey named = reference_document.create_null(property_name);
    CNodeKey surviving;
    TEST_EXPECT(ctx, reference_document.append_child(
        reference_document.root(), named, surviving).succeeded());
    TEST_EXPECT(ctx, reference_document.check_integrity());
    SLiveDocumentTestAccess::set_property_reference_count(
        reference_document, reference_document.name_id(named), 0u);
    TEST_EXPECT(ctx, !reference_document.check_integrity());
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

void test_ordinary_topology_accounting_detachment_and_erasure(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());

    const CStringView first_name{ reinterpret_cast<const std::uint8_t*>("first"), 5u };
    const CStringView second_name{ reinterpret_cast<const std::uint8_t*>("second"), 6u };
    const CStringView third_name{ reinterpret_cast<const std::uint8_t*>("third"), 5u };
    const CStringView nested_name{ reinterpret_cast<const std::uint8_t*>("nested"), 6u };
    const CStringView payload{ reinterpret_cast<const std::uint8_t*>("payload"), 7u };

    const CNodeKey first = document.create_signed_integer(1, first_name);
    const CNodeKey second = document.create_array(second_name);
    const CNodeKey third = document.create_string(payload, third_name);
    const CNodeKey anonymous = document.create_boolean(true);
    const CNodeKey named_array_child = document.create_null(nested_name);
    CNodeKey surviving;

    TEST_EXPECT(ctx, document.append_child(second, anonymous, surviving).succeeded());
    TEST_EXPECT(ctx, surviving == anonymous);
    TEST_EXPECT(ctx, document.append_child(second, named_array_child, surviving).succeeded());
    TEST_EXPECT(ctx, document.value_count() == 1u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, document.referenced_property_name_count() == 0u);

    TEST_EXPECT(ctx, document.append_child(document.root(), first, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(document.root(), third, surviving).succeeded());
    TEST_EXPECT(ctx, document.insert_child_at(document.root(), second, 1u, surviving).succeeded());
    TEST_EXPECT(ctx, document.first_child(document.root()) == first);
    TEST_EXPECT(ctx, document.next_sibling(first) == second);
    TEST_EXPECT(ctx, document.next_sibling(second) == third);
    TEST_EXPECT(ctx, document.last_child(document.root()) == third);
    TEST_EXPECT(ctx, document.parent(second) == document.root());
    TEST_EXPECT(ctx, document.child_count(document.root()) == 3u);
    TEST_EXPECT(ctx, document.child_count(second) == 2u);
    TEST_EXPECT(ctx, document.value_count() == 6u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 2u);
    TEST_EXPECT(ctx, document.referenced_property_name_count() == 4u);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 1u);
    TEST_EXPECT(ctx, document.check_integrity());

    TEST_EXPECT(ctx, document.detach(second));
    TEST_EXPECT(ctx, document.is_detached(second));
    TEST_EXPECT(ctx, document.value_count() == 3u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, document.referenced_property_name_count() == 2u);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 1u);
    TEST_EXPECT(ctx, document.check_integrity());

    TEST_EXPECT(ctx, document.insert_child_before(
        document.root(), second, third, surviving).succeeded());
    TEST_EXPECT(ctx, document.next_sibling(first) == second);
    TEST_EXPECT(ctx, document.next_sibling(second) == third);
    TEST_EXPECT(ctx, document.check_integrity());

    const CNodeKey duplicate = document.create_null(first_name);
    const CLiveAttachmentResult duplicate_result =
        document.append_child(document.root(), duplicate, surviving);
    TEST_EXPECT(ctx, duplicate_result.outcome == ELiveAttachmentOutcome::rejected);
    TEST_EXPECT(ctx, duplicate_result.rejection == ELiveAttachmentRejection::duplicate_object_name);
    TEST_EXPECT(ctx, !surviving.is_valid());
    TEST_EXPECT(ctx, document.is_detached(duplicate));
    TEST_EXPECT(ctx, document.check_integrity());

    TEST_EXPECT(ctx, document.erase(second));
    TEST_EXPECT(ctx, !document.contains(second));
    TEST_EXPECT(ctx, !document.contains(anonymous));
    TEST_EXPECT(ctx, !document.contains(named_array_child));
    TEST_EXPECT(ctx, document.value_count() == 3u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, document.check_integrity());

    TEST_EXPECT(ctx, document.erase(document.root()));
    TEST_EXPECT(ctx, document.root().is_valid());
    TEST_EXPECT(ctx, document.child_count(document.root()) == 0u);
    TEST_EXPECT(ctx, document.value_count() == 1u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, document.referenced_property_name_count() == 0u);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 0u);
    TEST_EXPECT(ctx, document.contains(duplicate));
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_deep_iterative_topology_and_root_clear(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise(2048u));
    const CStringView outer_name{ reinterpret_cast<const std::uint8_t*>("outer"), 5u };
    const CNodeKey outer = document.create_array(outer_name);
    CNodeKey parent = outer;
    CNodeKey surviving;
    constexpr std::uint32_t depth = 1000u;
    for (std::uint32_t index = 1u; index < depth; ++index)
    {
        const CNodeKey child = document.create_array();
        TEST_EXPECT(ctx, child.is_valid());
        TEST_EXPECT(ctx, document.append_child(parent, child, surviving).succeeded());
        parent = child;
    }
    const CNodeKey leaf = document.create_string(
        CStringView{ reinterpret_cast<const std::uint8_t*>("leaf"), 4u });
    TEST_EXPECT(ctx, document.append_child(parent, leaf, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(document.root(), outer, surviving).succeeded());
    TEST_EXPECT(ctx, document.value_count() == (depth + 2u));
    TEST_EXPECT(ctx, document.aggregate_payload_count() == (depth + 1u));
    TEST_EXPECT(ctx, document.check_integrity());
    TEST_EXPECT(ctx, document.erase(document.root()));
    TEST_EXPECT(ctx, document.value_count() == 1u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, !document.contains(outer));
    TEST_EXPECT(ctx, !document.contains(leaf));
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_attachment_rejections_and_cycles(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CStringView member_name{ reinterpret_cast<const std::uint8_t*>("member"), 6u };
    const CNodeKey anonymous = document.create_null();
    const CNodeKey named = document.create_null(member_name);
    const CNodeKey array = document.create_array();
    const CNodeKey nested = document.create_array();
    CNodeKey surviving;

    CLiveAttachmentResult result = document.append_child(document.root(), anonymous, surviving);
    TEST_EXPECT(ctx, result.rejection == ELiveAttachmentRejection::object_entry_required);
    TEST_EXPECT(ctx, !surviving.is_valid() && document.is_detached(anonymous));

    result = document.insert_child_before(document.root(), named, CNodeKey{}, surviving);
    TEST_EXPECT(ctx, result.rejection == ELiveAttachmentRejection::insert_before_not_child);
    TEST_EXPECT(ctx, document.is_detached(named));

    result = document.insert_child_at(document.root(), named, 1u, surviving);
    TEST_EXPECT(ctx, result.rejection == ELiveAttachmentRejection::index_out_of_range);
    TEST_EXPECT(ctx, document.is_detached(named));

    TEST_EXPECT(ctx, document.append_child(array, nested, surviving).succeeded());
    result = document.append_child(nested, array, surviving);
    TEST_EXPECT(ctx, result.rejection == ELiveAttachmentRejection::cycle);
    TEST_EXPECT(ctx, document.is_detached(array));

    TEST_EXPECT(ctx, document.append_child(document.root(), named, surviving).succeeded());
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_topology_operations_do_not_allocate(TTestContext& ctx)
{
    SFailingAllocator fixture;
    memory::CMemoryAllocator allocator{ &fixture, &allocate_with_failure, &deallocate_with_failure };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        CLiveDocument document;
        TEST_EXPECT(ctx, document.initialise());
        const CStringView member_name{ reinterpret_cast<const std::uint8_t*>("member"), 6u };
        const CNodeKey candidate = document.create_array(member_name);
        const CNodeKey child = document.create_string(
            CStringView{ reinterpret_cast<const std::uint8_t*>("payload"), 7u });
        CNodeKey surviving;
        TEST_EXPECT(ctx, document.append_child(candidate, child, surviving).succeeded());

        const std::size_t allocation_attempts = fixture.attempt;
        fixture.reject_all = true;
        CLiveAttachmentResult result = document.append_child(document.root(), candidate, surviving);
        TEST_EXPECT(ctx, result.succeeded());
        TEST_EXPECT(ctx, surviving == candidate);
        TEST_EXPECT(ctx, document.detach(candidate));
        TEST_EXPECT(ctx, document.append_child(document.root(), candidate, surviving).succeeded());
        TEST_EXPECT(ctx, document.erase(document.root()));
        fixture.reject_all = false;
        TEST_EXPECT(ctx, fixture.attempt == allocation_attempts);
        TEST_EXPECT(ctx, !document.contains(candidate));
        TEST_EXPECT(ctx, !document.contains(child));
        TEST_EXPECT(ctx, document.value_count() == 1u);
        TEST_EXPECT(ctx, document.check_integrity());
        document.deallocate();
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

void test_integrity_rejects_topology_corruption(TTestContext& ctx)
{
    const CStringView first_name{ reinterpret_cast<const std::uint8_t*>("first"), 5u };
    const CStringView second_name{ reinterpret_cast<const std::uint8_t*>("second"), 6u };

    CLiveDocument sibling_document;
    TEST_EXPECT(ctx, sibling_document.initialise());
    const CNodeKey sibling_first = sibling_document.create_null(first_name);
    const CNodeKey sibling_second = sibling_document.create_null(second_name);
    CNodeKey surviving;
    TEST_EXPECT(ctx, sibling_document.append_child(
        sibling_document.root(), sibling_first, surviving).succeeded());
    TEST_EXPECT(ctx, sibling_document.append_child(
        sibling_document.root(), sibling_second, surviving).succeeded());
    SLiveDocumentTestAccess::set_next_sibling(
        sibling_document, sibling_first, CNodeKey{});
    TEST_EXPECT(ctx, !sibling_document.check_integrity());

    CLiveDocument duplicate_document;
    TEST_EXPECT(ctx, duplicate_document.initialise());
    const CNodeKey duplicate_first = duplicate_document.create_null(first_name);
    const CNodeKey duplicate_second = duplicate_document.create_null(second_name);
    TEST_EXPECT(ctx, duplicate_document.append_child(
        duplicate_document.root(), duplicate_first, surviving).succeeded());
    TEST_EXPECT(ctx, duplicate_document.append_child(
        duplicate_document.root(), duplicate_second, surviving).succeeded());
    SLiveDocumentTestAccess::copy_value_name(
        duplicate_document, duplicate_first, duplicate_second);
    TEST_EXPECT(ctx, !duplicate_document.check_integrity());

    CLiveDocument kind_document;
    TEST_EXPECT(ctx, kind_document.initialise());
    const CNodeKey array = kind_document.create_array(first_name);
    TEST_EXPECT(ctx, kind_document.append_child(
        kind_document.root(), array, surviving).succeeded());
    SLiveDocumentTestAccess::set_aggregate_kind(
        kind_document, array, ELiveAggregateKind::object);
    TEST_EXPECT(ctx, !kind_document.check_integrity());
}

void test_ordinary_aggregate_value_kind_matrix_and_normalized_duplicates(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CStringView array_name{ reinterpret_cast<const std::uint8_t*>("values"), 6u };
    const CStringView entry_name{ reinterpret_cast<const std::uint8_t*>("entry"), 5u };
    const CStringView text_value{ reinterpret_cast<const std::uint8_t*>("text"), 4u };
    const CNodeKey array = document.create_array(array_name);
    const CNodeKey values[]{
        document.create_null(),
        document.create_boolean(true, entry_name),
        document.create_signed_integer(-1),
        document.create_unsigned_integer(1u, entry_name),
        document.create_floating_point(1.5),
        document.create_string(text_value, entry_name),
        document.create_array(),
        document.create_object(entry_name),
    };
    CNodeKey surviving;
    for (const CNodeKey value : values)
    {
        TEST_EXPECT(ctx, value.is_valid());
        TEST_EXPECT(ctx, document.append_child(array, value, surviving).succeeded());
    }
    TEST_EXPECT(ctx, document.is_object_entry(values[1u]));
    TEST_EXPECT(ctx, document.is_object_entry(values[3u]));
    TEST_EXPECT(ctx, document.is_object_entry(values[5u]));
    TEST_EXPECT(ctx, document.is_object_entry(values[7u]));
    TEST_EXPECT(ctx, document.append_child(document.root(), array, surviving).succeeded());
    TEST_EXPECT(ctx, document.child_count(array) == 8u);
    TEST_EXPECT(ctx, document.check_integrity());

    CLiveDocument object_document;
    TEST_EXPECT(ctx, object_document.initialise());
    const CStringView object_names[]{
        CStringView{ reinterpret_cast<const std::uint8_t*>("n"), 1u },
        CStringView{ reinterpret_cast<const std::uint8_t*>("b"), 1u },
        CStringView{ reinterpret_cast<const std::uint8_t*>("i"), 1u },
        CStringView{ reinterpret_cast<const std::uint8_t*>("f"), 1u },
        CStringView{ reinterpret_cast<const std::uint8_t*>("s"), 1u },
        CStringView{ reinterpret_cast<const std::uint8_t*>("a"), 1u },
        CStringView{ reinterpret_cast<const std::uint8_t*>("o"), 1u },
    };
    const CNodeKey object_values[]{
        object_document.create_null(object_names[0u]),
        object_document.create_boolean(false, object_names[1u]),
        object_document.create_signed_integer(7, object_names[2u]),
        object_document.create_floating_point(2.5, object_names[3u]),
        object_document.create_string(text_value, object_names[4u]),
        object_document.create_array(object_names[5u]),
        object_document.create_object(object_names[6u]),
    };
    for (const CNodeKey value : object_values)
    {
        TEST_EXPECT(ctx, object_document.append_child(
            object_document.root(), value, surviving).succeeded());
    }
    TEST_EXPECT(ctx, object_document.child_count(object_document.root()) == 7u);
    TEST_EXPECT(ctx, object_document.check_integrity());

    const std::uint8_t literal_nul[]{ 'd', 0u, 'u', 'p' };
    const std::uint8_t modified_nul[]{ 'd', 0xc0u, 0x80u, 'u', 'p' };
    CLiveDocument normalized_document;
    TEST_EXPECT(ctx, normalized_document.initialise());
    const CNodeKey literal = normalized_document.create_null(
        bytes_view(literal_nul, sizeof(literal_nul)));
    const CNodeKey modified = normalized_document.create_null(
        bytes_view(modified_nul, sizeof(modified_nul)));
    TEST_EXPECT(ctx, normalized_document.append_child(
        normalized_document.root(), literal, surviving).succeeded());
    const CLiveAttachmentResult duplicate = normalized_document.append_child(
        normalized_document.root(), modified, surviving);
    TEST_EXPECT(ctx, duplicate.rejection == ELiveAttachmentRejection::duplicate_object_name);
    TEST_EXPECT(ctx, normalized_document.is_detached(modified));
    TEST_EXPECT(ctx, normalized_document.check_integrity());
}

void test_recovered_aggregate_generic_reachability_accounting(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CStringView recovered_name{
        reinterpret_cast<const std::uint8_t*>("duplicate"), 9u };
    const CNodeKey recovered_owner = document.create_array(recovered_name);
    const CNodeKey first = document.create_signed_integer(1);
    const CNodeKey second = document.create_signed_integer(2);
    CNodeKey surviving;
    TEST_EXPECT(ctx, document.append_child(recovered_owner, first, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(recovered_owner, second, surviving).succeeded());
    SLiveDocumentTestAccess::set_aggregate_kind(
        document, recovered_owner, ELiveAggregateKind::recovered_array);
    TEST_EXPECT(ctx, document.check_integrity());
    TEST_EXPECT(ctx, document.is_canonical());

    TEST_EXPECT(ctx, document.append_child(
        document.root(), recovered_owner, surviving).succeeded());
    TEST_EXPECT(ctx, !document.is_canonical());
    TEST_EXPECT(ctx, document.check_integrity());
    TEST_EXPECT(ctx, document.detach(recovered_owner));
    TEST_EXPECT(ctx, document.is_canonical());
    TEST_EXPECT(ctx, document.check_integrity());
    TEST_EXPECT(ctx, document.erase(recovered_owner));
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_empty_completeness_and_payload_round_trip(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    TEST_EXPECT(ctx, document.is_complete());

    const CStringView before_name{ reinterpret_cast<const std::uint8_t*>("before"), 6u };
    const CStringView slot_name{ reinterpret_cast<const std::uint8_t*>("slot"), 4u };
    const CStringView after_name{ reinterpret_cast<const std::uint8_t*>("after"), 5u };
    const CStringView text{ reinterpret_cast<const std::uint8_t*>("payload"), 7u };
    const CNodeKey before = document.create_null(before_name);
    const CNodeKey payload = document.create_array(slot_name);
    const CNodeKey child = document.create_string(text);
    const CNodeKey after = document.create_null(after_name);
    CNodeKey surviving;
    TEST_EXPECT(ctx, document.append_child(payload, child, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(document.root(), before, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(document.root(), payload, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(document.root(), after, surviving).succeeded());
    TEST_EXPECT(ctx, document.is_complete());
    TEST_EXPECT(ctx, document.child_count(document.root()) == 3u);
    TEST_EXPECT(ctx, document.value_count() == 5u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 2u);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 1u);

    const CNodeKey replacement = document.detach_payload(payload);
    TEST_EXPECT(ctx, replacement.is_valid() && (replacement != payload));
    TEST_EXPECT(ctx, document.value_type(replacement) == ELiveValueType::empty);
    TEST_EXPECT(ctx, document.name(replacement) == slot_name);
    TEST_EXPECT(ctx, document.parent(replacement) == document.root());
    TEST_EXPECT(ctx, document.previous_sibling(replacement) == before);
    TEST_EXPECT(ctx, document.next_sibling(replacement) == after);
    TEST_EXPECT(ctx, document.child_count(document.root()) == 3u);
    TEST_EXPECT(ctx, document.is_detached(payload));
    TEST_EXPECT(ctx, document.name(payload).length() == 0u);
    TEST_EXPECT(ctx, document.value_type(payload) == ELiveValueType::array);
    TEST_EXPECT(ctx, document.first_child(payload) == child);
    TEST_EXPECT(ctx, !document.is_complete());
    TEST_EXPECT(ctx, document.value_count() == 4u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 0u);
    TEST_EXPECT(ctx, document.check_integrity());

    TEST_EXPECT(ctx, document.attach_payload(replacement, payload) == payload);
    TEST_EXPECT(ctx, !document.contains(replacement));
    TEST_EXPECT(ctx, document.parent(payload) == document.root());
    TEST_EXPECT(ctx, document.name(payload) == slot_name);
    TEST_EXPECT(ctx, document.previous_sibling(payload) == before);
    TEST_EXPECT(ctx, document.next_sibling(payload) == after);
    TEST_EXPECT(ctx, document.child_count(document.root()) == 3u);
    TEST_EXPECT(ctx, document.first_child(payload) == child);
    TEST_EXPECT(ctx, document.is_complete());
    TEST_EXPECT(ctx, document.value_count() == 5u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 2u);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 1u);
    TEST_EXPECT(ctx, document.check_integrity());

    const CNodeKey nested_empty = document.create_empty();
    TEST_EXPECT(ctx, document.value_type(nested_empty) == ELiveValueType::empty);
    TEST_EXPECT(ctx, document.is_complete());
    TEST_EXPECT(ctx, document.append_child(payload, nested_empty, surviving).succeeded());
    TEST_EXPECT(ctx, !document.is_complete());
    TEST_EXPECT(ctx, document.detach(nested_empty));
    TEST_EXPECT(ctx, document.is_complete());
    TEST_EXPECT(ctx, document.append_child(payload, nested_empty, surviving).succeeded());
    TEST_EXPECT(ctx, !document.is_complete());
    const CNodeKey null_payload = document.create_null();
    TEST_EXPECT(ctx, document.attach_payload(nested_empty, null_payload) == null_payload);
    TEST_EXPECT(ctx, !document.contains(nested_empty));
    TEST_EXPECT(ctx, document.parent(null_payload) == payload);
    TEST_EXPECT(ctx, document.is_complete());
    TEST_EXPECT(ctx, document.check_integrity());

    const CStringView final_empty_name{
        reinterpret_cast<const std::uint8_t*>("final-empty"), 11u };
    const CNodeKey final_empty = document.create_empty(final_empty_name);
    TEST_EXPECT(ctx, document.append_child(
        document.root(), final_empty, surviving).succeeded());
    TEST_EXPECT(ctx, !document.is_complete());
    TEST_EXPECT(ctx, document.erase(document.root()));
    TEST_EXPECT(ctx, document.is_complete());
    TEST_EXPECT(ctx, document.value_count() == 1u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_scalar_and_detached_payload_transfer(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CStringView name{ reinterpret_cast<const std::uint8_t*>("text"), 4u };
    const CStringView value{ reinterpret_cast<const std::uint8_t*>("value"), 5u };
    const CNodeKey string = document.create_string(value, name);
    CNodeKey surviving;
    TEST_EXPECT(ctx, document.append_child(document.root(), string, surviving).succeeded());
    const CStringValueId value_id = document.string_value_id(string);
    TEST_EXPECT(ctx, document.referenced_property_name_count() == 1u);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 1u);

    const CNodeKey replacement = document.detach_payload(string);
    TEST_EXPECT(ctx, document.string_value_id(string) == value_id);
    TEST_EXPECT(ctx, document.string_value(string) == value);
    TEST_EXPECT(ctx, document.name(string).length() == 0u);
    TEST_EXPECT(ctx, document.referenced_property_name_count() == 1u);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 0u);
    TEST_EXPECT(ctx, document.attach_payload(replacement, string) == string);
    TEST_EXPECT(ctx, document.name(string) == name);
    TEST_EXPECT(ctx, document.string_value_id(string) == value_id);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 1u);

    const CStringView detached_name{
        reinterpret_cast<const std::uint8_t*>("detached"), 8u };
    const CNodeKey detached = document.create_boolean(true, detached_name);
    const CNodeKey detached_empty = document.detach_payload(detached);
    TEST_EXPECT(ctx, document.is_detached(detached));
    TEST_EXPECT(ctx, document.is_detached(detached_empty));
    TEST_EXPECT(ctx, document.name(detached).length() == 0u);
    TEST_EXPECT(ctx, document.name(detached_empty) == detached_name);
    TEST_EXPECT(ctx, document.attach_payload(detached_empty, detached) == detached);
    bool boolean = false;
    TEST_EXPECT(ctx, document.boolean_value(detached, boolean) && boolean);
    TEST_EXPECT(ctx, document.name(detached) == detached_name);
    TEST_EXPECT(ctx, !document.contains(detached_empty));

    const CNodeKey signed_value = document.create_signed_integer(-17);
    const CNodeKey unsigned_value = document.create_unsigned_integer(42u);
    const CNodeKey floating_value = document.create_floating_point(3.5);
    const CNodeKey scalar_values[]{ signed_value, unsigned_value, floating_value };
    for (const CNodeKey scalar : scalar_values)
    {
        const CNodeKey scalar_empty = document.detach_payload(scalar);
        TEST_EXPECT(ctx, scalar_empty.is_valid());
        TEST_EXPECT(ctx, document.attach_payload(scalar_empty, scalar) == scalar);
    }
    std::int64_t signed_result = 0;
    std::uint64_t unsigned_result = 0u;
    double floating_result = 0.0;
    TEST_EXPECT(ctx, document.signed_integer_value(signed_value, signed_result) &&
        (signed_result == -17));
    TEST_EXPECT(ctx, document.unsigned_integer_value(unsigned_value, unsigned_result) &&
        (unsigned_result == 42u));
    TEST_EXPECT(ctx, document.floating_point_value(floating_value, floating_result) &&
        (floating_result == 3.5));
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_payload_detach_reacquires_container_after_node_growth(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CStringView before_name{ reinterpret_cast<const std::uint8_t*>("before"), 6u };
    const CStringView source_name{ reinterpret_cast<const std::uint8_t*>("source"), 6u };
    const CStringView after_name{ reinterpret_cast<const std::uint8_t*>("after"), 5u };
    const CStringView text{ reinterpret_cast<const std::uint8_t*>("text"), 4u };
    const CNodeKey before = document.create_null(before_name);
    const CNodeKey source = document.create_array(source_name);
    const CNodeKey child = document.create_string(text);
    const CNodeKey after = document.create_null(after_name);
    CNodeKey surviving;
    TEST_EXPECT(ctx, document.append_child(source, child, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(document.root(), before, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(document.root(), source, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(document.root(), after, surviving).succeeded());

    while (SLiveDocumentTestAccess::occupied_node_count(document) < 32u)
    {
        TEST_EXPECT(ctx, document.create_null().is_valid());
    }
    TEST_EXPECT(ctx, SLiveDocumentTestAccess::occupied_node_count(document) == 32u);
    const std::uint64_t allocation_size_before =
        SLiveDocumentTestAccess::node_storage_allocation_size(document);
    const CNodeKey replacement = document.detach_payload(source);
    TEST_EXPECT(ctx, replacement.is_valid());
    TEST_EXPECT(ctx,
        SLiveDocumentTestAccess::node_storage_allocation_size(document) > allocation_size_before);
    TEST_EXPECT(ctx, document.name(replacement) == source_name);
    TEST_EXPECT(ctx, document.name(source).length() == 0u);
    TEST_EXPECT(ctx,
        SLiveDocumentTestAccess::aggregate_name_id(document, source).query_value() ==
            CPropertyNameId::k_empty_value);
    TEST_EXPECT(ctx, document.is_detached(source));
    TEST_EXPECT(ctx, document.first_child(source) == child);
    TEST_EXPECT(ctx, document.parent(replacement) == document.root());
    TEST_EXPECT(ctx, document.previous_sibling(replacement) == before);
    TEST_EXPECT(ctx, document.next_sibling(replacement) == after);
    TEST_EXPECT(ctx, document.child_count(document.root()) == 3u);
    TEST_EXPECT(ctx, document.value_count() == 4u);
    TEST_EXPECT(ctx, document.aggregate_payload_count() == 1u);
    TEST_EXPECT(ctx, document.referenced_property_name_count() == 3u);
    TEST_EXPECT(ctx, document.referenced_string_value_count() == 0u);
    TEST_EXPECT(ctx, !document.is_complete());
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_internal_accounting_failure_marks_document_known_bad(TTestContext& ctx)
{
#if MV_DEVELOPMENT_BUILD
    (void)ctx;
#else
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CStringView source_name{ reinterpret_cast<const std::uint8_t*>("source"), 6u };
    const CStringView text{ reinterpret_cast<const std::uint8_t*>("text"), 4u };
    const CNodeKey source = document.create_array(source_name);
    const CNodeKey child = document.create_string(text);
    CNodeKey surviving;
    TEST_EXPECT(ctx, document.append_child(source, child, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(document.root(), source, surviving).succeeded());
    const CStringValueId string_id = document.string_value_id(child);
    SLiveDocumentTestAccess::set_string_reference_count(document, string_id, 0u);

    TEST_EXPECT(ctx, !document.detach(source));
    TEST_EXPECT(ctx, !document.is_ready());
    TEST_EXPECT(ctx, !document.check_integrity());
    TEST_EXPECT(ctx, !document.create_null().is_valid());
    TEST_EXPECT(ctx, document.reset());
    TEST_EXPECT(ctx, document.is_ready());
    TEST_EXPECT(ctx, document.check_integrity());
#endif
}

void test_reachable_total_underflow_marks_document_known_bad(TTestContext& ctx)
{
#if MV_DEVELOPMENT_BUILD
    (void)ctx;
#else
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CStringView source_name{ reinterpret_cast<const std::uint8_t*>("source"), 6u };
    const CNodeKey source = document.create_array(source_name);
    const CNodeKey child = document.create_null();
    CNodeKey surviving;
    TEST_EXPECT(ctx, document.append_child(source, child, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(document.root(), source, surviving).succeeded());
    SLiveDocumentTestAccess::set_reachable_value_count(document, 1u);

    TEST_EXPECT(ctx, !document.detach(source));
    TEST_EXPECT(ctx, !document.is_ready());
    TEST_EXPECT(ctx, !document.check_integrity());
    TEST_EXPECT(ctx, !document.create_null().is_valid());
    TEST_EXPECT(ctx, document.reset());
    TEST_EXPECT(ctx, document.is_ready());
    TEST_EXPECT(ctx, document.check_integrity());
#endif
}

void test_lateral_reachability_cycle_marks_document_known_bad(TTestContext& ctx)
{
#if MV_DEVELOPMENT_BUILD
    (void)ctx;
#else
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CStringView source_name{ reinterpret_cast<const std::uint8_t*>("source"), 6u };
    const CNodeKey source = document.create_array(source_name);
    const CNodeKey first = document.create_null();
    const CNodeKey last = document.create_null();
    CNodeKey surviving;
    TEST_EXPECT(ctx, document.append_child(source, first, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(source, last, surviving).succeeded());
    SLiveDocumentTestAccess::set_next_sibling(document, last, first);

    const CLiveAttachmentResult result =
        document.append_child(document.root(), source, surviving);
    TEST_EXPECT(ctx, !result.succeeded());
    TEST_EXPECT(ctx, !surviving.is_valid());
    TEST_EXPECT(ctx, !document.is_ready());
    TEST_EXPECT(ctx, !document.check_integrity());
    TEST_EXPECT(ctx, !document.create_null().is_valid());
    TEST_EXPECT(ctx, document.reset());
    TEST_EXPECT(ctx, document.is_ready());
    TEST_EXPECT(ctx, document.check_integrity());
#endif
}

void test_payload_transfer_rejections_cycles_and_recovery(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    TEST_EXPECT(ctx, !document.detach_payload(document.root()).is_valid());

    const CNodeKey empty = document.create_empty();
    TEST_EXPECT(ctx, !document.detach_payload(empty).is_valid());
    const CNodeKey nonempty_target = document.create_null();
    const CNodeKey payload = document.create_null();
    TEST_EXPECT(ctx, !document.attach_payload(nonempty_target, payload).is_valid());

    const CStringView named_value{
        reinterpret_cast<const std::uint8_t*>("named"), 5u };
    const CNodeKey named_payload = document.create_null(named_value);
    TEST_EXPECT(ctx, !document.attach_payload(empty, named_payload).is_valid());

    const CNodeKey attached_payload = document.create_null(named_value);
    CNodeKey surviving;
    TEST_EXPECT(ctx, document.append_child(document.root(), attached_payload, surviving).succeeded());
    TEST_EXPECT(ctx, !document.attach_payload(empty, attached_payload).is_valid());

    const CNodeKey cycle_payload = document.create_array();
    const CNodeKey cycle_target = document.create_empty();
    TEST_EXPECT(ctx, document.append_child(cycle_payload, cycle_target, surviving).succeeded());
    TEST_EXPECT(ctx, !document.attach_payload(cycle_target, cycle_payload).is_valid());
    TEST_EXPECT(ctx, document.parent(cycle_target) == cycle_payload);
    TEST_EXPECT(ctx, document.is_detached(cycle_payload));

    const CStringView recovery_name{
        reinterpret_cast<const std::uint8_t*>("recovered"), 9u };
    const CNodeKey recovered = document.create_array(recovery_name);
    const CNodeKey first = document.create_null();
    const CNodeKey second = document.create_null();
    TEST_EXPECT(ctx, document.append_child(recovered, first, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(recovered, second, surviving).succeeded());
    SLiveDocumentTestAccess::set_aggregate_kind(
        document, recovered, ELiveAggregateKind::recovered_array);
    TEST_EXPECT(ctx, document.append_child(document.root(), recovered, surviving).succeeded());
    const CNodeKey recovered_empty = document.detach_payload(recovered);
    TEST_EXPECT(ctx, recovered_empty.is_valid());
    TEST_EXPECT(ctx, document.name(recovered).length() == 0u);
    TEST_EXPECT(ctx, document.check_integrity());

    TEST_EXPECT(ctx, document.attach_payload(recovered_empty, recovered) == recovered);
    TEST_EXPECT(ctx, !document.is_canonical());
    TEST_EXPECT(ctx, document.check_integrity());

    const CNodeKey second_recovered_empty = document.detach_payload(recovered);
    TEST_EXPECT(ctx, second_recovered_empty.is_valid());
    const CStringView holder_name{
        reinterpret_cast<const std::uint8_t*>("holder"), 6u };
    const CNodeKey holder = document.create_array(holder_name);
    const CNodeKey anonymous_target = document.create_empty();
    TEST_EXPECT(ctx, document.append_child(holder, anonymous_target, surviving).succeeded());
    TEST_EXPECT(ctx, document.append_child(document.root(), holder, surviving).succeeded());
    TEST_EXPECT(ctx, document.attach_payload(anonymous_target, recovered) == recovered);
    TEST_EXPECT(ctx, document.name(recovered).length() == 0u);
    TEST_EXPECT(ctx, document.parent(recovered) == holder);
    TEST_EXPECT(ctx, !document.is_canonical());
    TEST_EXPECT(ctx, document.check_integrity());

    CLiveDocument local;
    CLiveDocument foreign;
    TEST_EXPECT(ctx, local.initialise());
    TEST_EXPECT(ctx, foreign.initialise());
    TEST_EXPECT(ctx, foreign.create_array().is_valid());
    const CNodeKey foreign_only = foreign.create_null();
    TEST_EXPECT(ctx, !local.detach_payload(foreign_only).is_valid());
    TEST_EXPECT(ctx, !local.attach_payload(foreign_only, local.create_null()).is_valid());

    CLiveDocument limits;
    TEST_EXPECT(ctx, limits.initialise());
    const CStringView limit_name{ reinterpret_cast<const std::uint8_t*>("limit"), 5u };
    const CNodeKey limit_target = limits.create_empty(limit_name);
    TEST_EXPECT(ctx, limits.append_child(limits.root(), limit_target, surviving).succeeded());
    TEST_EXPECT(ctx, limits.check_integrity());

    SLiveDocumentTestAccess::set_reachable_empty_count(limits, 0u);
    TEST_EXPECT(ctx, !limits.check_integrity());
    SLiveDocumentTestAccess::set_reachable_empty_count(limits, 1u);
    TEST_EXPECT(ctx, limits.check_integrity());

    CLiveDocument identity_limit;
    TEST_EXPECT(ctx, identity_limit.initialise());
    const CNodeKey identity_source = identity_limit.create_null();
    SLiveDocumentTestAccess::set_next_monotonic_node_key(identity_limit, 0u);
    TEST_EXPECT(ctx, !identity_limit.detach_payload(identity_source).is_valid());
    TEST_EXPECT(ctx, identity_limit.contains(identity_source));
}

void test_payload_transfer_allocation_limits_depth_move_and_attribution(TTestContext& ctx)
{
    SFailingAllocator fixture;
    memory::CMemoryAllocator allocator{ &fixture, &allocate_with_failure, &deallocate_with_failure };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        CLiveDocument document;
        TEST_EXPECT(ctx, document.initialise(2u));
        const CStringView name{ reinterpret_cast<const std::uint8_t*>("source"), 6u };
        const CNodeKey source = document.create_null(name);
        const CNodeKey no_allocate_target = document.create_empty();
        const CNodeKey no_allocate_payload = document.create_null();
        bool reached_capacity = false;
        for (std::uint32_t probe = 0u; probe < 4096u; ++probe)
        {
            fixture.reject_all = true;
            const CNodeKey filler = document.create_null();
            fixture.reject_all = false;
            if (!filler.is_valid())
            {
                reached_capacity = true;
                break;
            }
        }
        TEST_EXPECT(ctx, reached_capacity);
        fixture.reject_all = true;
        const CNodeKey replacement = document.detach_payload(source);
        TEST_EXPECT(ctx, !replacement.is_valid());
        TEST_EXPECT(ctx, document.name(source) == name);
        TEST_EXPECT(ctx, document.is_detached(source));
        const std::size_t attempts_before_attach = fixture.attempt;
        TEST_EXPECT(ctx,
            document.attach_payload(no_allocate_target, no_allocate_payload) == no_allocate_payload);
        TEST_EXPECT(ctx, fixture.attempt == attempts_before_attach);
        fixture.reject_all = false;
        TEST_EXPECT(ctx, document.check_integrity());
        document.deallocate();
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());

    CLiveDocument deep;
    TEST_EXPECT(ctx, deep.initialise(2048u));
    const CStringView outer_name{ reinterpret_cast<const std::uint8_t*>("outer"), 5u };
    const CNodeKey outer = deep.create_array(outer_name);
    CNodeKey parent = outer;
    CNodeKey surviving;
    constexpr std::uint32_t depth = 1000u;
    for (std::uint32_t index = 1u; index < depth; ++index)
    {
        const CNodeKey child = deep.create_array();
        TEST_EXPECT(ctx, deep.append_child(parent, child, surviving).succeeded());
        parent = child;
    }
    const CNodeKey leaf = deep.create_empty();
    TEST_EXPECT(ctx, deep.append_child(parent, leaf, surviving).succeeded());
    TEST_EXPECT(ctx, deep.append_child(deep.root(), outer, surviving).succeeded());
    TEST_EXPECT(ctx, !deep.is_complete());
    const CNodeKey outer_empty = deep.detach_payload(outer);
    TEST_EXPECT(ctx, outer_empty.is_valid());
    TEST_EXPECT(ctx, !deep.is_complete());
    TEST_EXPECT(ctx, deep.value_count() == 2u);
    TEST_EXPECT(ctx, deep.attach_payload(outer_empty, outer) == outer);
    TEST_EXPECT(ctx, !deep.is_complete());
    TEST_EXPECT(ctx, deep.value_count() == (depth + 2u));
    TEST_EXPECT(ctx, deep.check_integrity());

    CLiveDocument moved{ std::move(deep) };
    TEST_EXPECT(ctx, !deep.is_ready());
    TEST_EXPECT(ctx, !moved.is_complete());
    TEST_EXPECT(ctx, moved.check_integrity());
    TEST_EXPECT(ctx, moved.reset());
    TEST_EXPECT(ctx, moved.is_complete());
    TEST_EXPECT(ctx, moved.check_integrity());
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
    test_canonical_string_admission_avoids_normalisation_allocation(ctx);
    test_aliased_string_admission(ctx);
    test_cross_domain_alias_survives_property_relocation(ctx);
    test_integrity_rejects_string_key_and_reference_corruption(ctx);
    test_numeric_boundaries_metadata_and_negative_zero(ctx);
    test_container_pair_failure_atomicity_and_key_gaps(ctx);
    test_initialisation_failure_sweep(ctx);
    test_string_creation_failure_sweep(ctx);
    test_ordinary_topology_accounting_detachment_and_erasure(ctx);
    test_deep_iterative_topology_and_root_clear(ctx);
    test_attachment_rejections_and_cycles(ctx);
    test_topology_operations_do_not_allocate(ctx);
    test_integrity_rejects_topology_corruption(ctx);
    test_ordinary_aggregate_value_kind_matrix_and_normalized_duplicates(ctx);
    test_recovered_aggregate_generic_reachability_accounting(ctx);
    test_empty_completeness_and_payload_round_trip(ctx);
    test_scalar_and_detached_payload_transfer(ctx);
    test_payload_detach_reacquires_container_after_node_growth(ctx);
    test_internal_accounting_failure_marks_document_known_bad(ctx);
    test_reachable_total_underflow_marks_document_known_bad(ctx);
    test_lateral_reachability_cycle_marks_document_known_bad(ctx);
    test_payload_transfer_rejections_cycles_and_recovery(ctx);
    test_payload_transfer_allocation_limits_depth_move_and_attribution(ctx);
    test_move_reset_and_retained_attribution(ctx);

    std::cout << "LiveDocument: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
