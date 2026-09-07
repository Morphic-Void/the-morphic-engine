
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_document.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Sep 26
//
//  Baking, validation and observation of immutable baked documents.

#include "data_model/baked_document.hpp"
#include "data_model/baked_document_format.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include "containers/TPodVector.hpp"
#include "data_model/live_document.hpp"
#include "text/utf8_string.hpp"

class CBakedDocumentBaker
{
public:
    explicit CBakedDocumentBaker(const CLiveDocument& source) noexcept;

    [[nodiscard]] bool build() noexcept;
    [[nodiscard]] CByteBuffer take_bytes() noexcept;
    [[nodiscard]] const CBakedDocument& document() const noexcept;

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

CByteBuffer CBakedDocumentBaker::take_bytes() noexcept
{
    return std::move(m_bytes);
}

const CBakedDocument& CBakedDocumentBaker::document() const noexcept
{
    return m_document;
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

CBakedDocument::CBakedDocument(const void* const bytes, const std::size_t byte_count) noexcept
{
    (void)reset(bytes, byte_count);
}

bool CBakedDocument::reset(const void* const bytes, const std::size_t byte_count) noexcept
{
    clear();
    const std::uint8_t* const candidate = static_cast<const std::uint8_t*>(bytes);
    if (!validate(candidate, byte_count))
    {
        return false;
    }
    m_bytes = candidate;
    m_byte_count = byte_count;
    return true;
}

void CBakedDocument::clear() noexcept
{
    m_bytes = nullptr;
    m_byte_count = 0u;
}

bool CBakedDocument::is_ready() const noexcept
{
    return m_bytes != nullptr;
}

bool CBakedDocument::check_integrity() const noexcept
{
    return is_ready() && validate(m_bytes, m_byte_count);
}

bool CBakedDocument::is_canonical() const noexcept
{
    return is_ready() && !contains_recovered_content();
}

bool CBakedDocument::contains_recovered_content() const noexcept
{
    if (!is_ready())
    {
        return false;
    }
    SLayout layout;
    if (!derive_layout(*header(), m_byte_count, layout))
    {
        return false;
    }
    const SBakedValueRecord* const records = values(layout);
    for (std::uint32_t index = 0u; index < header()->value_count; ++index)
    {
        if (records[index].value_type == EBakedValueType::recovered_array)
        {
            return true;
        }
    }
    return false;
}

std::size_t CBakedDocument::byte_count() const noexcept
{
    return is_ready() ? m_byte_count : 0u;
}

CBakedValueIndex CBakedDocument::root() const noexcept
{
    return is_ready() ? CBakedValueIndex{ 0u } : CBakedValueIndex{};
}

std::uint32_t CBakedDocument::value_count() const noexcept
{
    return is_ready() ? header()->value_count : 0u;
}

std::uint32_t CBakedDocument::property_name_count() const noexcept
{
    return is_ready() ? (header()->property_name_reference_count - 1u) : 0u;
}

std::uint32_t CBakedDocument::string_value_count() const noexcept
{
    return is_ready() ? (header()->string_value_reference_count - 1u) : 0u;
}

bool CBakedDocument::contains(const CBakedValueIndex value) const noexcept
{
    return value_record(value) != nullptr;
}

EBakedValueType CBakedDocument::value_type(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return (record != nullptr) ? record->value_type : EBakedValueType::invalid;
}

bool CBakedDocument::is_object_entry(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return (record != nullptr) && (record->property_name_index != 0u);
}

CPropertyNameId CBakedDocument::name_id(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return (record != nullptr) ? CPropertyNameId{ record->property_name_index } : CPropertyNameId{};
}

CStringView CBakedDocument::name(const CBakedValueIndex value) const noexcept
{
    return property_name(name_id(value));
}

CPropertyNameId CBakedDocument::property_name_id_at_rank(const std::uint32_t rank) const noexcept
{
    return (is_ready() && (rank < header()->property_name_reference_count)) ?
        CPropertyNameId{ rank } : CPropertyNameId{};
}

CStringValueId CBakedDocument::string_value_id_at_rank(const std::uint32_t rank) const noexcept
{
    return (is_ready() && (rank < header()->string_value_reference_count)) ?
        CStringValueId{ rank } : CStringValueId{};
}

CStringView CBakedDocument::property_name(const CPropertyNameId id) const noexcept
{
    if (!is_ready() || !id.is_valid())
    {
        return CStringView{};
    }
    SLayout layout;
    return derive_layout(*header(), m_byte_count, layout) ?
        string_from(
            id.query_value(),
            layout.property_name_references_offset,
            header()->property_name_reference_count,
            layout.property_name_bytes_offset) : CStringView{};
}

CStringView CBakedDocument::string_value(const CStringValueId id) const noexcept
{
    if (!is_ready() || !id.is_valid())
    {
        return CStringView{};
    }
    SLayout layout;
    return derive_layout(*header(), m_byte_count, layout) ?
        string_from(
            id.query_value(),
            layout.string_value_references_offset,
            header()->string_value_reference_count,
            layout.string_value_bytes_offset) : CStringView{};
}

CBakedValueIndex CBakedDocument::parent(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return ((record != nullptr) && (record->parent_index != baked_document_format::k_invalid_index)) ?
        CBakedValueIndex{ record->parent_index } : CBakedValueIndex{};
}

CBakedValueIndex CBakedDocument::previous_sibling(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return ((record != nullptr) &&
        (record->parent_index != baked_document_format::k_invalid_index) &&
        ((record->value_flags & baked_document_format::k_first_sibling_flag) == 0u)) ?
        CBakedValueIndex{ value.query_value() - 1u } : CBakedValueIndex{};
}

CBakedValueIndex CBakedDocument::next_sibling(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return ((record != nullptr) &&
        (record->parent_index != baked_document_format::k_invalid_index) &&
        ((record->value_flags & baked_document_format::k_last_sibling_flag) == 0u)) ?
        CBakedValueIndex{ value.query_value() + 1u } : CBakedValueIndex{};
}

std::uint32_t CBakedDocument::child_count(const CBakedValueIndex container_value) const noexcept
{
    const SBakedValueRecord* const record = value_record(container_value);
    return ((record != nullptr) && value_type_is_container(record->value_type)) ? record->child_count : 0u;
}

CBakedValueIndex CBakedDocument::first_child(const CBakedValueIndex container_value) const noexcept
{
    const SBakedValueRecord* const record = value_record(container_value);
    return ((record != nullptr) && value_type_is_container(record->value_type) && (record->child_count != 0u)) ?
        CBakedValueIndex{ record->first_child_index } : CBakedValueIndex{};
}

CBakedValueIndex CBakedDocument::last_child(const CBakedValueIndex container_value) const noexcept
{
    const SBakedValueRecord* const record = value_record(container_value);
    return ((record != nullptr) && value_type_is_container(record->value_type) && (record->child_count != 0u)) ?
        CBakedValueIndex{ record->first_child_index + record->child_count - 1u } : CBakedValueIndex{};
}

CBakedValueIndex CBakedDocument::array_at(
    const CBakedValueIndex array,
    const std::uint32_t index) const noexcept
{
    const SBakedValueRecord* const record = value_record(array);
    return ((record != nullptr) && value_type_is_array(record->value_type) && (index < record->child_count)) ?
        CBakedValueIndex{ record->first_child_index + index } : CBakedValueIndex{};
}

CBakedValueIndex CBakedDocument::object_child(
    const CBakedValueIndex object,
    const CPropertyNameId name) const noexcept
{
    const SBakedValueRecord* const record = value_record(object);
    if ((record == nullptr) || (record->value_type != EBakedValueType::object) ||
        !name.is_valid() || name.is_empty() || (name.query_value() >= header()->property_name_reference_count))
    {
        return CBakedValueIndex{};
    }
    const SBakedValueRecord* const records = reinterpret_cast<const SBakedValueRecord*>(
        m_bytes + sizeof(SBakedDocumentHeader));
    for (std::uint32_t offset = 0u; offset < record->child_count; ++offset)
    {
        const std::uint32_t child_index = record->first_child_index + offset;
        if (records[child_index].property_name_index == name.query_value())
        {
            return CBakedValueIndex{ child_index };
        }
    }
    return CBakedValueIndex{};
}

CBakedValueIndex CBakedDocument::object_child(
    const CBakedValueIndex object,
    const CStringView& name) const noexcept
{
    return object_child(object, find_property_name_id(name));
}

bool CBakedDocument::boolean_value(const CBakedValueIndex value, bool& result) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    if ((record == nullptr) || (record->value_type != EBakedValueType::boolean))
    {
        return false;
    }
    result = record->payload_bits != 0u;
    return true;
}

bool CBakedDocument::signed_integer_value(const CBakedValueIndex value, std::int64_t& result) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    CIntegerMetadata metadata;
    if ((record == nullptr) || (record->value_type != EBakedValueType::integer) ||
        !decode_integer_metadata(
            record->value_flags & baked_document_format::k_integer_metadata_flags,
            metadata) ||
        (metadata.domain != EIntegerDomain::signed_value))
    {
        return false;
    }
    result = live_signed_integer_from_bits(record->payload_bits);
    return true;
}

bool CBakedDocument::unsigned_integer_value(const CBakedValueIndex value, std::uint64_t& result) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    CIntegerMetadata metadata;
    if ((record == nullptr) || (record->value_type != EBakedValueType::integer) ||
        !decode_integer_metadata(
            record->value_flags & baked_document_format::k_integer_metadata_flags,
            metadata) ||
        (metadata.domain != EIntegerDomain::unsigned_value))
    {
        return false;
    }
    result = record->payload_bits;
    return true;
}

bool CBakedDocument::integer_metadata(
    const CBakedValueIndex value,
    CIntegerMetadata& result) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return (record != nullptr) && (record->value_type == EBakedValueType::integer) &&
        decode_integer_metadata(
            record->value_flags & baked_document_format::k_integer_metadata_flags,
            result);
}

bool CBakedDocument::floating_point_value(const CBakedValueIndex value, double& result) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    if ((record == nullptr) || (record->value_type != EBakedValueType::floating_point))
    {
        return false;
    }
    result = live_floating_point_from_bits(record->payload_bits);
    return true;
}

CStringValueId CBakedDocument::string_value_id(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return ((record != nullptr) && (record->value_type == EBakedValueType::string)) ?
        CStringValueId{ static_cast<std::uint32_t>(record->payload_bits) } : CStringValueId{};
}

CStringView CBakedDocument::string_value(const CBakedValueIndex value) const noexcept
{
    return string_value(string_value_id(value));
}

bool CBakedDocument::derive_layout(
    const SBakedDocumentHeader& candidate_header,
    const std::size_t supplied_byte_count,
    SLayout& layout) noexcept
{
    layout = SLayout{};
    if ((supplied_byte_count > std::numeric_limits<std::uint32_t>::max()) ||
        (candidate_header.total_size != supplied_byte_count) ||
        (candidate_header.value_count == 0u) ||
        (candidate_header.property_name_reference_count == 0u) ||
        (candidate_header.property_name_byte_count == 0u) ||
        (candidate_header.string_value_reference_count == 0u) ||
        (candidate_header.string_value_byte_count == 0u))
    {
        return false;
    }

    std::uint64_t offset = sizeof(SBakedDocumentHeader);
    const auto place_section = [&offset](
        const std::uint32_t count,
        const std::uint32_t stride,
        std::uint32_t& section_offset) noexcept
    {
        if (offset > std::numeric_limits<std::uint32_t>::max())
        {
            return false;
        }
        section_offset = static_cast<std::uint32_t>(offset);
        offset += static_cast<std::uint64_t>(count) * stride;
        return offset <= std::numeric_limits<std::uint32_t>::max();
    };

    if (!place_section(candidate_header.value_count, sizeof(SBakedValueRecord), layout.values_offset) ||
        !place_section(
            candidate_header.property_name_reference_count,
            sizeof(SBakedStringReference),
            layout.property_name_references_offset) ||
        !place_section(
            candidate_header.string_value_reference_count,
            sizeof(SBakedStringReference),
            layout.string_value_references_offset) ||
        !place_section(candidate_header.property_name_byte_count, 1u, layout.property_name_bytes_offset) ||
        !place_section(candidate_header.string_value_byte_count, 1u, layout.string_value_bytes_offset))
    {
        return false;
    }
    return offset == candidate_header.total_size;
}

bool CBakedDocument::validate(const std::uint8_t* const bytes, const std::size_t byte_count) noexcept
{
    if ((bytes == nullptr) ||
        ((reinterpret_cast<std::uintptr_t>(bytes) & (baked_document_format::k_block_alignment - 1u)) != 0u) ||
        (byte_count < sizeof(SBakedDocumentHeader)))
    {
        return false;
    }

    const SBakedDocumentHeader* const candidate_header = reinterpret_cast<const SBakedDocumentHeader*>(bytes);
    if ((candidate_header->magic != baked_document_format::k_magic) ||
        (candidate_header->version != baked_document_format::k_version) ||
        (candidate_header->header_size != baked_document_format::k_header_size))
    {
        return false;
    }

    SLayout layout;
    if (!derive_layout(*candidate_header, byte_count, layout) ||
        ((layout.values_offset & (baked_document_format::k_block_alignment - 1u)) != 0u) ||
        !validate_string_table(
            bytes,
            layout.property_name_references_offset,
            candidate_header->property_name_reference_count,
            layout.property_name_bytes_offset,
            candidate_header->property_name_byte_count) ||
        !validate_string_table(
            bytes,
            layout.string_value_references_offset,
            candidate_header->string_value_reference_count,
            layout.string_value_bytes_offset,
            candidate_header->string_value_byte_count))
    {
        return false;
    }

    const SBakedValueRecord* const records = reinterpret_cast<const SBakedValueRecord*>(bytes + layout.values_offset);
    const std::uint32_t value_count = candidate_header->value_count;
    const std::uint32_t property_name_count = candidate_header->property_name_reference_count;
    const std::uint32_t string_value_count = candidate_header->string_value_reference_count;
    const std::size_t mark_count = std::max(property_name_count, string_value_count);
    TPodVector<std::uint32_t> marks;
    if (!marks.resize(mark_count))
    {
        return false;
    }
    for (std::size_t index = 0u; index < mark_count; ++index)
    {
        marks[index] = 0u;
    }

    for (std::uint32_t index = 0u; index < value_count; ++index)
    {
        const SBakedValueRecord& value = records[index];
        const std::uint8_t type = static_cast<std::uint8_t>(value.value_type);
        const std::uint8_t type_flags =
            value.value_flags & static_cast<std::uint8_t>(~baked_document_format::k_sibling_position_flags);
        if ((type < static_cast<std::uint8_t>(EBakedValueType::null_value)) ||
            (type > static_cast<std::uint8_t>(EBakedValueType::recovered_array)) ||
            (value.reserved_16 != 0u) ||
            (value.reserved_32 != 0u) ||
            (value.property_name_index >= property_name_count) ||
            ((index == 0u) &&
                ((value.value_type != EBakedValueType::object) ||
                    (value.parent_index != baked_document_format::k_invalid_index) ||
                    (value.property_name_index != 0u) ||
                    ((value.value_flags & baked_document_format::k_sibling_position_flags) != 0u))) ||
            ((index != 0u) && (value.parent_index >= index)))
        {
            return false;
        }
        marks[value.property_name_index] = 1u;

        const bool container = value_type_is_container(value.value_type);
        if (container)
        {
            if ((value.payload_bits != 0u) || (type_flags != 0u) ||
                ((value.child_count == 0u) &&
                    (value.first_child_index != baked_document_format::k_invalid_index)) ||
                ((value.child_count != 0u) &&
                    (value.first_child_index == baked_document_format::k_invalid_index)))
            {
                return false;
            }
        }
        else if ((value.first_child_index != baked_document_format::k_invalid_index) ||
            (value.child_count != 0u) ||
            ((value.value_type == EBakedValueType::null_value) &&
                ((value.payload_bits != 0u) || (type_flags != 0u))) ||
            ((value.value_type == EBakedValueType::boolean) &&
                ((value.payload_bits > 1u) || (type_flags != 0u))) ||
            ((value.value_type == EBakedValueType::integer) && !validate_integer(value)) ||
            ((value.value_type == EBakedValueType::floating_point) &&
                ((type_flags != 0u) ||
                    !live_floating_point_is_finite(live_floating_point_from_bits(value.payload_bits)))) ||
            ((value.value_type == EBakedValueType::string) &&
                ((type_flags != 0u) ||
                    ((value.payload_bits >> 32u) != 0u) ||
                    (static_cast<std::uint32_t>(value.payload_bits) >= string_value_count))))
        {
            return false;
        }
    }

    for (std::uint32_t index = 1u; index < property_name_count; ++index)
    {
        if (marks[index] == 0u)
        {
            return false;
        }
    }
    for (std::size_t index = 0u; index < mark_count; ++index)
    {
        marks[index] = 0u;
    }

    std::uint32_t expected_child = 1u;
    for (std::uint32_t index = 0u; index < value_count; ++index)
    {
        const SBakedValueRecord& container = records[index];
        if (!value_type_is_container(container.value_type) || (container.child_count == 0u))
        {
            continue;
        }
        const std::uint64_t range_end =
            static_cast<std::uint64_t>(container.first_child_index) + container.child_count;
        if ((container.first_child_index != expected_child) || (range_end > value_count))
        {
            return false;
        }
        const std::uint32_t object_stamp = index + 1u;
        for (std::uint32_t child_index = container.first_child_index;
            child_index < static_cast<std::uint32_t>(range_end);
            ++child_index)
        {
            const SBakedValueRecord& child = records[child_index];
            const std::uint8_t expected_position_flags =
                ((child_index == container.first_child_index) ? baked_document_format::k_first_sibling_flag : 0u) |
                (((child_index + 1u) == range_end) ? baked_document_format::k_last_sibling_flag : 0u);
            if ((child.parent_index != index) ||
                ((child.value_flags & baked_document_format::k_sibling_position_flags) != expected_position_flags) ||
                ((container.value_type == EBakedValueType::object) &&
                    ((child.property_name_index == 0u) ||
                        (marks[child.property_name_index] == object_stamp))) ||
                ((container.value_type == EBakedValueType::recovered_array) &&
                    (child.property_name_index != 0u)))
            {
                return false;
            }
            if (container.value_type == EBakedValueType::object)
            {
                marks[child.property_name_index] = object_stamp;
            }
        }
        expected_child = static_cast<std::uint32_t>(range_end);
    }
    if (expected_child != value_count)
    {
        return false;
    }

    for (std::size_t index = 0u; index < mark_count; ++index)
    {
        marks[index] = 0u;
    }
    for (std::uint32_t index = 0u; index < value_count; ++index)
    {
        const SBakedValueRecord& value = records[index];
        if (value.value_type == EBakedValueType::string)
        {
            marks[static_cast<std::uint32_t>(value.payload_bits)] = 1u;
        }
    }
    for (std::uint32_t index = 1u; index < string_value_count; ++index)
    {
        if (marks[index] == 0u)
        {
            return false;
        }
    }
    return true;
}

bool CBakedDocument::validate_string_table(
    const std::uint8_t* const bytes,
    const std::uint32_t references_offset,
    const std::uint32_t reference_count,
    const std::uint32_t bytes_offset,
    const std::uint32_t string_byte_count) noexcept
{
    const SBakedStringReference* const references =
        reinterpret_cast<const SBakedStringReference*>(bytes + references_offset);
    const std::uint8_t* const string_bytes = bytes + bytes_offset;
    if ((references[0u].offset != 0u) || (references[0u].length != 0u) ||
        (string_bytes[0u] != 0u))
    {
        return false;
    }

    std::uint64_t expected_offset = 1u;
    CStringView previous;
    for (std::uint32_t index = 1u; index < reference_count; ++index)
    {
        const SBakedStringReference& reference = references[index];
        const std::uint64_t terminator =
            static_cast<std::uint64_t>(reference.offset) + reference.length;
        if ((reference.offset != expected_offset) || (reference.length == 0u) ||
            (terminator >= string_byte_count) || (string_bytes[terminator] != 0u))
        {
            return false;
        }
        const CStringView current{ string_bytes + reference.offset, reference.length };
        std::size_t normalized_size = 0u;
        if (!utf8_string::validate_and_measure(
                current.string(),
                current.length(),
                utf8_string::ELiteralNulPolicy::reject,
                normalized_size) ||
            (normalized_size != current.length()) ||
            ((index > 1u) && (previous.relationship(current) >= 0)))
        {
            return false;
        }
        previous = current;
        expected_offset = terminator + 1u;
    }
    return expected_offset == string_byte_count;
}

bool CBakedDocument::value_type_is_array(const EBakedValueType type) noexcept
{
    return (type == EBakedValueType::array) || (type == EBakedValueType::recovered_array);
}

bool CBakedDocument::value_type_is_container(const EBakedValueType type) noexcept
{
    return value_type_is_array(type) || (type == EBakedValueType::object);
}

bool CBakedDocument::decode_integer_metadata(
    const std::uint8_t flags,
    CIntegerMetadata& metadata) noexcept
{
    if ((flags & static_cast<std::uint8_t>(~baked_document_format::k_integer_metadata_flags)) != 0u)
    {
        return false;
    }
    const CIntegerMetadata decoded{
        ((flags & 0x01u) != 0u) ? EIntegerDomain::unsigned_value : EIntegerDomain::signed_value,
        static_cast<EIntegerWidth>((flags >> 1u) & 0x03u),
        static_cast<EIntegerNotation>((flags >> 3u) & 0x03u),
        ((flags & 0x20u) != 0u) ? EIntegerPrefix::alternate : EIntegerPrefix::standard,
    };
    if (!live_integer_metadata_is_valid(decoded))
    {
        return false;
    }
    metadata = decoded;
    return true;
}

bool CBakedDocument::validate_integer(const SBakedValueRecord& value) noexcept
{
    CIntegerMetadata metadata;
    if (!decode_integer_metadata(
            value.value_flags & baked_document_format::k_integer_metadata_flags,
            metadata))
    {
        return false;
    }
    return (metadata.domain == EIntegerDomain::unsigned_value) ?
        live_integer_metadata_matches_unsigned(value.payload_bits, metadata) :
        live_integer_metadata_matches_signed(live_signed_integer_from_bits(value.payload_bits), metadata);
}

const SBakedDocumentHeader* CBakedDocument::header() const noexcept
{
    return is_ready() ? reinterpret_cast<const SBakedDocumentHeader*>(m_bytes) : nullptr;
}

const SBakedValueRecord* CBakedDocument::values(const SLayout& layout) const noexcept
{
    return reinterpret_cast<const SBakedValueRecord*>(m_bytes + layout.values_offset);
}

const SBakedValueRecord* CBakedDocument::value_record(const CBakedValueIndex value) const noexcept
{
    if (!is_ready() || !value.is_valid() || (value.query_value() >= header()->value_count))
    {
        return nullptr;
    }
    return reinterpret_cast<const SBakedValueRecord*>(m_bytes + sizeof(SBakedDocumentHeader)) +
        value.query_value();
}

CPropertyNameId CBakedDocument::find_property_name_id(const CStringView& name) const noexcept
{
    if (!is_ready() || (name.string() == nullptr) || (name.length() == 0u))
    {
        return CPropertyNameId{};
    }
    SLayout layout;
    if (!derive_layout(*header(), m_byte_count, layout))
    {
        return CPropertyNameId{};
    }
    const SBakedStringReference* const references = reinterpret_cast<const SBakedStringReference*>(
        m_bytes + layout.property_name_references_offset);
    std::uint32_t first = 1u;
    std::uint32_t end = header()->property_name_reference_count;
    while (first < end)
    {
        const std::uint32_t middle = first + ((end - first) / 2u);
        const SBakedStringReference& reference = references[middle];
        const CStringView candidate{
            m_bytes + layout.property_name_bytes_offset + reference.offset,
            reference.length };
        const std::int32_t relationship = candidate.relationship(name);
        if (relationship < 0)
        {
            first = middle + 1u;
        }
        else
        {
            end = middle;
        }
    }
    if (first >= header()->property_name_reference_count)
    {
        return CPropertyNameId{};
    }
    const SBakedStringReference& reference = references[first];
    const CStringView candidate{
        m_bytes + layout.property_name_bytes_offset + reference.offset,
        reference.length };
    return (candidate == name) ? CPropertyNameId{ first } : CPropertyNameId{};
}

CStringView CBakedDocument::string_from(
    const std::uint32_t id,
    const std::uint32_t references_offset,
    const std::uint32_t reference_count,
    const std::uint32_t bytes_offset) const noexcept
{
    if (id >= reference_count)
    {
        return CStringView{};
    }
    const SBakedStringReference* const references =
        reinterpret_cast<const SBakedStringReference*>(m_bytes + references_offset);
    const SBakedStringReference& reference = references[id];
    return CStringView{ m_bytes + bytes_offset + reference.offset, reference.length };
}

CBakedDocumentBlock::CBakedDocumentBlock(CBakedDocumentBlock&& source) noexcept
{
    replace_with(source);
}

CBakedDocumentBlock& CBakedDocumentBlock::operator=(CBakedDocumentBlock&& source) noexcept
{
    if (this != &source)
    {
        replace_with(source);
    }
    return *this;
}

bool CBakedDocumentBlock::build_from(const CLiveDocument& source) noexcept
{
    CBakedDocumentBaker baker{ source };
    if (!baker.build())
    {
        return false;
    }

    m_document = baker.document();
    m_bytes = baker.take_bytes();
    return true;
}

void CBakedDocumentBlock::deallocate() noexcept
{
    m_document.clear();
    m_bytes.deallocate();
}

bool CBakedDocumentBlock::is_ready() const noexcept
{
    return m_bytes.is_ready() && m_document.is_ready();
}

const CBakedDocument& CBakedDocumentBlock::document() const noexcept
{
    return m_document;
}

CByteConstView CBakedDocumentBlock::bytes() const noexcept
{
    return m_bytes.const_view();
}

std::uint32_t CBakedDocumentBlock::memory_token_count() const noexcept
{
    return m_bytes.memory_token_count();
}

std::uint32_t CBakedDocumentBlock::memory_allocation_count() const noexcept
{
    return m_bytes.memory_allocation_count();
}

std::uint64_t CBakedDocumentBlock::memory_allocation_size() const noexcept
{
    return m_bytes.memory_allocation_size();
}

void CBakedDocumentBlock::replace_with(CBakedDocumentBlock& source) noexcept
{
    m_bytes = std::move(source.m_bytes);
    m_document = source.m_document;
    source.m_document.clear();
}
