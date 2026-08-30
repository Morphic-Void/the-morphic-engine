//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)

#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>

#include "data_model/live_document.hpp"
#include "tests/support/test_context.hpp"

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
}

int run_live_document_tests()
{
    TTestContext ctx;
    test_layout_and_scalars(ctx);
    test_numeric_intent_and_widths(ctx);
    test_array_mutation_and_cursor(ctx);
    test_object_names_and_rejected_moves(ctx);
    test_attachment_rejects_cycles(ctx);
    test_stale_key_rejection(ctx);
    std::cout << "LiveDocument: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
