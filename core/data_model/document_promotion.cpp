
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_promotion.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Sep 26
//
//  Translation from a checked baked view to a mutable live document.

#include "data_model/document_translation.hpp"
#include "data_model/document_copy.hpp"

#include <limits>
#include <utility>

#include "containers/TPodVector.hpp"
#include "data_model/baked_document.hpp"
#include "data_model/live_document.hpp"

class CLiveDocumentPromoter
{
public:
    explicit CLiveDocumentPromoter(const CBakedDocument& source) noexcept;
    CLiveDocumentPromoter(const CBakedDocument& source, const CStringView& member_name) noexcept;

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

    const CBakedDocument& m_source;
    CStringView m_member_name;
    CBakedValueIndex m_selected_member;
    bool m_select_root_member{ false };
    CLiveDocument m_destination;
    TPodVector<SPromotedValue> m_values;
    std::uint32_t m_live_node_count{ 0u };
};

CLiveDocumentPromoter::CLiveDocumentPromoter(const CBakedDocument& source) noexcept : m_source(source)
{
}

CLiveDocumentPromoter::CLiveDocumentPromoter(const CBakedDocument& source, const CStringView& member_name) noexcept :
    m_source(source), m_member_name(member_name), m_select_root_member(true)
{
}

bool CLiveDocumentPromoter::build() noexcept
{
    if (!m_source.is_ready() || !prepare_values() || !m_destination.initialise(m_live_node_count))
    {
        return false;
    }

    m_values[0u].destination = m_destination.root();
    if (!m_destination.set_root_type(m_source.value_type(m_source.root()) == EBakedValueType::array ?
        ELiveValueType::array : ELiveValueType::object))
    {
        return false;
    }
    for (std::uint32_t index = 1u; index < m_values.size(); ++index)
    {
        SPromotedValue& value = m_values[index];
        value.destination = document_translation::detail::create_value_copy(
            m_destination, m_source, value.source, m_source.name(value.source));
        if (!value.destination.is_valid())
        {
            return false;
        }
        if (!m_destination.append_child(m_values[value.parent_index].destination, value.destination).succeeded())
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
    if (m_select_root_member)
    {
        if (m_source.value_type(m_source.root()) != EBakedValueType::object)
        {
            return false;
        }
        m_selected_member = m_source.object_child(m_source.root(), m_member_name);
        if (!m_values.push_back(SPromotedValue{
            m_source.root(), CNodeKey{}, std::numeric_limits<std::uint32_t>::max() }))
        {
            return false;
        }
    }
    else if (!m_values.resize(m_source.value_count()))
    {
        return false;
    }

    if (!m_select_root_member)
    {
        m_values[0u] = SPromotedValue{ m_source.root(), CNodeKey{}, std::numeric_limits<std::uint32_t>::max() };
    }
    std::uint32_t next_value = 1u;
    std::uint64_t live_node_count = m_select_root_member ? 1u : m_source.value_count();
    for (std::uint32_t index = 0u; index < next_value; ++index)
    {
        const EBakedValueType type = m_source.value_type(m_values[index].source);
        if ((type == EBakedValueType::array) || (type == EBakedValueType::object))
        {
            ++live_node_count;
        }
        CBakedValueIndex child = (m_select_root_member && (index == 0u)) ?
            m_selected_member : m_source.first_child(m_values[index].source);
        for (; child.is_valid(); child = (m_select_root_member && (index == 0u)) ?
            CBakedValueIndex{} : m_source.next_sibling(child))
        {
            if (next_value >= m_source.value_count())
            {
                return false;
            }
            const SPromotedValue promoted{ child, CNodeKey{}, index };
            if (m_select_root_member)
            {
                if (!m_values.push_back(promoted))
                {
                    return false;
                }
                ++live_node_count;
            }
            else
            {
                m_values[next_value] = promoted;
            }
            ++next_value;
        }
    }
    if ((!m_select_root_member && (next_value != m_source.value_count())) ||
        (live_node_count > std::numeric_limits<std::uint32_t>::max()))
    {
        return false;
    }
    m_live_node_count = static_cast<std::uint32_t>(live_node_count);
    return true;
}

bool document_translation::promote(const CBakedDocument& source, CLiveDocument& destination) noexcept
{
    CLiveDocumentPromoter promoter{ source };
    if (!promoter.build())
    {
        return false;
    }

    destination = promoter.take_document();
    return true;
}

bool document_translation::promote_root_member(const CBakedDocument& source, const CStringView& member_name, CLiveDocument& destination) noexcept
{
    CLiveDocumentPromoter promoter{ source, member_name };
    if (!promoter.build())
    {
        return false;
    }

    destination = promoter.take_document();
    return true;
}
