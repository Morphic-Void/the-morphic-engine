//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)

#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>

#include "data_model/live_document.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"

namespace
{
using TTestContext = tests::TTestContext;

CStringView text(const char* const value) noexcept
{
    return CStringView{ value };
}

void test_layout_and_scalars(TTestContext& ctx)
{
    static_assert(sizeof(CJsonSlot) == 64u);
    static_assert(std::is_trivially_copyable_v<CJsonSlot>);

    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise(4u));
    const CNodeKey null_node = document.create_null();
    const CNodeKey bool_node = document.create_boolean(true);
    const CNodeKey integer_node = document.create_integer(-37);
    const CNodeKey float_node = document.create_floating_point(2.5);
    const CNodeKey string_node = document.create_string(text("value"));
    TEST_EXPECT(ctx, null_node.is_valid());
    TEST_EXPECT(ctx, bool_node.is_valid());
    TEST_EXPECT(ctx, integer_node.is_valid());
    TEST_EXPECT(ctx, float_node.is_valid());
    TEST_EXPECT(ctx, string_node.is_valid());
    TEST_EXPECT(ctx, document.node_type(null_node) == EJsonNodeType::null_value);
    bool bool_value = false;
    std::int64_t integer_value = 0;
    double float_value = 0.0;
    TEST_EXPECT(ctx, document.boolean_value(bool_node, bool_value) && bool_value);
    TEST_EXPECT(ctx, document.integer_value(integer_node, integer_value) && (integer_value == -37));
    TEST_EXPECT(ctx, document.floating_point_value(float_node, float_value) && (float_value == 2.5));
    TEST_EXPECT(ctx, document.string_value(string_node).length() == 5u);
    TEST_EXPECT(ctx, std::memcmp(document.string_value(string_node).string(), "value", 5u) == 0);
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_live_floating_workspace_accepts_non_finite_values(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CNodeKey nan = document.create_floating_point(std::numeric_limits<double>::quiet_NaN());
    const CNodeKey positive_infinity = document.create_floating_point(std::numeric_limits<double>::infinity());
    const CNodeKey negative_infinity = document.create_floating_point(-std::numeric_limits<double>::infinity());
    double value = 0.0;
    TEST_EXPECT(ctx, document.floating_point_value(nan, value) && (value != value));
    TEST_EXPECT(ctx, document.floating_point_value(positive_infinity, value) &&
        (value == std::numeric_limits<double>::infinity()));
    TEST_EXPECT(ctx, document.floating_point_value(negative_infinity, value) &&
        (value == -std::numeric_limits<double>::infinity()));
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_numeric_intent_and_widths(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CJsonIntegerMetadata explicit_hex{
        EJsonIntegerSign::signed_value, EJsonIntegerWidth::bits_8,
        EJsonIntegerNotation::hexadecimal, EJsonIntegerPrefix::alternate };
    const CJsonIntegerMetadata unsigned_binary{
        EJsonIntegerSign::unsigned_value, EJsonIntegerWidth::bits_64,
        EJsonIntegerNotation::binary, EJsonIntegerPrefix::standard };
    const CNodeKey signed_hex = document.create_integer(127, explicit_hex);
    const CNodeKey maximum = document.create_unsigned_integer(std::numeric_limits<std::uint64_t>::max(), unsigned_binary);
    const CNodeKey negative = document.create_integer(-128);
    const CNodeKey minimum = document.create_integer(std::numeric_limits<std::int64_t>::min());
    CJsonIntegerMetadata metadata;
    std::int64_t signed_value = 0;
    std::uint64_t unsigned_value = 0u;
    TEST_EXPECT(ctx, document.integer_metadata(signed_hex, metadata));
    TEST_EXPECT(ctx, metadata.sign == EJsonIntegerSign::signed_value);
    TEST_EXPECT(ctx, metadata.width == EJsonIntegerWidth::bits_8);
    TEST_EXPECT(ctx, metadata.notation == EJsonIntegerNotation::hexadecimal);
    TEST_EXPECT(ctx, metadata.prefix == EJsonIntegerPrefix::alternate);
    TEST_EXPECT(ctx, document.integer_value(signed_hex, signed_value) && (signed_value == 127));
    TEST_EXPECT(ctx, document.unsigned_integer_value(maximum, unsigned_value) &&
        (unsigned_value == std::numeric_limits<std::uint64_t>::max()));
    TEST_EXPECT(ctx, !document.integer_value(maximum, signed_value));
    TEST_EXPECT(ctx, document.integer_metadata(negative, metadata));
    TEST_EXPECT(ctx, metadata.sign == EJsonIntegerSign::signed_value);
    TEST_EXPECT(ctx, metadata.width == EJsonIntegerWidth::bits_8);
    TEST_EXPECT(ctx, document.integer_value(minimum, signed_value) &&
        (signed_value == std::numeric_limits<std::int64_t>::min()));
    TEST_EXPECT(ctx, document.integer_metadata(minimum, metadata));
    TEST_EXPECT(ctx, metadata.sign == EJsonIntegerSign::signed_value);
    TEST_EXPECT(ctx, metadata.width == EJsonIntegerWidth::bits_64);
    TEST_EXPECT(ctx, !document.is_canonical());
    TEST_EXPECT(ctx, document.requires_morphic_json_extensions());
    const CJsonIntegerMetadata wrong_width{
        EJsonIntegerSign::unsigned_value, EJsonIntegerWidth::bits_16,
        EJsonIntegerNotation::decimal, EJsonIntegerPrefix::standard };
    TEST_EXPECT(ctx, !document.create_integer(127, wrong_width).is_valid());
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_array_mutation_and_cursor(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CNodeKey array = document.create_array();
    const CNodeKey first = document.create_integer(1);
    const CNodeKey last = document.create_integer(3);
    const CNodeKey middle = document.create_integer(2);
    TEST_EXPECT(ctx, document.set_root(array));
    TEST_EXPECT(ctx, document.append_array_child(array, first));
    TEST_EXPECT(ctx, document.append_array_child(array, last));
    TEST_EXPECT(ctx, document.insert_array_child_before(array, last, middle));
    TEST_EXPECT(ctx, document.child_count(array) == 3u);
    TEST_EXPECT(ctx, document.array_at(array, 0u) == first);
    TEST_EXPECT(ctx, document.array_at(array, 1u) == middle);
    TEST_EXPECT(ctx, document.array_at(array, 2u) == last);
    TEST_EXPECT(ctx, !document.previous_sibling(first).is_valid());
    TEST_EXPECT(ctx, !document.next_sibling(last).is_valid());

    CArrayCursor cursor;
    TEST_EXPECT(ctx, document.array_cursor_at(array, 0u, cursor));
    TEST_EXPECT(ctx, cursor.current == first);
    TEST_EXPECT(ctx, document.array_cursor_next(cursor));
    TEST_EXPECT(ctx, cursor.current == middle);
    TEST_EXPECT(ctx, document.detach(middle));
    TEST_EXPECT(ctx, !document.array_cursor_next(cursor));
    TEST_EXPECT(ctx, document.child_count(array) == 2u);
    TEST_EXPECT(ctx, document.first_child(array) == first);
    TEST_EXPECT(ctx, document.last_child(array) == last);
    TEST_EXPECT(ctx, document.parent(middle) == CNodeKey{});
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_object_names_and_rejected_moves(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CNodeKey object = document.create_object();
    const CNodeKey enabled = document.create_boolean(true);
    const CNodeKey duplicate = document.create_boolean(false);
    const CNodeKey array = document.create_array();
    TEST_EXPECT(ctx, document.set_root(object));
    TEST_EXPECT(ctx, document.add_object_child(object, text("enabled"), enabled));
    TEST_EXPECT(ctx, !document.add_object_child(object, text("enabled"), duplicate));
    TEST_EXPECT(ctx, !document.append_array_child(array, enabled));
    const CPropertyNameId enabled_name = document.intern_property_name(text("enabled"));
    TEST_EXPECT(ctx, document.object_child(object, enabled_name) == enabled);
    TEST_EXPECT(ctx, document.child_count(object) == 1u);
    TEST_EXPECT(ctx, document.parent(duplicate) == CNodeKey{});
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_attachment_rejects_cycles(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CNodeKey root = document.create_null();
    const CNodeKey outer = document.create_array();
    const CNodeKey inner = document.create_array();
    TEST_EXPECT(ctx, document.set_root(root));
    TEST_EXPECT(ctx, !document.append_array_child(outer, outer));
    TEST_EXPECT(ctx, document.append_array_child(outer, inner));
    TEST_EXPECT(ctx, !document.append_array_child(inner, outer));
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_stale_key_rejection(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise(1u));
    const CNodeKey stale = document.create_null();
    TEST_EXPECT(ctx, stale.is_valid());
    TEST_EXPECT(ctx, document.erase_detached(stale));
    const CNodeKey replacement = document.create_null();
    TEST_EXPECT(ctx, replacement.is_valid());
    TEST_EXPECT(ctx, replacement != stale);
    TEST_EXPECT(ctx, document.node_type(stale) == EJsonNodeType::invalid);
    TEST_EXPECT(ctx, document.node_type(replacement) == EJsonNodeType::null_value);
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_move_preserves_content_and_attribution(TTestContext& ctx)
{
    static_assert(std::is_nothrow_move_constructible_v<CLiveDocument>);
    static_assert(std::is_nothrow_move_assignable_v<CLiveDocument>);
    static_assert(!std::is_copy_constructible_v<CLiveDocument>);
    static_assert(!std::is_copy_assignable_v<CLiveDocument>);
    tests::TAllocatorFixture state;
    const tests::TModuleIdScope module(module_ids::executable);
    memory::CMemoryAllocator allocator(&state, tests::allocate_test_memory, tests::deallocate_test_memory, system_ids::host);
    memory::CMemoryContext source_context(allocator, system_ids::host);
    memory::CMemoryContext destination_context(allocator, system_ids::host);
    {
        const tests::TMemoryContextScope source_scope(&source_context);
        CLiveDocument source;
        TEST_EXPECT(ctx, source.initialise());
        TEST_EXPECT(ctx, source.set_root(source.create_object()));
        const CNodeKey array = source.create_duplicate_array();
        const CNodeKey first = source.create_string(text("retained"));
        const CNodeKey second = source.create_integer(2);
        TEST_EXPECT(ctx, source.add_object_child(source.root(), text("values"), array));
        TEST_EXPECT(ctx, source.append_array_child(array, first));
        TEST_EXPECT(ctx, source.append_array_child(array, second));
        const CNodeKey root = source.root();
        const CPropertyNameId name = source.name_in_parent(array);
        const CStringView name_view = source.property_name(name);
        const CStringView value_view = source.string_value(first);
        CArrayCursor cursor;
        TEST_EXPECT(ctx, source.array_cursor_at(array, 0u, cursor));
        const tests::TMemoryContextScope destination_scope(&destination_context);
        CLiveDocument destination;
        TEST_EXPECT(ctx, destination.initialise());
        TEST_EXPECT(ctx, destination.set_root(destination.create_string(text("discarded"))));
        const auto source_allocations = source_context.get_live_allocation_count();
        const auto source_bytes = source_context.get_live_allocated_bytes();
        TEST_EXPECT(ctx, !destination_context.is_attribution_empty());
        state.reject_allocation = true;
        {
            const tests::TMemoryContextScope scope(&destination_context);
            CLiveDocument moved(std::move(source));
            TEST_EXPECT(ctx, source.is_empty() && !source.root().is_valid());
            TEST_EXPECT(ctx, source.is_valid() && !source.is_ready());
            source.deallocate();
            TEST_EXPECT(ctx, moved.root() == root);
            TEST_EXPECT(ctx, moved.contains_recovered_duplicate_arrays());
            TEST_EXPECT(ctx, moved.object_child(root, name) == array);
            TEST_EXPECT(ctx, moved.string_value(first).string() == value_view.string());
            TEST_EXPECT(ctx, moved.property_name(name).string() == name_view.string());
            TEST_EXPECT(ctx, moved.array_cursor_next(cursor) && cursor.current == second);
            TEST_EXPECT(ctx, moved.array_cursor_at(array, 0u, cursor));
            CLiveDocument& alias = moved;
            moved = std::move(alias);
            TEST_EXPECT(ctx, moved.root() == root && moved.check_integrity());
            TEST_EXPECT(ctx, moved.array_cursor_next(cursor) && cursor.current == second);
            TEST_EXPECT(ctx, moved.array_cursor_at(array, 0u, cursor));
            destination = std::move(moved);
            TEST_EXPECT(ctx, moved.is_empty() && !moved.root().is_valid());
            TEST_EXPECT(ctx, moved.is_valid() && !moved.is_ready());
        }
        TEST_EXPECT(ctx, destination_context.is_attribution_empty());
        TEST_EXPECT(ctx, source_context.get_live_allocation_count() == source_allocations);
        TEST_EXPECT(ctx, source_context.get_live_allocated_bytes() == source_bytes);
        TEST_EXPECT(ctx, destination.root() == root && destination.check_integrity());
        TEST_EXPECT(ctx, destination.property_name(name).string() == name_view.string());
        TEST_EXPECT(ctx, destination.string_value(first).string() == value_view.string());
        TEST_EXPECT(ctx, std::memcmp(value_view.string(), "retained", 8u) == 0);
        TEST_EXPECT(ctx, destination.array_cursor_next(cursor) && cursor.current == second);
        state.reject_allocation = false;
        TEST_EXPECT(ctx, destination.create_null().query_value() > second.query_value());
        TEST_EXPECT(ctx, source.initialise());
        TEST_EXPECT(ctx, source.create_null().query_value() == 1u);
        TEST_EXPECT(ctx, source.check_integrity());
        CLiveDocument empty;
        destination = std::move(empty);
        TEST_EXPECT(ctx, destination.is_empty() && !destination.root().is_valid());
        TEST_EXPECT(ctx, !destination.contains_recovered_duplicate_arrays());
        TEST_EXPECT(ctx, destination.is_valid() && !destination.is_ready());
    }
    TEST_EXPECT(ctx, source_context.is_attribution_empty());
    TEST_EXPECT(ctx, destination_context.is_attribution_empty());
}

void test_transfer_arrays_and_root_replacement(TTestContext& ctx)
{
    tests::TAllocatorFixture state;
    const tests::TModuleIdScope module(module_ids::executable);
    memory::CMemoryAllocator allocator(&state, tests::allocate_test_memory, tests::deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context(allocator, system_ids::host);
    {
        const tests::TMemoryContextScope scope(&context);
        CLiveDocument document;
        TEST_EXPECT(ctx, document.initialise());
        const CNodeKey donor = document.create_duplicate_array();
        const CNodeKey recipient = document.create_array();
        const CNodeKey existing = document.create_integer(0);
        const CNodeKey nested = document.create_array();
        const CNodeKey first = document.create_integer(1);
        const CNodeKey second = document.create_integer(2);
        const CNodeKey last = document.create_integer(3);
        TEST_EXPECT(ctx, document.set_root(donor));
        TEST_EXPECT(ctx, document.append_array_child(recipient, existing));
        TEST_EXPECT(ctx, document.append_array_child(nested, first));
        TEST_EXPECT(ctx, document.append_array_child(nested, second));
        TEST_EXPECT(ctx, document.append_array_child(donor, nested));
        TEST_EXPECT(ctx, document.append_array_child(donor, last));
        CArrayCursor donor_cursor, recipient_cursor, nested_cursor;
        TEST_EXPECT(ctx, document.array_cursor_at(donor, 0u, donor_cursor));
        TEST_EXPECT(ctx, document.array_cursor_at(recipient, 0u, recipient_cursor));
        TEST_EXPECT(ctx, document.array_cursor_at(nested, 0u, nested_cursor));
        const auto allocations = context.get_live_allocation_count();
        const auto bytes = context.get_live_allocated_bytes();
        state.reject_allocation = true;
        TEST_EXPECT(ctx, document.set_root(recipient));
        TEST_EXPECT(ctx, document.transfer_children(donor, recipient));
        TEST_EXPECT(ctx, context.get_live_allocation_count() == allocations);
        TEST_EXPECT(ctx, context.get_live_allocated_bytes() == bytes);
        TEST_EXPECT(ctx, document.child_count(donor) == 0u);
        TEST_EXPECT(ctx, !document.first_child(donor).is_valid() && !document.last_child(donor).is_valid());
        TEST_EXPECT(ctx, document.child_count(recipient) == 3u);
        TEST_EXPECT(ctx, document.array_at(recipient, 0u) == existing);
        TEST_EXPECT(ctx, document.array_at(recipient, 1u) == nested);
        TEST_EXPECT(ctx, document.array_at(recipient, 2u) == last);
        TEST_EXPECT(ctx, document.parent(nested) == recipient && document.parent(last) == recipient);
        TEST_EXPECT(ctx, document.previous_sibling(nested) == existing);
        TEST_EXPECT(ctx, document.next_sibling(nested) == last);
        TEST_EXPECT(ctx, !document.array_cursor_next(donor_cursor));
        TEST_EXPECT(ctx, !document.array_cursor_next(recipient_cursor));
        TEST_EXPECT(ctx, document.array_cursor_next(nested_cursor) && nested_cursor.current == second);
        TEST_EXPECT(ctx, document.contains_recovered_duplicate_arrays());
        TEST_EXPECT(ctx, document.array_cursor_at(recipient, 0u, recipient_cursor));
        TEST_EXPECT(ctx, document.transfer_children(donor, recipient));
        TEST_EXPECT(ctx, document.array_cursor_next(recipient_cursor) && recipient_cursor.current == nested);
        TEST_EXPECT(ctx, document.erase_detached(donor));
        TEST_EXPECT(ctx, !document.contains_recovered_duplicate_arrays());
        TEST_EXPECT(ctx, document.node_type(donor) == EJsonNodeType::invalid);
        TEST_EXPECT(ctx, document.check_integrity());
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}

void test_transfer_objects_and_rejections(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, document.initialise());
    const CNodeKey root = document.create_object();
    const CNodeKey donor = document.create_object();
    const CNodeKey recipient = document.create_object();
    const CNodeKey first = document.create_boolean(true);
    const CNodeKey second = document.create_array();
    const CNodeKey existing = document.create_null();
    TEST_EXPECT(ctx, document.set_root(root));
    TEST_EXPECT(ctx, document.add_object_child(root, text("donor"), donor));
    TEST_EXPECT(ctx, document.add_object_child(root, text("recipient"), recipient));
    TEST_EXPECT(ctx, document.add_object_child(donor, text("a"), first));
    TEST_EXPECT(ctx, document.add_object_child(donor, text("b"), second));
    TEST_EXPECT(ctx, document.add_object_child(recipient, text("b"), existing));
    const CPropertyNameId first_name = document.name_in_parent(first);
    const CPropertyNameId second_name = document.name_in_parent(second);
    //  A collision on the last donor member must not transfer the earlier one.
    TEST_EXPECT(ctx, !document.transfer_children(donor, recipient));
    TEST_EXPECT(ctx, document.child_count(donor) == 2u && document.child_count(recipient) == 1u);
    TEST_EXPECT(ctx, document.parent(first) == donor && document.parent(second) == donor);
    TEST_EXPECT(ctx, document.first_child(donor) == first && document.next_sibling(first) == second);
    TEST_EXPECT(ctx, !document.object_child(recipient, first_name).is_valid());
    TEST_EXPECT(ctx, document.check_integrity());
    TEST_EXPECT(ctx, document.detach(existing));
    TEST_EXPECT(ctx, document.add_object_child(recipient, text("prefix"), existing));
    TEST_EXPECT(ctx, document.transfer_children(donor, recipient));
    TEST_EXPECT(ctx, document.first_child(recipient) == existing);
    TEST_EXPECT(ctx, document.next_sibling(existing) == first && document.next_sibling(first) == second);
    TEST_EXPECT(ctx, document.last_child(recipient) == second);
    TEST_EXPECT(ctx, document.object_child(recipient, first_name) == first);
    TEST_EXPECT(ctx, document.object_child(recipient, second_name) == second);
    TEST_EXPECT(ctx, document.parent(first) == recipient && document.parent(second) == recipient);
    TEST_EXPECT(ctx, document.parent(donor) == root);
    TEST_EXPECT(ctx, !document.erase_detached(donor));
    TEST_EXPECT(ctx, document.detach(donor) && document.erase_detached(donor));

    TEST_EXPECT(ctx, !document.transfer_children(recipient, recipient));
    TEST_EXPECT(ctx, !document.transfer_children(CNodeKey{}, recipient));
    TEST_EXPECT(ctx, !document.transfer_children(recipient, CNodeKey{}));
    TEST_EXPECT(ctx, !document.transfer_children(donor, recipient));
    TEST_EXPECT(ctx, !document.transfer_children(first, recipient));
    TEST_EXPECT(ctx, !document.transfer_children(recipient, first));
    TEST_EXPECT(ctx, !document.transfer_children(recipient, second));
    TEST_EXPECT(ctx, !document.transfer_children(second, recipient));
    TEST_EXPECT(ctx, !document.transfer_children(root, recipient));
    TEST_EXPECT(ctx, document.child_count(recipient) == 3u && document.check_integrity());

    const CNodeKey outer = document.create_array();
    const CNodeKey middle = document.create_duplicate_array();
    const CNodeKey inner = document.create_array();
    const CNodeKey leaf = document.create_null();
    TEST_EXPECT(ctx, document.append_array_child(outer, middle));
    TEST_EXPECT(ctx, document.append_array_child(middle, inner));
    TEST_EXPECT(ctx, document.append_array_child(inner, leaf));
    TEST_EXPECT(ctx, !document.transfer_children(outer, inner));
    TEST_EXPECT(ctx, !document.transfer_children(middle, inner));
    TEST_EXPECT(ctx, document.parent(middle) == outer && document.parent(inner) == middle);
    TEST_EXPECT(ctx, document.child_count(outer) == 1u && document.child_count(inner) == 1u);
    //  Moving to an ancestor is safe: the donor remains an empty child.
    TEST_EXPECT(ctx, document.transfer_children(inner, middle));
    TEST_EXPECT(ctx, document.array_at(middle, 0u) == inner && document.array_at(middle, 1u) == leaf);
    TEST_EXPECT(ctx, document.parent(leaf) == middle && document.child_count(inner) == 0u);
    const CNodeKey empty = document.create_array();
    TEST_EXPECT(ctx, document.transfer_children(middle, empty));
    TEST_EXPECT(ctx, document.first_child(empty) == inner && document.last_child(empty) == leaf);
    TEST_EXPECT(ctx, document.parent(inner) == empty && document.parent(leaf) == empty);
    TEST_EXPECT(ctx, document.check_integrity());
}

void test_duplicate_creation_failure(TTestContext& ctx)
{
    tests::TAllocatorFixture state;
    const tests::TModuleIdScope module(module_ids::executable);
    memory::CMemoryAllocator allocator(&state, tests::allocate_test_memory, tests::deallocate_test_memory, system_ids::host);
    memory::CMemoryContext context(allocator, system_ids::host);
    {
        const tests::TMemoryContextScope scope(&context);
        CLiveDocument document;
        TEST_EXPECT(ctx, !document.create_duplicate_array().is_valid());
        TEST_EXPECT(ctx, document.initialise(1u));
        const CNodeKey root = document.create_null();
        TEST_EXPECT(ctx, document.set_root(root));
        state.reject_allocation = true;
        //  Exhaust any spare slots without assuming the container's growth size.
        std::size_t filled = 0u;
        while ((filled < 1024u) && document.create_null().is_valid()) ++filled;
        TEST_EXPECT(ctx, filled < 1024u);
        TEST_EXPECT(ctx, !document.create_duplicate_array().is_valid());
        TEST_EXPECT(ctx, document.root() == root && !document.contains_recovered_duplicate_arrays());
        TEST_EXPECT(ctx, document.check_integrity());
        state.reject_allocation = false;
        const CNodeKey duplicate = document.create_duplicate_array();
        TEST_EXPECT(ctx, duplicate.is_valid() && document.contains_recovered_duplicate_arrays());
        TEST_EXPECT(ctx, document.erase_detached(duplicate));
        TEST_EXPECT(ctx, !document.contains_recovered_duplicate_arrays() && document.check_integrity());
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
}
}

int run_live_document_tests()
{
    TTestContext ctx;
    test_layout_and_scalars(ctx);
    test_live_floating_workspace_accepts_non_finite_values(ctx);
    test_numeric_intent_and_widths(ctx);
    test_array_mutation_and_cursor(ctx);
    test_object_names_and_rejected_moves(ctx);
    test_attachment_rejects_cycles(ctx);
    test_stale_key_rejection(ctx);
    test_move_preserves_content_and_attribution(ctx);
    test_transfer_arrays_and_root_replacement(ctx);
    test_transfer_objects_and_rejections(ctx);
    test_duplicate_creation_failure(ctx);
    std::cout << "LiveDocument: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
