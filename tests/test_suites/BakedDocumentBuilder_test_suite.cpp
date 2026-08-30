//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)

#include <cstring>
#include <iostream>
#include <type_traits>
#include <vector>

#include "data_model/baked_document_builder.hpp"
#include "data_model/baked_document.hpp"
#include "tests/support/test_context.hpp"

namespace
{
using TTestContext = tests::TTestContext;

CStringView text(const char* const value) noexcept { return CStringView{ value }; }

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
    std::cout << "BakedDocumentBuilder: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
