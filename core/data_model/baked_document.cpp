
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_document.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Sep 26
//
//  Validation, observation and ownership of immutable baked documents.

#include "data_model/baked_document.hpp"
#include "data_model/baked_document_format.hpp"

#include <algorithm>

#include "text/utf8_string.hpp"

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

CBakedValueIndex CBakedDocument::object_child(const CBakedValueIndex object, const CPropertyNameId name) const noexcept
{
    const SBakedValueRecord* const record = value_record(object);
    if ((record == nullptr) || (record->value_type != EBakedValueType::object) ||
        !name.is_valid() || name.is_empty() || (name.query_value() >= header()->property_name_reference_count))
    {
        return CBakedValueIndex{};
    }
    const SBakedValueRecord* const records = reinterpret_cast<const SBakedValueRecord*>(m_bytes + sizeof(SBakedDocumentHeader));
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

CBakedValueIndex CBakedDocument::object_child(const CBakedValueIndex object, const CStringView& name) const noexcept
{
    return object_child(object, find_property_name_id(name));
}

bool CBakedDocument::derive_layout(const SBakedDocumentHeader& candidate_header, const std::size_t supplied_byte_count, SLayout& layout) noexcept
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
    const auto place_section = [&offset](const std::uint32_t count, const std::uint32_t stride, std::uint32_t& section_offset) noexcept
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
        !place_section(candidate_header.property_name_reference_count, sizeof(SBakedStringReference), layout.property_name_references_offset) ||
        !place_section(candidate_header.string_value_reference_count,  sizeof(SBakedStringReference), layout.string_value_references_offset) ||
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
        const std::uint8_t type_flags = value.value_flags & static_cast<std::uint8_t>(~baked_document_format::k_sibling_position_flags);
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
                ((value.child_count == 0u) && (value.first_child_index != baked_document_format::k_invalid_index)) ||
                ((value.child_count != 0u) && (value.first_child_index == baked_document_format::k_invalid_index)))
            {
                return false;
            }
        }
        else if ((value.first_child_index != baked_document_format::k_invalid_index) ||
            (value.child_count != 0u) ||
            ((value.value_type == EBakedValueType::null_value) && ((value.payload_bits != 0u) || (type_flags != 0u))) ||
            ((value.value_type == EBakedValueType::boolean) && ((value.payload_bits > 1u) || (type_flags != 0u))) ||
            ((value.value_type == EBakedValueType::integer) && !validate_integer(value)) ||
            ((value.value_type == EBakedValueType::floating_point) &&
                ((type_flags != 0u) || !live_floating_point_is_finite(live_floating_point_from_bits(value.payload_bits)))) ||
            ((value.value_type == EBakedValueType::string) &&
                ((type_flags != 0u) || ((value.payload_bits >> 32u) != 0u) || (static_cast<std::uint32_t>(value.payload_bits) >= string_value_count))))
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
                ((container.value_type == EBakedValueType::object) && ((child.property_name_index == 0u) || (marks[child.property_name_index] == object_stamp))) ||
                ((container.value_type == EBakedValueType::recovered_array) && (child.property_name_index != 0u)))
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
    const SBakedStringReference* const references = reinterpret_cast<const SBakedStringReference*>(bytes + references_offset);
    const std::uint8_t* const string_bytes = bytes + bytes_offset;
    if ((references[0u].offset != 0u) || (references[0u].length != 0u) || (string_bytes[0u] != 0u))
    {
        return false;
    }

    std::uint64_t expected_offset = 1u;
    CStringView previous;
    for (std::uint32_t index = 1u; index < reference_count; ++index)
    {
        const SBakedStringReference& reference = references[index];
        const std::uint64_t terminator = static_cast<std::uint64_t>(reference.offset) + reference.length;
        if ((reference.offset != expected_offset) || (reference.length == 0u) ||
            (terminator >= string_byte_count) || (string_bytes[terminator] != 0u))
        {
            return false;
        }
        const CStringView current{ string_bytes + reference.offset, reference.length };
        std::size_t normalized_size = 0u;
        if (!utf8_string::validate_and_measure(current.string(), current.length(), utf8_string::ELiteralNulPolicy::reject, normalized_size) ||
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

bool CBakedDocument::validate_integer(const SBakedValueRecord& value) noexcept
{
    CIntegerMetadata metadata;
    if (!decode_integer_metadata(value.value_flags & baked_document_format::k_integer_metadata_flags, metadata))
    {
        return false;
    }
    return (metadata.domain == EIntegerDomain::unsigned_value) ?
        live_integer_metadata_matches_unsigned(value.payload_bits, metadata) :
        live_integer_metadata_matches_signed(live_signed_integer_from_bits(value.payload_bits), metadata);
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
    const SBakedStringReference* const references = reinterpret_cast<const SBakedStringReference*>(m_bytes + layout.property_name_references_offset);
    std::uint32_t first = 1u;
    std::uint32_t end = header()->property_name_reference_count;
    while (first < end)
    {
        const std::uint32_t middle = first + ((end - first) / 2u);
        const SBakedStringReference& reference = references[middle];
        const CStringView candidate{ (m_bytes + layout.property_name_bytes_offset + reference.offset), reference.length };
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
    const CStringView candidate{ (m_bytes + layout.property_name_bytes_offset + reference.offset), reference.length };
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
    const SBakedStringReference* const references = reinterpret_cast<const SBakedStringReference*>(m_bytes + references_offset);
    const SBakedStringReference& reference = references[id];
    return CStringView{ (m_bytes + bytes_offset + reference.offset), reference.length };
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

void CBakedDocumentBlock::deallocate() noexcept
{
    m_document.clear();
    m_bytes.deallocate();
}

void CBakedDocumentBlock::replace_with(CBakedDocumentBlock& source) noexcept
{
    m_bytes = std::move(source.m_bytes);
    m_document = source.m_document;
    source.m_document.clear();
}
