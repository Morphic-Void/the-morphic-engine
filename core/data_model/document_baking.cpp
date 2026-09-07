
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_baking.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Sep 26
//
//  Translation from a live document to an immutable baked block.

#include "data_model/document_translation.hpp"

#include <cstring>
#include <limits>
#include <utility>

#include "containers/TPodVector.hpp"
#include "data_model/baked_document.hpp"
#include "data_model/baked_document_format.hpp"
#include "data_model/live_document.hpp"

class CBakedDocumentBaker
{
public:
    explicit CBakedDocumentBaker(const CLiveDocument& source) noexcept;

    [[nodiscard]] bool build() noexcept;
    void publish(CBakedDocumentBlock& destination) noexcept;

private:
    enum class EStringDomain : std::uint8_t
    {
        property_names,
        string_values,
    };

    struct SStringDomain
    {
        EStringDomain kind;
        TPodVector<std::uint32_t>* live_to_baked;
        std::uint32_t reference_count{ 1u };
        std::uint32_t byte_count{ 1u };
        std::uint32_t references_offset{ 0u };
        std::uint32_t bytes_offset{ 0u };
    };

    struct SLiveString
    {
        std::uint32_t id;
        CStringView value;
    };

    [[nodiscard]] bool prepare() noexcept;
    [[nodiscard]] bool measure_strings(SStringDomain& domain) noexcept;
    [[nodiscard]] bool derive_layout() noexcept;
    [[nodiscard]] bool place_section(
        std::uint64_t& offset,
        std::uint32_t count,
        std::uint32_t stride,
        std::uint32_t& destination) const noexcept;
    [[nodiscard]] bool allocate_output() noexcept;
    void emit_strings(SStringDomain& domain) noexcept;
    [[nodiscard]] bool emit_values() noexcept;
    [[nodiscard]] bool emit_value_payload(
        CNodeKey value,
        SBakedValueRecord& destination) const noexcept;
    [[nodiscard]] static std::uint8_t encode_integer_metadata(
        CIntegerMetadata metadata) noexcept;
    [[nodiscard]] SLiveString live_string_at_rank(
        EStringDomain domain,
        std::uint32_t rank) const noexcept;
    [[nodiscard]] bool validate_output() noexcept;

    const CLiveDocument& m_source;
    SLiveDocumentAnalysis m_analysis;
    SLiveDocumentStringAnalysis m_string_analysis;
    SStringDomain m_property_names;
    SStringDomain m_string_values;
    TPodVector<CNodeKey> m_values;
    CByteBuffer m_bytes;
    CBakedDocument m_document;
    std::uint32_t m_values_offset{ 0u };
    std::uint32_t m_total_size{ 0u };
};

CBakedDocumentBaker::CBakedDocumentBaker(const CLiveDocument& source) noexcept :
    m_source(source),
    m_property_names{
        EStringDomain::property_names,
        &m_string_analysis.property_name_references },
    m_string_values{
        EStringDomain::string_values,
        &m_string_analysis.string_value_references }
{
}

bool CBakedDocumentBaker::build() noexcept
{
    if (!prepare() || !allocate_output())
    {
        return false;
    }

    emit_strings(m_property_names);
    emit_strings(m_string_values);
    return emit_values() && validate_output();
}

bool CBakedDocumentBaker::prepare() noexcept
{
    if (!m_source.check_integrity() || !m_source.analyse(m_analysis, &m_string_analysis))
    {
        return false;
    }
    m_property_names.reference_count =
        m_string_analysis.referenced_property_name_count + 1u;
    m_string_values.reference_count =
        m_string_analysis.referenced_string_value_count + 1u;
    if (!measure_strings(m_property_names) || !measure_strings(m_string_values))
    {
        return false;
    }
    if (!derive_layout() || !m_values.resize(m_analysis.value_count))
    {
        return false;
    }
    m_values[0u] = m_source.root();
    return true;
}

bool CBakedDocumentBaker::measure_strings(SStringDomain& domain) noexcept
{
    const TPodVector<std::uint32_t>& references = *domain.live_to_baked;
    std::uint64_t byte_count = 1u;
    for (std::size_t rank = 1u; rank < references.size(); ++rank)
    {
        const SLiveString string = live_string_at_rank(
            domain.kind, static_cast<std::uint32_t>(rank));
        if (string.id >= references.size())
        {
            return false;
        }
        if (references[string.id] == 0u)
        {
            continue;
        }
        if ((string.value.string() == nullptr) || (string.value.length() == 0u))
        {
            return false;
        }
        byte_count += static_cast<std::uint64_t>(string.value.length()) + 1u;
        if (byte_count > std::numeric_limits<std::uint32_t>::max())
        {
            return false;
        }
    }
    domain.byte_count = static_cast<std::uint32_t>(byte_count);
    return true;
}

bool CBakedDocumentBaker::derive_layout() noexcept
{
    std::uint64_t offset = sizeof(SBakedDocumentHeader);
    if ((m_analysis.value_count == 0u) ||
        !place_section(
            offset, m_analysis.value_count, sizeof(SBakedValueRecord), m_values_offset) ||
        !place_section(
            offset,
            m_property_names.reference_count,
            sizeof(SBakedStringReference),
            m_property_names.references_offset) ||
        !place_section(
            offset,
            m_string_values.reference_count,
            sizeof(SBakedStringReference),
            m_string_values.references_offset) ||
        !place_section(
            offset, m_property_names.byte_count, 1u, m_property_names.bytes_offset) ||
        !place_section(
            offset, m_string_values.byte_count, 1u, m_string_values.bytes_offset))
    {
        return false;
    }
    m_total_size = static_cast<std::uint32_t>(offset);
    return true;
}

bool CBakedDocumentBaker::place_section(
    std::uint64_t& offset,
    const std::uint32_t count,
    const std::uint32_t stride,
    std::uint32_t& destination) const noexcept
{
    if (offset > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    destination = static_cast<std::uint32_t>(offset);
    offset += static_cast<std::uint64_t>(count) * stride;
    return offset <= std::numeric_limits<std::uint32_t>::max();
}

bool CBakedDocumentBaker::allocate_output() noexcept
{
    if (!m_bytes.reallocate(
            m_total_size,
            m_total_size,
            baked_document_format::k_block_alignment))
    {
        return false;
    }
    m_bytes.zero_fill();

    SBakedDocumentHeader& header = *reinterpret_cast<SBakedDocumentHeader*>(m_bytes.data());
    header.magic = baked_document_format::k_magic;
    header.version = baked_document_format::k_version;
    header.header_size = baked_document_format::k_header_size;
    header.total_size = m_total_size;
    header.value_count = m_analysis.value_count;
    header.property_name_reference_count = m_property_names.reference_count;
    header.property_name_byte_count = m_property_names.byte_count;
    header.string_value_reference_count = m_string_values.reference_count;
    header.string_value_byte_count = m_string_values.byte_count;
    return true;
}

void CBakedDocumentBaker::emit_strings(SStringDomain& domain) noexcept
{
    TPodVector<std::uint32_t>& live_to_baked = *domain.live_to_baked;
    SBakedStringReference* const references = reinterpret_cast<SBakedStringReference*>(
        m_bytes.data() + domain.references_offset);
    std::uint8_t* const bytes = m_bytes.data() + domain.bytes_offset;
    references[0u] = SBakedStringReference{ 0u, 0u };
    bytes[0u] = 0u;
    live_to_baked[0u] = 0u;

    std::uint32_t baked_id = 1u;
    std::uint32_t byte_offset = 1u;
    for (std::size_t rank = 1u; rank < live_to_baked.size(); ++rank)
    {
        const SLiveString string = live_string_at_rank(
            domain.kind, static_cast<std::uint32_t>(rank));
        if (live_to_baked[string.id] == 0u)
        {
            continue;
        }
        references[baked_id] = SBakedStringReference{
            byte_offset,
            static_cast<std::uint32_t>(string.value.length()) };
        std::memcpy(
            bytes + byte_offset,
            string.value.string(),
            string.value.length());
        byte_offset += static_cast<std::uint32_t>(string.value.length());
        bytes[byte_offset++] = 0u;
        live_to_baked[string.id] = baked_id++;
    }
}

bool CBakedDocumentBaker::emit_values() noexcept
{
    SBakedValueRecord* const records =
        reinterpret_cast<SBakedValueRecord*>(m_bytes.data() + m_values_offset);
    records[0u].parent_index = baked_document_format::k_invalid_index;
    std::uint32_t next_value = 1u;
    for (std::uint32_t index = 0u; index < m_analysis.value_count; ++index)
    {
        const CNodeKey value = m_values[index];
        SBakedValueRecord& record = records[index];
        record.first_child_index = baked_document_format::k_invalid_index;
        if (!emit_value_payload(value, record))
        {
            return false;
        }

        const std::uint32_t child_count = m_source.child_count(value);
        if (child_count == 0u)
        {
            continue;
        }
        if (child_count > (m_analysis.value_count - next_value))
        {
            return false;
        }

        record.first_child_index = next_value;
        record.child_count = child_count;
        CNodeKey child = m_source.first_child(value);
        for (std::uint32_t ordinal = 0u; ordinal < child_count; ++ordinal)
        {
            if (!child.is_valid())
            {
                return false;
            }
            m_values[next_value] = child;
            records[next_value].parent_index = index;
            if (ordinal == 0u)
            {
                records[next_value].value_flags |= baked_document_format::k_first_sibling_flag;
            }
            if ((ordinal + 1u) == child_count)
            {
                records[next_value].value_flags |= baked_document_format::k_last_sibling_flag;
            }
            ++next_value;
            child = m_source.next_sibling(child);
        }
    }
    return next_value == m_analysis.value_count;
}

bool CBakedDocumentBaker::emit_value_payload(
    const CNodeKey value,
    SBakedValueRecord& destination) const noexcept
{
    const CPropertyNameId name = m_source.name_id(value);
    if (!name.is_valid() || (name.query_value() >= m_property_names.live_to_baked->size()))
    {
        return false;
    }
    destination.property_name_index = (*m_property_names.live_to_baked)[name.query_value()];

    switch (m_source.value_type(value))
    {
        case ELiveValueType::empty:
        case ELiveValueType::null_value:
        {
            destination.value_type = EBakedValueType::null_value;
            return true;
        }
        case ELiveValueType::boolean:
        {
            bool result = false;
            if (!m_source.boolean_value(value, result))
            {
                return false;
            }
            destination.value_type = EBakedValueType::boolean;
            destination.payload_bits = result ? 1u : 0u;
            return true;
        }
        case ELiveValueType::integer:
        {
            CIntegerMetadata metadata;
            if (!m_source.integer_metadata(value, metadata))
            {
                return false;
            }
            destination.value_type = EBakedValueType::integer;
            destination.value_flags |= encode_integer_metadata(metadata);
            if (metadata.domain == EIntegerDomain::unsigned_value)
            {
                return m_source.unsigned_integer_value(value, destination.payload_bits);
            }
            std::int64_t result = 0;
            if (!m_source.signed_integer_value(value, result))
            {
                return false;
            }
            destination.payload_bits = live_signed_integer_bits(result);
            return true;
        }
        case ELiveValueType::floating_point:
        {
            double result = 0.0;
            if (!m_source.floating_point_value(value, result))
            {
                return false;
            }
            destination.value_type = EBakedValueType::floating_point;
            destination.payload_bits = live_floating_point_bits(result);
            return true;
        }
        case ELiveValueType::string:
        {
            const CStringValueId string = m_source.string_value_id(value);
            if (!string.is_valid() ||
                (string.query_value() >= m_string_values.live_to_baked->size()))
            {
                return false;
            }
            destination.value_type = EBakedValueType::string;
            destination.payload_bits = (*m_string_values.live_to_baked)[string.query_value()];
            return true;
        }
        case ELiveValueType::array:
        {
            destination.value_type = EBakedValueType::array;
            return true;
        }
        case ELiveValueType::object:
        {
            destination.value_type = EBakedValueType::object;
            return true;
        }
        case ELiveValueType::recovered_array:
        {
            destination.value_type = EBakedValueType::recovered_array;
            return true;
        }
        default:
        {
            return false;
        }
    }
}

std::uint8_t CBakedDocumentBaker::encode_integer_metadata(
    const CIntegerMetadata metadata) noexcept
{
    return
        ((metadata.domain == EIntegerDomain::unsigned_value) ? 0x01u : 0u) |
        (static_cast<std::uint8_t>(metadata.width) << 1u) |
        (static_cast<std::uint8_t>(metadata.notation) << 3u) |
        ((metadata.prefix == EIntegerPrefix::alternate) ? 0x20u : 0u);
}

CBakedDocumentBaker::SLiveString CBakedDocumentBaker::live_string_at_rank(
    const EStringDomain domain,
    const std::uint32_t rank) const noexcept
{
    if (domain == EStringDomain::property_names)
    {
        const CPropertyNameId id = m_source.property_name_id_at_rank(rank);
        return SLiveString{ id.query_value(), m_source.property_name(id) };
    }
    const CStringValueId id = m_source.string_value_id_at_rank(rank);
    return SLiveString{ id.query_value(), m_source.string_value(id) };
}

bool CBakedDocumentBaker::validate_output() noexcept
{
    return m_document.reset(m_bytes.data(), m_bytes.size());
}

void CBakedDocumentBaker::publish(CBakedDocumentBlock& destination) noexcept
{
    destination.m_document = m_document;
    destination.m_bytes = std::move(m_bytes);
}

bool document_translation::bake(
    const CLiveDocument& source,
    CBakedDocumentBlock& destination) noexcept
{
    CBakedDocumentBaker baker{ source };
    if (!baker.build())
    {
        return false;
    }

    baker.publish(destination);
    return true;
}
