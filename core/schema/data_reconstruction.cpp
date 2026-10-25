
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_reconstruction.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Oct 26
//
//  Complete bulk record reconstruction for section-only schema import.

#include "schema/data_reconstruction.hpp"
#include "schema/value_codec.hpp"
#include "data_model/document_copy.hpp"

namespace schema::detail
{

class CBulkDataReconstructor
{
public:
    CBulkDataReconstructor(CLiveDocument& document, const CResolvedTypes& schema, EBulkDataLoadReason& reason, const unsigned depth_limit) noexcept :
        m_document(document), m_schema(schema), m_reason(reason), m_depth_limit(depth_limit) {}

    [[nodiscard]] bool records(const CNodeKey entry, const CTypesIndex type,
        const std::uint8_t* const bytes, const std::uint32_t count) noexcept;

private:
    [[nodiscard]] CNodeKey scalar(const CTypesIndex type, const std::uint64_t bits,
        const unsigned width, const CStringView& name) noexcept;
    [[nodiscard]] CNodeKey value(const CTypesIndex type, const std::uint8_t* const bytes,
        const CStringView& name, const bool in_array, const unsigned depth) noexcept;

    CLiveDocument& m_document;
    const CResolvedTypes& m_schema;
    EBulkDataLoadReason& m_reason;
    const unsigned m_depth_limit;
};

bool CBulkDataReconstructor::records(const CNodeKey entry, const CTypesIndex type,
    const std::uint8_t* const bytes, const std::uint32_t count) noexcept
{
    SType layout;
    if (!m_schema.type(type, layout))
    {
        m_reason = EBulkDataLoadReason::incompatible_schema;
        return false;
    }

    //  Every explicit record needs at least one node; reject impossible expansions before traversal.
    if ((count > static_cast<std::uint32_t>(INT32_MAX)) ||
        (count > (memory::k_byte_size_ceiling / sizeof(CLiveNode))))
    {
        m_reason = EBulkDataLoadReason::invalid_range;
        return false;
    }
    const CNodeKey array = m_document.create_array(CStringView{ "data" });
    if (!document_translation::detail::CDocumentConstruction::append(m_document, entry, array))
    {
        m_reason = EBulkDataLoadReason::allocation_failed;
        return false;
    }
    for (std::uint32_t ordinal = 0u; ordinal < count; ++ordinal)
    {
        const std::uint8_t* const original = layout.size ? (bytes + (layout.size * ordinal)) : nullptr;
        const CNodeKey item = value(type, original, {}, true, 0u);
        if (!item || !document_translation::detail::CDocumentConstruction::append(m_document, array, item))
        {
            if (m_reason == EBulkDataLoadReason::none)
            {
                m_reason = EBulkDataLoadReason::allocation_failed;
            }
            return false;
        }
        detail::SValueDiagnostic error;
        bool matches{};
        if (!detail::check_value(m_schema, detail::CDocumentRead{ m_document }, type, detail::SOccurrence{ item },
            original, static_cast<std::size_t>(layout.size), detail::EConstructionMode::complete_bulk_data, matches, error) || !matches)
        {
            m_reason = detail::value_failure(error, EBulkDataLoadReason::unrepresentable_value);
            return false;
        }
    }
    return true;
}

CNodeKey CBulkDataReconstructor::scalar(const CTypesIndex type, const std::uint64_t bits,
    const unsigned width, const CStringView& name) noexcept
{
    EScalarDecodeReason decoded;
    const CNodeKey value = decode_document_scalar(m_document, m_schema, type, bits, width, name, decoded);
    if (!value)
    {
        m_reason = (decoded == EScalarDecodeReason::invalid_type) ? EBulkDataLoadReason::incompatible_schema :
            ((decoded == EScalarDecodeReason::unrepresentable_value) ? EBulkDataLoadReason::unrepresentable_value :
                EBulkDataLoadReason::allocation_failed);
    }
    return value;
}

CNodeKey CBulkDataReconstructor::value(const CTypesIndex type, const std::uint8_t* const bytes,
    const CStringView& name, const bool in_array, const unsigned depth) noexcept
{
    if (depth >= m_depth_limit)
    {
        m_reason = EBulkDataLoadReason::invalid_range;
        return {};
    }
    SType layout;
    if ((depth >= document_limits::k_max_recursion_depth) || !m_schema.type(type, layout))
    {
        m_reason = EBulkDataLoadReason::incompatible_schema;
        return {};
    }
    if ((layout.category == ECategory::primitive) || (layout.category == ECategory::enumeration))
    {
        const CNodeKey decoded = scalar(type,
            read_scalar_bits(bytes, static_cast<std::size_t>(layout.size)),
            static_cast<unsigned>(layout.size * 8u), name);
        if (!decoded && (m_reason == EBulkDataLoadReason::none))
        {
            m_reason = EBulkDataLoadReason::allocation_failed;
        }
        return decoded;
    }

    //  Keep the established explicit positional output for anonymous
    //  one-member compounds; input also accepts named and scalar forms.
    const bool positional = in_array && (layout.count == 1u) &&
        ((layout.category == ECategory::structure) || (layout.category == ECategory::bit_structure));
    const bool array = (layout.category == ECategory::array) || positional;
    const CNodeKey result = array ? m_document.create_array(name) : m_document.create_object(name);
    if (!result)
    {
        m_reason = EBulkDataLoadReason::allocation_failed;
        return {};
    }
    for (std::uint32_t index = 0u; index < layout.count; ++index)
    {
        CNodeKey child;
        if (layout.category == ECategory::structure)
        {
            SMember member;
            if (!m_schema.member(m_schema.member_at(type, index), member))
            {
                m_reason = EBulkDataLoadReason::incompatible_schema;
            }
            else
            {
                child = value(member.type, (bytes ? (bytes + member.offset) : nullptr),
                    (positional ? CStringView{} : m_schema.name(member.name)), positional, (depth + 1u));
            }
        }
        else if (layout.category == ECategory::array)
        {
            child = value(layout.element_or_storage,
                (bytes ? (bytes + (layout.stride * index)) : nullptr), {}, true, (depth + 1u));
        }
        else if (layout.category == ECategory::bit_structure)
        {
            SField field;
            if (!m_schema.field(m_schema.field_at(type, index), field))
            {
                m_reason = EBulkDataLoadReason::incompatible_schema;
            }
            else
            {
                if ((depth + 1u) >= m_depth_limit)
                {
                    m_reason = EBulkDataLoadReason::invalid_range;
                    (void)m_document.erase(result);
                    return {};
                }
                const std::uint64_t bits = (read_scalar_bits(bytes, static_cast<std::size_t>(layout.size)) & field.mask) >> field.shift;
                child = scalar(field.type, bits, field.width, (positional ? CStringView{} : m_schema.name(field.name)));
            }
        }
        else
        {
            m_reason = EBulkDataLoadReason::incompatible_schema;
        }
        if (!child || !document_translation::detail::CDocumentConstruction::append(m_document, result, child))
        {
            if (m_reason == EBulkDataLoadReason::none)
            {
                m_reason = EBulkDataLoadReason::allocation_failed;
            }
            (void)m_document.erase(result);
            return {};
        }
    }
    return result;
}



bool reconstruct_bulk_records(CLiveDocument& document, const CResolvedTypes& schema,
    const CNodeKey entry, const CTypesIndex type, const std::uint8_t* const bytes,
    const std::uint32_t count, EBulkDataLoadReason& reason, const unsigned depth_limit) noexcept
{
    reason = EBulkDataLoadReason::none;
    CBulkDataReconstructor reconstructor{ document, schema, reason, depth_limit };
    return reconstructor.records(entry, type, bytes, count);
}

}   // namespace schema::detail
