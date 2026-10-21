
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_helpers.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    21 Oct 26

#include "tests/support/document_helpers.hpp"

#include <cstring>

#include "data_model/document_parser.hpp"
#include "data_model/live_document.hpp"
#include "tests/support/test_context.hpp"

namespace tests
{

using schema::detail::CDocumentRead;
using schema::detail::SOccurrence;
using schema::EDocumentValueKind;

void attach_document_child(TTestContext& ctx, CLiveDocument& document, const CNodeKey parent, const CNodeKey child)
{
    TEST_EXPECT(ctx, child.is_valid());
    TEST_EXPECT(ctx, document.append_child(parent, child).succeeded());
}

CNodeKey add_document_child(TTestContext& ctx, CLiveDocument& document, const CNodeKey parent, const CNodeKey value)
{
    TEST_EXPECT(ctx, value.is_valid() && document.append_child(parent, value).succeeded());
    return value;
}

CBakedValueIndex baked_document_child(const CBakedDocument& document, const CBakedValueIndex parent, const char* name)
{
    return document.object_child(parent, CStringView{ name });
}

bool parse_document(const std::string& text, CLiveDocument& document)
{
    return document_parser::parse(CByteConstView{
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), 1u }, document).accepted();
}

bool parse_document(const char* const text, CLiveDocument& document)
{
    return document_parser::parse(CByteConstView{
        reinterpret_cast<const std::uint8_t*>(text), std::strlen(text) }, document).accepted();
}

bool same_baked_document(const CLiveDocument& document, const CBakedDocumentBlock& expected)
{
    CBakedDocumentBlock actual;
    return document_translation::bake(document, actual) && (actual.bytes().size() == expected.bytes().size()) &&
        (std::memcmp(actual.bytes().data(), expected.bytes().data(), actual.bytes().size()) == 0);
}

bool equal_document_values(const CDocumentRead& a, const SOccurrence left, const CDocumentRead& b, const SOccurrence right)
{
    if ((a.value_kind(left) != b.value_kind(right)) || !(a.name(left) == b.name(right)) ||
        (a.is_object_entry(left) != b.is_object_entry(right)) || (a.child_count(left) != b.child_count(right)))
    {
        return false;
    }
    switch (a.value_kind(left))
    {
        case EDocumentValueKind::integer:
        {
            std::uint64_t x{}, y{};
            if (a.unsigned_integer_value(left, x) && b.unsigned_integer_value(right, y))
            {
                return x == y;
            }
            std::int64_t sx{}, sy{};
            return a.signed_integer_value(left, sx) && b.signed_integer_value(right, sy) && (sx == sy);
        }
        case EDocumentValueKind::string:
        {
            return a.string_value(left) == b.string_value(right);
        }
        case EDocumentValueKind::floating_point:
        {
            double x{}, y{};
            return a.floating_point_value(left, x) && b.floating_point_value(right, y) &&
                (std::memcmp(&x, &y, sizeof(x)) == 0);
        }
        case EDocumentValueKind::boolean:
        {
            bool x{}, y{};
            return a.boolean_value(left, x) && b.boolean_value(right, y) && (x == y);
        }
        default:
        {
            SOccurrence r = b.first_child(right);
            for (SOccurrence l = a.first_child(left); l; l = a.next_sibling(l))
            {
                if (!equal_document_values(a, l, b, r))
                {
                    return false;
                }
                r = b.next_sibling(r);
            }
            return true;
        }
    }
}

}   //  namespace tests
