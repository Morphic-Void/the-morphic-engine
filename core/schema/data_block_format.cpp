
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_format.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Bounded validation of instance and bulk data persistence envelopes.

#include "schema/data_block_format.hpp"

#include <cstring>
#include <limits>

namespace schema::detail
{

static bool text_is(const CStringView value, const char* const expected) noexcept
{
    const CStringView text{ expected };
    return (value.length() == text.length()) && (std::memcmp(value.string(), text.string(), text.length()) == 0);
}

static bool fail(SDataBlockDiagnostic& diagnostic, const EDataBlockReason reason,
    const EDataBlockStage stage = EDataBlockStage::format_validation) noexcept
{
    diagnostic = {};
    diagnostic.stage = stage;
    diagnostic.reason = reason;
    return false;
}

static bool fail_at_byte(SDataBlockDiagnostic& diagnostic, const EDataBlockReason reason, const std::uint64_t offset) noexcept
{
    (void)fail(diagnostic, reason);
    diagnostic.location.kind = EDataBlockLocationKind::file_byte;
    diagnostic.location.byte_offset = offset;
    return false;
}

static bool fail_at_value(SDataBlockDiagnostic& diagnostic, const EDataBlockReason reason, const CBakedValueIndex value) noexcept
{
    (void)fail(diagnostic, reason);
    diagnostic.location.kind = EDataBlockLocationKind::source_baked_value;
    diagnostic.location.baked_value = value;
    return false;
}

bool data_block_layout(const std::uint64_t document_size, const std::uint64_t payload_size, SDataBlockLayout& layout, EDataBlockReason& reason) noexcept
{
    layout = {};
    reason = EDataBlockReason::none;
    if (document_size < baked_document_format::k_min_document_size)
    {
        reason = EDataBlockReason::invalid_document;
        return false;
    }
    if (document_size > memory::k_byte_size_ceiling)
    {
        reason = EDataBlockReason::storage_limit;
        return false;
    }

    //  The document ceiling makes alignment addition safe in uint64_t.
    const std::uint64_t offset = (payload_size == 0u) ? document_size :
        (document_size + k_data_block_alignment - 1u) & ~std::uint64_t{ k_data_block_alignment - 1u };
    if (payload_size > (std::numeric_limits<std::uint64_t>::max() - offset))
    {
        reason = EDataBlockReason::size_overflow;
        return false;
    }
    const std::uint64_t end = offset + payload_size;
    if (end > memory::k_byte_size_ceiling)
    {
        reason = EDataBlockReason::storage_limit;
        return false;
    }
    layout.payload_offset = (payload_size == 0u) ? 0u : static_cast<std::size_t>(offset);
    layout.file_size = static_cast<std::size_t>(end);
    return true;
}

bool validate_data_block(const void* const bytes, const std::size_t size,
    const EDataBlockKind kind, SDataBlockView& output, SDataBlockDiagnostic& diagnostic) noexcept
{
    output = {};
    diagnostic = {};
    if ((kind != EDataBlockKind::instances) && (kind != EDataBlockKind::bulk_data))
    {
        return fail(diagnostic, EDataBlockReason::invalid_option, EDataBlockStage::preconditions);
    }
    if (bytes == nullptr)
    {
        return fail(diagnostic, EDataBlockReason::invalid_input, EDataBlockStage::preconditions);
    }
    if (size > memory::k_byte_size_ceiling)
    {
        return fail(diagnostic, EDataBlockReason::storage_limit);
    }
    if ((reinterpret_cast<std::uintptr_t>(bytes) & (k_data_block_alignment - 1u)) != 0u)
    {
        return fail(diagnostic, EDataBlockReason::invalid_alignment);
    }
    if (size < sizeof(SBakedDocumentHeader))
    {
        return fail(diagnostic, EDataBlockReason::invalid_document);
    }
    SBakedDocumentHeader header;
    std::memcpy(&header, bytes, sizeof(header));
    if (header.magic != baked_document_format::k_magic)
    {
        return fail_at_byte(diagnostic, EDataBlockReason::invalid_document, offsetof(SBakedDocumentHeader, magic));
    }
    if (header.version != baked_document_format::k_version)
    {
        return fail_at_byte(diagnostic, EDataBlockReason::unsupported_document_version, offsetof(SBakedDocumentHeader, version));
    }
    if (header.total_size > size)
    {
        return fail_at_byte(diagnostic, EDataBlockReason::file_length_mismatch, offsetof(SBakedDocumentHeader, total_size));
    }
    CBakedDocument document;
    if (!document.reset(bytes, header.total_size))
    {
        return fail(diagnostic, EDataBlockReason::invalid_document);
    }
    const CBakedValueIndex root = document.root();
    if (document.value_type(root) != EBakedValueType::object)
    {
        return fail_at_value(diagnostic, EDataBlockReason::invalid_property_type, root);
    }
    const char* const section_name = (kind == EDataBlockKind::instances) ? "instances" : "data";
    const char* const identifier = (kind == EDataBlockKind::instances) ? "morphic-instance-data" : "morphic-bulk-data";
    CBakedValueIndex file_type, version, payload_size, section, stripped;
    for (CBakedValueIndex child = document.first_child(root); child; child = document.next_sibling(child))
    {
        const CStringView name = document.name(child);
        if (text_is(name, "fileType")) file_type = child;
        else if (text_is(name, "version")) version = child;
        else if (text_is(name, "payloadSize")) payload_size = child;
        else if (text_is(name, section_name)) section = child;
        else if (text_is(name, "stripped")) stripped = child;
        else return fail_at_value(diagnostic, EDataBlockReason::unknown_property, child);
    }
    if (!file_type || !version || !payload_size || !section)
    {
        return fail_at_value(diagnostic, EDataBlockReason::missing_property, root);
    }
    if (document.value_type(file_type) != EBakedValueType::string)
    {
        return fail_at_value(diagnostic, EDataBlockReason::invalid_property_type, file_type);
    }
    if (!text_is(document.string_value(file_type), identifier))
    {
        return fail_at_value(diagnostic, EDataBlockReason::wrong_content_type, file_type);
    }
    std::uint64_t format_version{}, payload_count{};
    if (!document.unsigned_integer_value(version, format_version))
    {
        return fail_at_value(diagnostic, EDataBlockReason::invalid_property_type, version);
    }
    if (format_version != k_data_block_version)
    {
        return fail_at_value(diagnostic, EDataBlockReason::unsupported_format_version, version);
    }
    if (!document.unsigned_integer_value(payload_size, payload_count))
    {
        return fail_at_value(diagnostic, EDataBlockReason::invalid_property_type, payload_size);
    }
    if (document.value_type(section) != EBakedValueType::object)
    {
        return fail_at_value(diagnostic, EDataBlockReason::invalid_property_type, section);
    }
    bool values_stripped{};
    if (stripped && !document.boolean_value(stripped, values_stripped))
    {
        return fail_at_value(diagnostic, EDataBlockReason::invalid_property_type, stripped);
    }
    if ((document.child_count(section) == 0u) && (payload_count != 0u))
    {
        return fail_at_value(diagnostic, EDataBlockReason::invalid_input, payload_size);
    }
    SDataBlockLayout layout;
    EDataBlockReason layout_reason;
    if (!data_block_layout(document.byte_count(), payload_count, layout, layout_reason))
    {
        return fail_at_value(diagnostic, layout_reason, payload_size);
    }
    if (layout.file_size != size)
    {
        return fail_at_value(diagnostic, EDataBlockReason::file_length_mismatch, payload_size);
    }
    const auto* const storage = static_cast<const std::uint8_t*>(bytes);
    if (payload_count != 0u)
    {
        for (std::size_t index = document.byte_count(); index < layout.payload_offset; ++index)
        {
            if (storage[index] != 0u)
            {
                return fail_at_byte(diagnostic, EDataBlockReason::nonzero_padding, index);
            }
        }
    }
    output.document = document;
    output.section = section;
    output.stripped = values_stripped;
    if (payload_count != 0u)
    {
        output.payload = CByteConstView{ storage + layout.payload_offset, static_cast<std::size_t>(payload_count), k_data_block_alignment };
    }
    return true;
}

}   // namespace schema::detail
