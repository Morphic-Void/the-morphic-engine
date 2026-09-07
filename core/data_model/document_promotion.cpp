
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_promotion.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Sep 26
//
//  Translation from a checked baked view to a mutable live document.

#include "data_model/document_translation.hpp"

#include <limits>
#include <utility>

#include "containers/TPodVector.hpp"
#include "data_model/baked_document.hpp"
#include "data_model/live_document.hpp"

class CLiveDocumentPromoter
{
public:
    explicit CLiveDocumentPromoter(const CBakedDocument& source) noexcept;

    [[nodiscard]] bool build() noexcept;
    [[nodiscard]] CLiveDocument take_document() noexcept;

private:
    struct SPromotedValue
    {
        CBakedValueIndex source;
        CNodeKey destination;
        std::uint32_t parent_index;
    };

    [[nodiscard]] bool prepare_values() noexcept;
    [[nodiscard]] CNodeKey create_value(CBakedValueIndex source) noexcept;

    const CBakedDocument& m_source;
    CLiveDocument m_destination;
    TPodVector<SPromotedValue> m_values;
    std::uint32_t m_live_node_count{ 0u };
};

CLiveDocumentPromoter::CLiveDocumentPromoter(const CBakedDocument& source) noexcept :
    m_source(source)
{
}

bool CLiveDocumentPromoter::build() noexcept
{
    if (!m_source.is_ready() || !prepare_values() ||
        !m_destination.initialise(m_live_node_count))
    {
        return false;
    }

    m_values[0u].destination = m_destination.root();
    for (std::uint32_t index = 1u; index < m_source.value_count(); ++index)
    {
        SPromotedValue& value = m_values[index];
        value.destination = create_value(value.source);
        if (!value.destination.is_valid() ||
            !m_destination.append_child(
                m_values[value.parent_index].destination,
                value.destination).succeeded())
        {
            return false;
        }
    }
    return m_destination.check_integrity();
}

CLiveDocument CLiveDocumentPromoter::take_document() noexcept
{
    return std::move(m_destination);
}

bool CLiveDocumentPromoter::prepare_values() noexcept
{
    if (!m_values.resize(m_source.value_count()))
    {
        return false;
    }

    m_values[0u] = SPromotedValue{
        m_source.root(),
        CNodeKey{},
        std::numeric_limits<std::uint32_t>::max() };
    std::uint32_t next_value = 1u;
    std::uint64_t live_node_count = m_source.value_count();
    for (std::uint32_t index = 0u; index < next_value; ++index)
    {
        const EBakedValueType type = m_source.value_type(m_values[index].source);
        if ((type == EBakedValueType::array) || (type == EBakedValueType::object) ||
            (type == EBakedValueType::recovered_array))
        {
            ++live_node_count;
        }
        for (CBakedValueIndex child = m_source.first_child(m_values[index].source);
            child.is_valid();
            child = m_source.next_sibling(child))
        {
            if (next_value >= m_source.value_count())
            {
                return false;
            }
            m_values[next_value++] = SPromotedValue{ child, CNodeKey{}, index };
        }
    }
    if ((next_value != m_source.value_count()) ||
        (live_node_count > std::numeric_limits<std::uint32_t>::max()))
    {
        return false;
    }
    m_live_node_count = static_cast<std::uint32_t>(live_node_count);
    return true;
}

CNodeKey CLiveDocumentPromoter::create_value(const CBakedValueIndex source) noexcept
{
    const CStringView name = m_source.name(source);
    switch (m_source.value_type(source))
    {
        case EBakedValueType::null_value:
        {
            return m_destination.create_null(name);
        }
        case EBakedValueType::boolean:
        {
            bool value = false;
            return m_source.boolean_value(source, value) ?
                m_destination.create_boolean(value, name) : CNodeKey{};
        }
        case EBakedValueType::integer:
        {
            CIntegerMetadata metadata;
            if (!m_source.integer_metadata(source, metadata))
            {
                return CNodeKey{};
            }
            if (metadata.domain == EIntegerDomain::unsigned_value)
            {
                std::uint64_t value = 0u;
                return m_source.unsigned_integer_value(source, value) ?
                    m_destination.create_unsigned_integer(value, metadata, name) : CNodeKey{};
            }
            std::int64_t value = 0;
            return m_source.signed_integer_value(source, value) ?
                m_destination.create_signed_integer(value, metadata, name) : CNodeKey{};
        }
        case EBakedValueType::floating_point:
        {
            double value = 0.0;
            return m_source.floating_point_value(source, value) ?
                m_destination.create_floating_point(value, name) : CNodeKey{};
        }
        case EBakedValueType::string:
        {
            return m_destination.create_string(m_source.string_value(source), name);
        }
        case EBakedValueType::array:
        {
            return m_destination.create_array(name);
        }
        case EBakedValueType::object:
        {
            return m_destination.create_object(name);
        }
        case EBakedValueType::recovered_array:
        {
            return m_destination.create_recovered_array(name);
        }
        default:
        {
            return CNodeKey{};
        }
    }
}

bool document_translation::promote(
    const CBakedDocument& source,
    CLiveDocument& destination) noexcept
{
    CLiveDocumentPromoter promoter{ source };
    if (!promoter.build())
    {
        return false;
    }

    destination = promoter.take_document();
    return true;
}
