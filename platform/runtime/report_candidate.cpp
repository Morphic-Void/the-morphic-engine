
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_candidate.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Authors typed report values and passes logical text and named spans to the schema baker.

#include "runtime/report_candidate.hpp"
#include "data_model/document_parser.hpp"

#include <cstring>
#include "schema/baked_data_block.hpp"

namespace platform::runtime
{

using reports::k_collections;

static CNodeKey child(CLiveDocument& doc, const CNodeKey parent, const CNodeKey value) noexcept
{
    return (parent.is_valid() && value.is_valid() && doc.append_child(parent, value).succeeded()) ? value : CNodeKey{};
}

static CNodeKey object(CLiveDocument& doc, const CNodeKey parent, const char* const name) noexcept
{
    return child(doc, parent, doc.create_object(CStringView{ name }));
}

template<typename T>
static T read_scalar(const std::uint8_t* const bytes) noexcept
{
    T value;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

//  Staging descriptors contain capture-local text IDs and logical ranges.
//  The baker independently assigns document string and collection IDs.
static CNodeKey logical_value(CLiveDocument& doc, const schema::CResolvedTypes& types,
    const schema::CTypesIndex type, const std::uint8_t* const bytes, const CStableStrings& text,
    const CStringView name = {}) noexcept
{
    schema::SType layout;
    if (!types.type(type, layout)) return {};
    switch (layout.category)
    {
        case schema::ECategory::primitive:
        {
            switch (layout.primitive)
            {
                case schema::EPrimitive::u8:
                {
                    return doc.create_unsigned_integer(*bytes, name);
                }
                case schema::EPrimitive::u32:
                {
                    return doc.create_unsigned_integer(read_scalar<std::uint32_t>(bytes), name);
                }
                case schema::EPrimitive::u64:
                {
                    return doc.create_unsigned_integer(read_scalar<std::uint64_t>(bytes), name);
                }
                case schema::EPrimitive::i32:
                {
                    return doc.create_signed_integer(read_scalar<std::int32_t>(bytes), name);
                }
                default:
                {
                    return {};
                }
            }
        }
        case schema::ECategory::enumeration:
        {
            schema::SScalar scalar;
            scalar.value.unsigned_value = read_scalar<std::uint32_t>(bytes);
            schema::SLabel label;
            if (!types.label(types.first_label_for_value(type, scalar), label))
            {
                return {};
            }
            return doc.create_string(types.name(label.name), name);
        }
        case schema::ECategory::structure:
        {
            const CNodeKey value = doc.create_array(name);
            for (std::uint32_t n = 0u; n < layout.count; ++n)
            {
                schema::SMember member;
                if (!types.member(types.member_at(type, n), member) ||
                    !child(doc, value, logical_value(doc, types, member.type, (bytes + member.offset), text)).is_valid())
                {
                    return {};
                }
            }
            return value;
        }
        case schema::ECategory::bit_structure:
        {
            const std::uint32_t bits = read_scalar<std::uint32_t>(bytes);
            const CNodeKey value = doc.create_array(name);
            for (std::uint32_t n = 0u; n < layout.count; ++n)
            {
                schema::SField field;
                if (!types.field(types.field_at(type, n), field) ||
                    !child(doc, value, doc.create_boolean((bits & field.mask) != 0u)).is_valid())
                {
                    return {};
                }
            }
            return value;
        }
        case schema::ECategory::text:
        {
            const std::int32_t id = read_scalar<schema::STextReference>(bytes).string_value_id;
            if (id < 0)
            {
                const CNodeKey value = doc.create_object(name);
                return child(doc, value, doc.create_boolean(true, CStringView{ "absent" })).is_valid() ? value : CNodeKey{};
            }
            if (id && !text.is_valid_id(id))
            {
                return {};
            }
            return doc.create_string(id ? text.view(id) : CStringView{ "" }, name);
        }
        case schema::ECategory::span:
        {
            const schema::SSpanReference range = read_scalar<schema::SSpanReference>(bytes);
            const CNodeKey value = doc.create_object(name);
            if (!range.count)
            {
                const CStringView marker{ (range.collection_id < 0) ? "absent" : "empty" };
                return child(doc, value, doc.create_boolean(true, marker)).is_valid() ? value : CNodeKey{};
            }
            schema::SType element;
            if (!types.type(layout.element_or_storage, element))
            {
                return {};
            }
            for (const reports::SCollectionName& collection : k_collections)
            {
                if (!(types.name(element.name) == CStringView{ collection.type }))
                {
                    continue;
                }
                if (!child(doc, value, doc.create_string(CStringView{ collection.name }, CStringView{ "collection" })).is_valid() ||
                    !child(doc, value, doc.create_signed_integer(range.first, CStringView{ "first" })).is_valid() ||
                    !child(doc, value, doc.create_unsigned_integer(range.count, CStringView{ "count" })).is_valid())
                {
                    return {};
                }
                return value;
            }
            return {};
        }
        default:
        {
            return {};
        }
    }
}

struct SRows
{
    template<typename T>
    explicit SRows(const TPodVector<T>& rows) noexcept :
        bytes{ reinterpret_cast<const std::uint8_t*>(rows.data()) }, count{ rows.size() }, stride{ sizeof(T) } {}

    const std::uint8_t* bytes;
    std::size_t count, stride;
};

bool SReportBakeTiming::stamp(std::uint64_t& value) noexcept
{
    clock_failed = clock.read(value).code != platform::threading::EWaitError::none;
    return !clock_failed;
}

bool SReportCandidate::bake(CBakedDocumentBlock& result, const schema::SDataOperationLimits limits,
    schema::SSchemaDiagnostic& diagnostic, SReportBakeTiming* const timing) const noexcept
{
    CLiveDocument doc;
    if (!document_parser::parse(reports::schema_source(), doc).accepted())
    {
        return false;
    }
    schema::CResolvedTypes types;
    schema::SDiagnostic type_error;
    if (!types.resolve(doc, type_error) || (timing && !timing->stamp(timing->prepared)))
    {
        return false;
    }
    const CNodeKey instances = object(doc, doc.root(), "instances");
    const CNodeKey group = object(doc, instances, "PlatformReportHeader");
    const CNodeKey report = object(doc, group, "report");
    const auto header_type = types.find_type(CStringView{ "PlatformReportHeader" });
    if (!child(doc, report, logical_value(doc, types, header_type, reinterpret_cast<const std::uint8_t*>(&header),
        text, CStringView{ "declaration" })).is_valid())
    {
        return false;
    }
    const CNodeKey data = object(doc, doc.root(), "data");
    const SRows tables[] = { SRows{ adapters }, SRows{ sources }, SRows{ displays }, SRows{ paths }, SRows{ catalogues },
        SRows{ modes }, SRows{ identities }, SRows{ bytes }, SRows{ queries } };
    for (std::size_t n = 0u; n < std::extent_v<decltype(tables)>; ++n)
    {
        const SRows& rows = tables[n];
        if (!rows.count) continue;
        const schema::CTypesIndex type = types.find_type(CStringView{ k_collections[n].type });
        schema::SType layout;
        if (!types.type(type, layout) || (rows.stride != layout.size))
        {
            return false;
        }
        const CNodeKey entry = object(doc, object(doc, data, k_collections[n].type), k_collections[n].name);
        const CNodeKey array = child(doc, entry, doc.create_array(CStringView{ "data" }));
        for (std::size_t i = 0u; i < rows.count; ++i)
        {
            if (!child(doc, array, logical_value(doc, types, type, (rows.bytes + (i * rows.stride)), text)).is_valid())
            {
                return false;
            }
        }
    }
    types.clear();
    schema::CSchema authored;
    schema::CBakedDataBlock block;
    return authored.try_adopt(std::move(doc), diagnostic) && (!timing || timing->stamp(timing->authored)) &&
        authored.bake(block, limits, diagnostic) && (!timing || timing->stamp(timing->baked)) && block.release_local(result);
}

}   //  namespace platform::runtime
