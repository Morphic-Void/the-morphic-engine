//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)

#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

#include "data_model/baked_document_builder.hpp"
#include "data_model/baked_document.hpp"
#include "tests/support/test_context.hpp"

namespace
{
using TTestContext = tests::TTestContext;

CStringView text(const char* const value) noexcept { return CStringView{ value }; }

std::uint64_t double_bits(const double value) noexcept
{
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::uint32_t payload_crc(const std::uint8_t* bytes, std::size_t byte_count) noexcept
{
    std::uint32_t value = 0xFFFFFFFFu;
    for (; byte_count--; ++bytes)
    {
        value ^= *bytes;
        for (std::uint32_t bit = 0u; bit < 8u; ++bit)
        {
            value = (value >> 1u) ^ ((value & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return ~value;
}

std::vector<std::uint8_t> copy_bytes(const CBakedDocumentBlock& block)
{
    const CByteBuffer& bytes = block.bytes();
    return std::vector<std::uint8_t>{ bytes.data(), bytes.data() + bytes.size() };
}

void refresh_payload_crc(std::vector<std::uint8_t>& bytes) noexcept
{
    CBakedDocumentHeader* const header = reinterpret_cast<CBakedDocumentHeader*>(bytes.data());
    header->payload_crc = payload_crc(bytes.data() + header->header_size, bytes.size() - header->header_size);
}

void test_bake_preserves_reachable_semantics(TTestContext& ctx)
{
    static_assert(sizeof(CBakedNode) == 32u);
    static_assert(std::is_trivially_copyable_v<CBakedNode>);
    static_assert(sizeof(CBakedDocumentHeader) == 64u);
    static_assert(std::is_trivially_copyable_v<CBakedDocumentHeader>);

    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const CNodeKey root = live.create_object();
    const CNodeKey values = live.create_array();
    const CNodeKey first = live.create_integer(7);
    const CNodeKey second = live.create_string(text("two"));
    const CNodeKey enabled = live.create_boolean(true);
    const CNodeKey detached_string = live.create_string(text("discarded"));
    const CNodeKey detached_object_value = live.create_null();
    TEST_EXPECT(ctx, live.set_root(root));
    TEST_EXPECT(ctx, live.add_object_child(root, text("values"), values));
    TEST_EXPECT(ctx, live.add_object_child(root, text("enabled"), enabled));
    TEST_EXPECT(ctx, live.append_array_child(values, first));
    TEST_EXPECT(ctx, live.append_array_child(values, second));
    TEST_EXPECT(ctx, detached_string.is_valid());
    TEST_EXPECT(ctx, detached_object_value.is_valid());
    TEST_EXPECT(ctx, live.check_integrity());

    CBakedDocumentBuilder baked;
    TEST_EXPECT(ctx, baked.build_from(live));
    TEST_EXPECT(ctx, baked.is_ready());
    TEST_EXPECT(ctx, baked.check_integrity());
    TEST_EXPECT(ctx, baked.node_count() == 5u);
    TEST_EXPECT(ctx, baked.property_name_count() == 2u);
    TEST_EXPECT(ctx, baked.string_value_count() == 1u);

    const CBakedNodeIndex baked_root = baked.root();
    const CBakedNodeIndex baked_values = baked.object_child(baked_root, text("values"));
    const CBakedNodeIndex baked_enabled = baked.object_child(baked_root, text("enabled"));
    TEST_EXPECT(ctx, baked.node_type(baked_root) == EJsonNodeType::object);
    TEST_EXPECT(ctx, baked_values.is_valid());
    TEST_EXPECT(ctx, baked_enabled.is_valid());
    TEST_EXPECT(ctx, baked.parent(baked_values) == baked_root);
    TEST_EXPECT(ctx, baked.previous_sibling(baked_values) == CBakedNodeIndex{});
    TEST_EXPECT(ctx, baked.next_sibling(baked_values) == baked_enabled);
    TEST_EXPECT(ctx, baked.child_count(baked_values) == 2u);
    const CBakedNodeIndex baked_first = baked.array_at(baked_values, 0u);
    const CBakedNodeIndex baked_second = baked.array_at(baked_values, 1u);
    std::int64_t integer = 0;
    bool boolean = false;
    TEST_EXPECT(ctx, baked.integer_value(baked_first, integer) && (integer == 7));
    TEST_EXPECT(ctx, baked.string_value(baked_second).length() == 3u);
    TEST_EXPECT(ctx, std::memcmp(baked.string_value(baked_second).string(), "two", 3u) == 0);
    TEST_EXPECT(ctx, baked.boolean_value(baked_enabled, boolean) && boolean);
}

void test_bake_is_atomic_and_rejects_invalid_source(TTestContext& ctx)
{
    CLiveDocument valid;
    TEST_EXPECT(ctx, valid.initialise());
    const CNodeKey valid_root = valid.create_array();
    TEST_EXPECT(ctx, valid.set_root(valid_root));

    CBakedDocumentBuilder baked;
    TEST_EXPECT(ctx, baked.build_from(valid));
    const CBakedNodeIndex old_root = baked.root();
    const std::uint32_t old_count = baked.node_count();

    CLiveDocument rootless;
    TEST_EXPECT(ctx, rootless.initialise());
    TEST_EXPECT(ctx, rootless.create_null().is_valid());
    TEST_EXPECT(ctx, !baked.build_from(rootless));
    TEST_EXPECT(ctx, baked.root() == old_root);
    TEST_EXPECT(ctx, baked.node_count() == old_count);
    TEST_EXPECT(ctx, baked.check_integrity());

    CLiveDocument unready;
    TEST_EXPECT(ctx, !baked.build_from(unready));
    TEST_EXPECT(ctx, baked.root() == old_root);
    TEST_EXPECT(ctx, baked.check_integrity());
}

void test_final_baked_block(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const CNodeKey root=live.create_object(), value=live.create_string(text("value"));
    TEST_EXPECT(ctx, live.set_root(root));
    TEST_EXPECT(ctx, live.add_object_child(root,text("key"),value));
    CBakedDocumentBuilder builder;
    TEST_EXPECT(ctx, builder.build_from(live));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, block.build_from(builder));
    const CBakedDocument& doc=block.document();
    TEST_EXPECT(ctx, doc.is_ready() && doc.check_integrity());
    const CBakedNodeIndex child=doc.object_child(doc.root(),text("key"));
    TEST_EXPECT(ctx, child.is_valid());
    TEST_EXPECT(ctx, doc.string_value(child).length()==5u);
    TEST_EXPECT(ctx, std::memcmp(doc.string_value(child).string(),"value",5u)==0);
    const CStringView child_name = doc.property_name(doc.name_in_parent(child));
    TEST_EXPECT(ctx, child_name.length() == 3u);
    TEST_EXPECT(ctx, child_name.string()[child_name.length()] == 0u);
    CBakedDocument invalid{block.bytes().data(),block.bytes().size()-1u};
    TEST_EXPECT(ctx, !invalid.is_ready());
}

void test_baked_validation_rejects_malformed_layout_and_structure(TTestContext& ctx)
{
    CLiveDocument live;
    TEST_EXPECT(ctx, live.initialise());
    const CNodeKey root = live.create_object();
    const CNodeKey first = live.create_integer(1);
    const CNodeKey second = live.create_integer(2);
    TEST_EXPECT(ctx, live.set_root(root));
    TEST_EXPECT(ctx, live.add_object_child(root, text("first"), first));
    TEST_EXPECT(ctx, live.add_object_child(root, text("second"), second));

    CBakedDocumentBuilder builder;
    TEST_EXPECT(ctx, builder.build_from(live));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, block.build_from(builder));

    std::vector<std::uint8_t> unaligned(block.bytes().size() + 1u);
    std::memcpy(unaligned.data() + 1u, block.bytes().data(), block.bytes().size());
    const CBakedDocument unaligned_document{ unaligned.data() + 1u, block.bytes().size() };
    TEST_EXPECT(ctx, !unaligned_document.is_ready());

    std::vector<std::uint8_t> wrong_layout = copy_bytes(block);
    CBakedDocumentHeader* header = reinterpret_cast<CBakedDocumentHeader*>(wrong_layout.data());
    ++header->property_names.references_offset;
    const CBakedDocument wrong_layout_document{ wrong_layout.data(), wrong_layout.size() };
    TEST_EXPECT(ctx, !wrong_layout_document.is_ready());

    std::vector<std::uint8_t> orphan = copy_bytes(block);
    header = reinterpret_cast<CBakedDocumentHeader*>(orphan.data());
    CBakedNode* nodes = reinterpret_cast<CBakedNode*>(orphan.data() + header->nodes.offset);
    nodes[3].parent = CBakedNodeIndex{};
    refresh_payload_crc(orphan);
    const CBakedDocument orphan_document{ orphan.data(), orphan.size() };
    TEST_EXPECT(ctx, !orphan_document.is_ready());

    std::vector<std::uint8_t> reverse_containment = copy_bytes(block);
    header = reinterpret_cast<CBakedDocumentHeader*>(reverse_containment.data());
    nodes = reinterpret_cast<CBakedNode*>(reverse_containment.data() + header->nodes.offset);
    nodes[1].child_count = 1u;
    refresh_payload_crc(reverse_containment);
    const CBakedDocument reverse_containment_document{
        reverse_containment.data(), reverse_containment.size() };
    TEST_EXPECT(ctx, !reverse_containment_document.is_ready());

    std::vector<std::uint8_t> unnamed_object_child = copy_bytes(block);
    header = reinterpret_cast<CBakedDocumentHeader*>(unnamed_object_child.data());
    nodes = reinterpret_cast<CBakedNode*>(unnamed_object_child.data() + header->nodes.offset);
    nodes[2].name_in_parent = CPropertyNameId{};
    refresh_payload_crc(unnamed_object_child);
    const CBakedDocument unnamed_object_child_document{
        unnamed_object_child.data(), unnamed_object_child.size() };
    TEST_EXPECT(ctx, !unnamed_object_child_document.is_ready());

    std::vector<std::uint8_t> duplicate_object_name = copy_bytes(block);
    header = reinterpret_cast<CBakedDocumentHeader*>(duplicate_object_name.data());
    nodes = reinterpret_cast<CBakedNode*>(duplicate_object_name.data() + header->nodes.offset);
    nodes[3].name_in_parent = nodes[2].name_in_parent;
    refresh_payload_crc(duplicate_object_name);
    const CBakedDocument duplicate_object_name_document{
        duplicate_object_name.data(), duplicate_object_name.size() };
    TEST_EXPECT(ctx, !duplicate_object_name_document.is_ready());
}

void test_baked_document_promotion(TTestContext& ctx)
{
    CLiveDocument source;
    TEST_EXPECT(ctx, source.initialise());
    const CNodeKey root = source.create_object();
    const CNodeKey values = source.create_array();
    const CNodeKey first = source.create_integer(7);
    const CNodeKey second = source.create_string(text("two"));
    TEST_EXPECT(ctx, source.set_root(root));
    TEST_EXPECT(ctx, source.add_object_child(root, text("values"), values));
    TEST_EXPECT(ctx, source.append_array_child(values, first));
    TEST_EXPECT(ctx, source.append_array_child(values, second));

    CBakedDocumentBuilder builder;
    TEST_EXPECT(ctx, builder.build_from(source));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, block.build_from(builder));

    CLiveDocument promoted;
    TEST_EXPECT(ctx, promoted.build_from(block.document()));
    TEST_EXPECT(ctx, promoted.check_integrity());
    const CNodeKey promoted_values = promoted.object_child(promoted.root(), text("values"));
    TEST_EXPECT(ctx, promoted.node_type(promoted.root()) == EJsonNodeType::object);
    TEST_EXPECT(ctx, promoted.child_count(promoted_values) == 2u);
    std::int64_t first_value = 0;
    TEST_EXPECT(ctx,
        promoted.integer_value(promoted.array_at(promoted_values, 0u), first_value) &&
        (first_value == 7));
    TEST_EXPECT(ctx,
        promoted.string_value(promoted.array_at(promoted_values, 1u)).length() == 3u);

    const CNodeKey appended = promoted.create_null();
    TEST_EXPECT(ctx, promoted.append_array_child(promoted_values, appended));
    TEST_EXPECT(ctx, promoted.child_count(promoted_values) == 3u);

    const CNodeKey old_root = promoted.root();
    const CBakedDocument invalid;
    TEST_EXPECT(ctx, !promoted.build_from(invalid));
    TEST_EXPECT(ctx, promoted.root() == old_root);
    TEST_EXPECT(ctx, promoted.check_integrity());
}

void test_numeric_intent_survives_bake_and_promotion(TTestContext& ctx)
{
    CLiveDocument source;
    TEST_EXPECT(ctx, source.initialise());
    const CNodeKey root = source.create_array();
    const CJsonIntegerMetadata explicit_hex{
        EJsonIntegerSign::signed_value, EJsonIntegerWidth::bits_8,
        EJsonIntegerNotation::hexadecimal, EJsonIntegerPrefix::alternate };
    const CJsonIntegerMetadata binary{
        EJsonIntegerSign::unsigned_value, EJsonIntegerWidth::bits_16,
        EJsonIntegerNotation::binary, EJsonIntegerPrefix::standard };
    const CNodeKey first = source.create_integer(127, explicit_hex);
    const CNodeKey second = source.create_unsigned_integer(256u, binary);
    TEST_EXPECT(ctx, source.set_root(root));
    TEST_EXPECT(ctx, source.append_array_child(root, first));
    TEST_EXPECT(ctx, source.append_array_child(root, second));

    CBakedDocumentBuilder builder;
    TEST_EXPECT(ctx, builder.build_from(source));
    TEST_EXPECT(ctx, !builder.is_canonical());
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, block.build_from(builder));
    const CBakedDocument& baked = block.document();
    TEST_EXPECT(ctx, baked.check_integrity());
    TEST_EXPECT(ctx, baked.requires_morphic_json_extensions());
    CJsonIntegerMetadata metadata;
    TEST_EXPECT(ctx, baked.integer_metadata(baked.array_at(baked.root(), 0u), metadata));
    TEST_EXPECT(ctx, metadata.sign == EJsonIntegerSign::signed_value);
    TEST_EXPECT(ctx, metadata.notation == EJsonIntegerNotation::hexadecimal);
    TEST_EXPECT(ctx, metadata.prefix == EJsonIntegerPrefix::alternate);
    TEST_EXPECT(ctx, baked.integer_metadata(baked.array_at(baked.root(), 1u), metadata));
    TEST_EXPECT(ctx, metadata.width == EJsonIntegerWidth::bits_16);
    TEST_EXPECT(ctx, metadata.notation == EJsonIntegerNotation::binary);

    CLiveDocument promoted;
    TEST_EXPECT(ctx, promoted.build_from(baked));
    TEST_EXPECT(ctx, promoted.integer_metadata(promoted.array_at(promoted.root(), 0u), metadata));
    TEST_EXPECT(ctx, metadata.sign == EJsonIntegerSign::signed_value);
    TEST_EXPECT(ctx, metadata.notation == EJsonIntegerNotation::hexadecimal);
    TEST_EXPECT(ctx, metadata.prefix == EJsonIntegerPrefix::alternate);
    TEST_EXPECT(ctx, promoted.check_integrity());
}

void test_floating_boundary_and_payload_preservation(TTestContext& ctx)
{
    CLiveDocument source;
    TEST_EXPECT(ctx, source.initialise());
    const CNodeKey root = source.create_array();
    TEST_EXPECT(ctx, source.set_root(root));
    TEST_EXPECT(ctx, source.append_array_child(root, source.create_floating_point(3.5)));
    TEST_EXPECT(ctx, source.append_array_child(root, source.create_floating_point(-2.25)));
    TEST_EXPECT(ctx, source.append_array_child(root, source.create_floating_point(-0.0)));

    CBakedDocumentBuilder builder;
    TEST_EXPECT(ctx, builder.build_from(source));
    TEST_EXPECT(ctx, builder.is_canonical());
    TEST_EXPECT(ctx, !builder.requires_morphic_json_extensions());
    double value = 0.0;
    TEST_EXPECT(ctx, builder.floating_point_value(builder.array_at(builder.root(), 2u), value));
    TEST_EXPECT(ctx, double_bits(value) == double_bits(-0.0));

    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, block.build_from(builder));
    const CBakedDocumentHeader* const header = reinterpret_cast<const CBakedDocumentHeader*>(block.bytes().data());
    const CBakedNode* const nodes = reinterpret_cast<const CBakedNode*>(block.bytes().data() + header->nodes.offset);
    TEST_EXPECT(ctx, header->version == 3u);
    TEST_EXPECT(ctx, nodes[2].flags == 0u);
    TEST_EXPECT(ctx, nodes[3].flags == 0u);
    TEST_EXPECT(ctx, nodes[4].flags == 0u);
    TEST_EXPECT(ctx, block.document().floating_point_value(block.document().array_at(block.document().root(), 2u), value));
    TEST_EXPECT(ctx, double_bits(value) == double_bits(-0.0));

    CLiveDocument promoted;
    TEST_EXPECT(ctx, promoted.build_from(block.document()));
    TEST_EXPECT(ctx, promoted.floating_point_value(promoted.array_at(promoted.root(), 2u), value));
    TEST_EXPECT(ctx, double_bits(value) == double_bits(-0.0));
}

void test_non_finite_bake_rejection_is_atomic(TTestContext& ctx)
{
    CLiveDocument valid;
    TEST_EXPECT(ctx, valid.initialise());
    const CNodeKey valid_root = valid.create_integer(9);
    TEST_EXPECT(ctx, valid.set_root(valid_root));
    CBakedDocumentBuilder destination;
    TEST_EXPECT(ctx, destination.build_from(valid));
    const CBakedNodeIndex old_root = destination.root();
    const std::uint32_t old_count = destination.node_count();

    const double rejected[]{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity() };
    for (const double value : rejected)
    {
        CLiveDocument source;
        TEST_EXPECT(ctx, source.initialise());
        const CNodeKey root = source.create_floating_point(value);
        TEST_EXPECT(ctx, source.set_root(root));
        TEST_EXPECT(ctx, source.check_integrity());
        TEST_EXPECT(ctx, !destination.build_from(source));
        TEST_EXPECT(ctx, destination.root() == old_root);
        TEST_EXPECT(ctx, destination.node_count() == old_count);
        TEST_EXPECT(ctx, destination.check_integrity());
    }
}

bool bake_single_string(CBakedDocumentBuilder& destination, const std::uint8_t* bytes, const std::size_t length)
{
    CLiveDocument source;
    if (!source.initialise()) return false;
    const CNodeKey root = source.create_string(CStringView{ bytes, length });
    return root.is_valid() && source.set_root(root) && destination.build_from(source);
}

void test_modified_utf8_and_terminated_baked_strings(TTestContext& ctx)
{
    const std::uint8_t literal_nul[]{ 'a', 0u, 'b' };
    const std::uint8_t modified_nul[]{ 'a', 0xc0u, 0x80u, 'b' };
    CBakedDocumentBuilder builder;
    TEST_EXPECT(ctx, bake_single_string(builder, literal_nul, sizeof(literal_nul)));
    const CStringView normalized = builder.string_value(builder.root());
    TEST_EXPECT(ctx, normalized.length() == sizeof(modified_nul));
    TEST_EXPECT(ctx, std::memcmp(normalized.string(), modified_nul, sizeof(modified_nul)) == 0);

    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, block.build_from(builder));
    const CStringView baked = block.document().string_value(block.document().root());
    TEST_EXPECT(ctx, baked.length() == sizeof(modified_nul));
    TEST_EXPECT(ctx, std::memcmp(baked.string(), modified_nul, sizeof(modified_nul)) == 0);
    TEST_EXPECT(ctx, baked.string()[baked.length()] == 0u);

    const CBakedDocumentHeader* const header = reinterpret_cast<const CBakedDocumentHeader*>(block.bytes().data());
    TEST_EXPECT(ctx, header->string_values.bytes_size == (sizeof(modified_nul) + 1u));
    const CBakedStringRef* const references = reinterpret_cast<const CBakedStringRef*>(
        block.bytes().data() + header->string_values.references_offset);
    TEST_EXPECT(ctx, references[1].length == sizeof(modified_nul));

    CBakedDocumentBuilder already_modified;
    TEST_EXPECT(ctx, bake_single_string(already_modified, modified_nul, sizeof(modified_nul)));
    TEST_EXPECT(ctx, already_modified.string_value(already_modified.root()) == normalized);

    const std::uint8_t strict_multibyte[]{
        'x', 0xc2u, 0xa2u, 0xe2u, 0x82u, 0xacu, 0xf0u, 0x9fu, 0x98u, 0x80u };
    CBakedDocumentBuilder strict_builder;
    TEST_EXPECT(ctx, bake_single_string(strict_builder, strict_multibyte, sizeof(strict_multibyte)));
    CBakedDocumentBlock strict_block;
    TEST_EXPECT(ctx, strict_block.build_from(strict_builder));
    const CStringView strict_baked = strict_block.document().string_value(strict_block.document().root());
    TEST_EXPECT(ctx, strict_baked.length() == sizeof(strict_multibyte));
    TEST_EXPECT(ctx, std::memcmp(strict_baked.string(), strict_multibyte, sizeof(strict_multibyte)) == 0);
    TEST_EXPECT(ctx, strict_baked.string()[strict_baked.length()] == 0u);
}

void test_utf8_rejection_and_normalized_name_collision_are_atomic(TTestContext& ctx)
{
    CBakedDocumentBuilder destination;
    const std::uint8_t seed[]{ 'o', 'k' };
    TEST_EXPECT(ctx, bake_single_string(destination, seed, sizeof(seed)));
    const CBakedNodeIndex old_root = destination.root();

    const std::uint8_t lone_continuation[]{ 0x80u };
    const std::uint8_t overlong[]{ 0xc1u, 0x81u };
    const std::uint8_t surrogate[]{ 0xedu, 0xa0u, 0x80u };
    const std::uint8_t extended[]{ 0xf5u, 0x80u, 0x80u, 0x80u };
    const std::uint8_t truncated[]{ 0xe2u, 0x82u };
    const struct { const std::uint8_t* bytes; std::size_t size; } malformed[]{
        { lone_continuation, sizeof(lone_continuation) }, { overlong, sizeof(overlong) },
        { surrogate, sizeof(surrogate) }, { extended, sizeof(extended) },
        { truncated, sizeof(truncated) } };
    for (const auto& value : malformed)
    {
        TEST_EXPECT(ctx, !bake_single_string(destination, value.bytes, value.size));
        TEST_EXPECT(ctx, destination.root() == old_root);
        TEST_EXPECT(ctx, destination.check_integrity());
    }

    const std::uint8_t literal_name[]{ 'a', 0u, 'b' };
    const std::uint8_t modified_name[]{ 'a', 0xc0u, 0x80u, 'b' };
    CLiveDocument collision;
    TEST_EXPECT(ctx, collision.initialise());
    const CNodeKey object = collision.create_object();
    TEST_EXPECT(ctx, collision.set_root(object));
    TEST_EXPECT(ctx, collision.add_object_child(object, CStringView{ literal_name, sizeof(literal_name) }, collision.create_null()));
    TEST_EXPECT(ctx, collision.add_object_child(object, CStringView{ modified_name, sizeof(modified_name) }, collision.create_null()));
    TEST_EXPECT(ctx, collision.check_integrity());
    TEST_EXPECT(ctx, !destination.build_from(collision));
    TEST_EXPECT(ctx, destination.root() == old_root);
}

void test_baked_v3_rejects_forged_boundary_violations(TTestContext& ctx)
{
    const std::uint8_t content[]{ 'v', 'a', 'l' };
    CBakedDocumentBuilder builder;
    TEST_EXPECT(ctx, bake_single_string(builder, content, sizeof(content)));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, block.build_from(builder));

    std::vector<std::uint8_t> wrong_version = copy_bytes(block);
    reinterpret_cast<CBakedDocumentHeader*>(wrong_version.data())->version = 2u;
    TEST_EXPECT(ctx, (!CBakedDocument{ wrong_version.data(), wrong_version.size() }.is_ready()));
    reinterpret_cast<CBakedDocumentHeader*>(wrong_version.data())->version = 4u;
    TEST_EXPECT(ctx, (!CBakedDocument{ wrong_version.data(), wrong_version.size() }.is_ready()));

    std::vector<std::uint8_t> nonzero_reserved = copy_bytes(block);
    CBakedDocumentHeader* header = reinterpret_cast<CBakedDocumentHeader*>(nonzero_reserved.data());
    CBakedNode* nodes = reinterpret_cast<CBakedNode*>(nonzero_reserved.data() + header->nodes.offset);
    nodes[1].reserved = 1u;
    refresh_payload_crc(nonzero_reserved);
    TEST_EXPECT(ctx, (!CBakedDocument{ nonzero_reserved.data(), nonzero_reserved.size() }.is_ready()));

    std::vector<std::uint8_t> missing_terminator = copy_bytes(block);
    header = reinterpret_cast<CBakedDocumentHeader*>(missing_terminator.data());
    CBakedStringRef* references = reinterpret_cast<CBakedStringRef*>(
        missing_terminator.data() + header->string_values.references_offset);
    missing_terminator[header->string_values.bytes_offset + references[1].offset + references[1].length] = 'x';
    refresh_payload_crc(missing_terminator);
    TEST_EXPECT(ctx, (!CBakedDocument{ missing_terminator.data(), missing_terminator.size() }.is_ready()));

    std::vector<std::uint8_t> malformed_utf8 = copy_bytes(block);
    header = reinterpret_cast<CBakedDocumentHeader*>(malformed_utf8.data());
    references = reinterpret_cast<CBakedStringRef*>(malformed_utf8.data() + header->string_values.references_offset);
    malformed_utf8[header->string_values.bytes_offset + references[1].offset] = 0x80u;
    refresh_payload_crc(malformed_utf8);
    TEST_EXPECT(ctx, (!CBakedDocument{ malformed_utf8.data(), malformed_utf8.size() }.is_ready()));

    CLiveDocument two_strings_source;
    TEST_EXPECT(ctx, two_strings_source.initialise());
    const CNodeKey array = two_strings_source.create_array();
    TEST_EXPECT(ctx, two_strings_source.set_root(array));
    TEST_EXPECT(ctx, two_strings_source.append_array_child(array, two_strings_source.create_string(text("a"))));
    TEST_EXPECT(ctx, two_strings_source.append_array_child(array, two_strings_source.create_string(text("b"))));
    CBakedDocumentBuilder two_strings_builder;
    TEST_EXPECT(ctx, two_strings_builder.build_from(two_strings_source));
    CBakedDocumentBlock two_strings_block;
    TEST_EXPECT(ctx, two_strings_block.build_from(two_strings_builder));

    std::vector<std::uint8_t> non_dense = copy_bytes(two_strings_block);
    header = reinterpret_cast<CBakedDocumentHeader*>(non_dense.data());
    references = reinterpret_cast<CBakedStringRef*>(non_dense.data() + header->string_values.references_offset);
    references[2].offset = references[1].offset;
    refresh_payload_crc(non_dense);
    TEST_EXPECT(ctx, (!CBakedDocument{ non_dense.data(), non_dense.size() }.is_ready()));

    std::vector<std::uint8_t> duplicate_string = copy_bytes(two_strings_block);
    header = reinterpret_cast<CBakedDocumentHeader*>(duplicate_string.data());
    references = reinterpret_cast<CBakedStringRef*>(duplicate_string.data() + header->string_values.references_offset);
    duplicate_string[header->string_values.bytes_offset + references[2].offset] = 'a';
    refresh_payload_crc(duplicate_string);
    TEST_EXPECT(ctx, (!CBakedDocument{ duplicate_string.data(), duplicate_string.size() }.is_ready()));

    CLiveDocument float_source;
    TEST_EXPECT(ctx, float_source.initialise());
    const CNodeKey float_root = float_source.create_floating_point(1.0);
    TEST_EXPECT(ctx, float_source.set_root(float_root));
    CBakedDocumentBuilder float_builder;
    TEST_EXPECT(ctx, float_builder.build_from(float_source));
    CBakedDocumentBlock float_block;
    TEST_EXPECT(ctx, float_block.build_from(float_builder));

    std::vector<std::uint8_t> non_finite = copy_bytes(float_block);
    header = reinterpret_cast<CBakedDocumentHeader*>(non_finite.data());
    nodes = reinterpret_cast<CBakedNode*>(non_finite.data() + header->nodes.offset);
    nodes[1].payload.floating_value = std::numeric_limits<double>::infinity();
    refresh_payload_crc(non_finite);
    TEST_EXPECT(ctx, (!CBakedDocument{ non_finite.data(), non_finite.size() }.is_ready()));

    std::vector<std::uint8_t> floating_flags = copy_bytes(float_block);
    header = reinterpret_cast<CBakedDocumentHeader*>(floating_flags.data());
    nodes = reinterpret_cast<CBakedNode*>(floating_flags.data() + header->nodes.offset);
    nodes[1].flags = 1u;
    refresh_payload_crc(floating_flags);
    TEST_EXPECT(ctx, (!CBakedDocument{ floating_flags.data(), floating_flags.size() }.is_ready()));
}
}

int run_baked_document_builder_tests()
{
    TTestContext ctx;
    test_bake_preserves_reachable_semantics(ctx);
    test_bake_is_atomic_and_rejects_invalid_source(ctx);
    test_final_baked_block(ctx);
    test_baked_validation_rejects_malformed_layout_and_structure(ctx);
    test_baked_document_promotion(ctx);
    test_numeric_intent_survives_bake_and_promotion(ctx);
    test_floating_boundary_and_payload_preservation(ctx);
    test_non_finite_bake_rejection_is_atomic(ctx);
    test_modified_utf8_and_terminated_baked_strings(ctx);
    test_utf8_rejection_and_normalized_name_collision_are_atomic(ctx);
    test_baked_v3_rejects_forged_boundary_violations(ctx);
    std::cout << "BakedDocumentBuilder: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
