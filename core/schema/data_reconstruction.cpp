
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_reconstruction.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Oct 26
//
//  Shared minimal instance and complete bulk record reconstruction.

#include "schema/data_reconstruction.hpp"
#include "schema/value_codec.hpp"
#include "data_model/document_copy.hpp"

namespace schema::detail
{

//  Compare meaningful encoded values; old declarations are not an input.
class CInstanceReconciler
{
public:
    CInstanceReconciler(CLiveDocument& document, const CResolvedSchema& schema,
        EInstanceLoadReason& reason, const unsigned depth_limit) noexcept :
        m_document(document), m_schema(schema), m_reason(reason), m_depth_limit(depth_limit)
    {
    }

    [[nodiscard]] CNodeKey decode(const CSchemaIndex type, const std::uint8_t* const bytes,
        const std::uint8_t* const baseline, const CStringView name) noexcept
    {
        return decode(type, bytes, baseline, name, nullptr, 0u);
    }

private:
    //  Stack-local output state. An equal subtree allocates nothing. Before a
    //  changed descendant is emitted, ensure ancestors and preceding array slots
    //  in output order; no subtree is compared again to make that decision.
    struct SFrame
    {
        SFrame* parent;
        SType layout;
        const std::uint8_t* bytes;
        CStringView name;
        CNodeKey output;
        std::uint32_t current{}, emitted{};
        unsigned depth{};
    };

    [[nodiscard]] CNodeKey decode_scalar(const CSchemaIndex type, const std::uint64_t bits, const unsigned width, const CStringView& name) noexcept;
    [[nodiscard]] CNodeKey equal_element(const CSchemaIndex type, const SType& layout, const std::uint8_t* const bytes, const unsigned depth) noexcept;
    [[nodiscard]] bool ensure_before_child(SFrame& frame) noexcept;
    [[nodiscard]] CNodeKey decode(const CSchemaIndex type, const std::uint8_t* const bytes,
        const std::uint8_t* const baseline, const CStringView name, SFrame* const parent, const unsigned depth) noexcept;

    CLiveDocument& m_document;
    const CResolvedSchema& m_schema;
    EInstanceLoadReason& m_reason;
    const unsigned m_depth_limit;
};

CNodeKey CInstanceReconciler::decode_scalar(const CSchemaIndex type, const std::uint64_t bits, const unsigned width, const CStringView& name) noexcept
{
    EScalarDecodeReason decoded;
    const CNodeKey value = decode_document_scalar(m_document, m_schema, type, bits, width, name, decoded);
    if (!value)
    {
        m_reason = (decoded == EScalarDecodeReason::allocation_failed) ?
            EInstanceLoadReason::allocation_failed : EInstanceLoadReason::unrepresentable_value;
    }
    return value;
}

CNodeKey CInstanceReconciler::equal_element(const CSchemaIndex type, const SType& layout, const std::uint8_t* const bytes, const unsigned depth) noexcept
{
    if (depth >= m_depth_limit)
    {
        m_reason = EInstanceLoadReason::invalid_range;
        return {};
    }
    if ((layout.category == ECategory::primitive) || (layout.category == ECategory::enumeration))
    {
        return decode_scalar(type, read_scalar_bits(bytes, static_cast<std::size_t>(layout.size)),
            static_cast<unsigned>(layout.size * 8u), {});
    }
    const CNodeKey result = (layout.category == ECategory::array) ? m_document.create_array() : m_document.create_object();
    if (!result) m_reason = EInstanceLoadReason::allocation_failed;
    return result;
}

bool CInstanceReconciler::ensure_before_child(SFrame& frame) noexcept
{
    if (!frame.output)
    {
        if (frame.depth >= m_depth_limit)
        {
            m_reason = EInstanceLoadReason::invalid_range;
            return false;
        }
        if (frame.parent && !ensure_before_child(*frame.parent))
        {
            return false;
        }
        frame.output = (frame.layout.category == ECategory::array) ?
            m_document.create_array(frame.name) : m_document.create_object(frame.name);
        if (!frame.output)
        {
            m_reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
    }
    if ((frame.layout.category == ECategory::array) && (frame.emitted < frame.current))
    {
        SType element;
        (void)m_schema.type(frame.layout.element_or_storage, element);
        for (; frame.emitted < frame.current; ++frame.emitted)
        {
            const std::size_t offset = static_cast<std::size_t>(frame.layout.stride * frame.emitted);
            const CNodeKey child = equal_element(frame.layout.element_or_storage, element,
                (frame.bytes ? (frame.bytes + offset) : nullptr), (frame.depth + 1u));
            if (!child || !document_translation::detail::CDocumentConstruction::append(m_document, frame.output, child))
            {
                if (m_reason == EInstanceLoadReason::none)
                {
                    m_reason = EInstanceLoadReason::allocation_failed;
                }
                return false;
            }
        }
    }
    return true;
}

CNodeKey CInstanceReconciler::decode(const CSchemaIndex type, const std::uint8_t* const bytes,
    const std::uint8_t* const baseline, const CStringView name, SFrame* const parent, const unsigned depth) noexcept
{
    SType layout;
    if ((depth >= document_limits::k_max_recursion_depth) || !m_schema.type(type, layout))
    {
        m_reason = EInstanceLoadReason::unrepresentable_value;
        return {};
    }
    const std::size_t size = static_cast<std::size_t>(layout.size);

    //  In particular, never expand enormous arrays of zero-size elements.
    if (size == 0u)
    {
        return {};
    }
    if ((layout.category == ECategory::primitive) || (layout.category == ECategory::enumeration))
    {
        if (detail::compare_encoded(m_schema, type, bytes, size, baseline, size))
        {
            return {};
        }
        if (parent && !ensure_before_child(*parent))
        {
            return {};
        }
        if (depth >= m_depth_limit)
        {
            m_reason = EInstanceLoadReason::invalid_range;
            return {};
        }
        return decode_scalar(type, read_scalar_bits(bytes, size), static_cast<unsigned>(size * 8u), name);
    }
    SFrame frame{ parent, layout, bytes, name, {}, 0u, 0u, depth };
    const bool array = layout.category == ECategory::array;
    for (; (frame.current < layout.count) && (m_reason == EInstanceLoadReason::none); ++frame.current)
    {
        CNodeKey child;
        if (layout.category == ECategory::bit_structure)
        {
            SField field;
            (void)m_schema.field(m_schema.field_at(type, frame.current), field);
            const std::uint64_t actual = read_scalar_bits(bytes, size) & field.mask;
            if (actual != (read_scalar_bits(baseline, size) & field.mask))
            {
                if (!ensure_before_child(frame))
                {
                    break;
                }
                if ((depth + 1u) >= m_depth_limit)
                {
                    m_reason = EInstanceLoadReason::invalid_range;
                    break;
                }
                child = decode_scalar(field.type, (actual >> field.shift), field.width, m_schema.name(field.name));
            }
        }
        else
        {
            CSchemaIndex child_type = layout.element_or_storage;
            std::size_t offset = static_cast<std::size_t>(layout.stride * frame.current);
            CStringView child_name;
            if (!array)
            {
                SMember member;
                (void)m_schema.member(m_schema.member_at(type, frame.current), member);
                child_type = member.type;
                offset = static_cast<std::size_t>(member.offset);
                child_name = m_schema.name(member.name);
            }
            child = decode(child_type, (bytes ? (bytes + offset) : nullptr), (baseline ? (baseline + offset) : nullptr),
                child_name, &frame, (depth + 1u));
        }
        if (child)
        {
            if (!document_translation::detail::CDocumentConstruction::append(m_document, frame.output, child))
            {
                m_reason = EInstanceLoadReason::allocation_failed;
            }
            frame.emitted = frame.current + 1u;
        }
    }
    if (m_reason != EInstanceLoadReason::none)
    {
        if (frame.output) (void)m_document.erase(frame.output);
        return {};
    }
    return frame.output;
}

//  Output helpers keep all construction within the staged document.

class CBulkDataReconstructor
{
public:
    CBulkDataReconstructor(CLiveDocument& document, const CResolvedSchema& schema, EBulkDataLoadReason& reason, const unsigned depth_limit) noexcept :
        m_document(document), m_schema(schema), m_reason(reason), m_depth_limit(depth_limit) {}

    [[nodiscard]] bool records(const CNodeKey entry, const CSchemaIndex type,
        const std::uint8_t* const bytes, const std::uint32_t count) noexcept;

private:
    [[nodiscard]] CNodeKey scalar(const CSchemaIndex type, const std::uint64_t bits,
        const unsigned width, const CStringView& name) noexcept;
    [[nodiscard]] CNodeKey value(const CSchemaIndex type, const std::uint8_t* const bytes,
        const CStringView& name, const bool in_array, const unsigned depth) noexcept;

    CLiveDocument& m_document;
    const CResolvedSchema& m_schema;
    EBulkDataLoadReason& m_reason;
    const unsigned m_depth_limit;
};

bool CBulkDataReconstructor::records(const CNodeKey entry, const CSchemaIndex type,
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

CNodeKey CBulkDataReconstructor::scalar(const CSchemaIndex type, const std::uint64_t bits,
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

CNodeKey CBulkDataReconstructor::value(const CSchemaIndex type, const std::uint8_t* const bytes,
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

bool reconstruct_instance(CLiveDocument& document, const CResolvedSchema& schema,
    const CSchemaIndex type, const std::uint8_t* const bytes, const std::uint8_t* const baseline,
    const std::size_t size, const bool base, CNodeKey& result, EInstanceLoadReason& reason,
    const unsigned depth_limit) noexcept
{
    result = {};
    reason = EInstanceLoadReason::none;
    CInstanceReconciler reconciler{ document, schema, reason, depth_limit };
    const CNodeKey value = reconciler.decode(type, bytes, baseline, CStringView{ "declaration" });
    if (reason != EInstanceLoadReason::none)
    {
        return false;
    }
    SValueDiagnostic error;
    bool matches{};
    const CDocumentRead read{ document };
    const bool checked = base ? check_value(schema, read, type, SOccurrence{ value }, bytes, size,
        EConstructionMode::instance, matches, error) :
        check_alternative(schema, read, type, SOccurrence{ value }, baseline, size, bytes, size, matches, error);
    if (!checked || !matches)
    {
        if (value) (void)document.erase(value);
        reason = value_failure(error, EInstanceLoadReason::unrepresentable_value);
        return false;
    }
    result = value;
    return true;
}

bool reconstruct_bulk_records(CLiveDocument& document, const CResolvedSchema& schema,
    const CNodeKey entry, const CSchemaIndex type, const std::uint8_t* const bytes,
    const std::uint32_t count, EBulkDataLoadReason& reason, const unsigned depth_limit) noexcept
{
    reason = EBulkDataLoadReason::none;
    CBulkDataReconstructor reconstructor{ document, schema, reason, depth_limit };
    return reconstructor.records(entry, type, bytes, count);
}

}   // namespace schema::detail
