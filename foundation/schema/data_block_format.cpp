
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


static bool root_failure(SDataBlockDiagnostic& diagnostic, const EDataBlockReason reason, const SOccurrence value) noexcept
{
    (void)fail(diagnostic, reason);
    if (value.live)
    {
        diagnostic.location.kind = EDataBlockLocationKind::source_live_node;
        diagnostic.location.live_node = value.live;
    }
    else if (value.baked)
    {
        diagnostic.location.kind = EDataBlockLocationKind::source_baked_value;
        diagnostic.location.baked_value = value.baked;
    }
    return false;
}

bool read_data_block_root(const CDocumentRead& document, SDataBlockRoot& output,
    SDataBlockDiagnostic& diagnostic, const bool require_envelope) noexcept
{
    output = {};
    diagnostic = {};
    SDataBlockRoot staged;
    const SOccurrence root = document.root();
    if (!document.is_ready() || (document.value_kind(root) != EDocumentValueKind::object))
    {
        return root_failure(diagnostic, EDataBlockReason::invalid_input, root);
    }
    SOccurrence file_type, version, stripped;
    for (SOccurrence child = document.first_child(root); child; child = document.next_sibling(child))
    {
        const CStringView name = document.name(child);
        if (text_is(name, "fileType")) file_type = child;
        else if (text_is(name, "version")) version = child;
        else if (text_is(name, "instancePayloadSize")) staged.instance_size = child;
        else if (text_is(name, "bulkPayloadSize")) staged.bulk_size = child;
        else if (text_is(name, "types")) staged.types = child;
        else if (text_is(name, "instances")) staged.instances = child;
        else if (text_is(name, "data")) staged.bulk_data = child;
        else if (text_is(name, "stripped")) stripped = child;
        else return root_failure(diagnostic, EDataBlockReason::unknown_property, child);
    }
    const bool envelope = file_type || version || staged.instance_size || staged.bulk_size;
    if (envelope || require_envelope)
    {
        if (!file_type || !version) return root_failure(diagnostic, EDataBlockReason::missing_property, root);
        if (document.value_kind(file_type) != EDocumentValueKind::string)
        {
            return root_failure(diagnostic, EDataBlockReason::invalid_property_type, file_type);
        }
        if (!text_is(document.string_value(file_type), "morphic-schema-data"))
        {
            return root_failure(diagnostic, EDataBlockReason::wrong_content_type, file_type);
        }
        std::uint64_t number{};
        if (!document.unsigned_integer_value(version, number))
        {
            return root_failure(diagnostic, EDataBlockReason::invalid_property_type, version);
        }
        if (number != k_data_block_version)
        {
            return root_failure(diagnostic, EDataBlockReason::unsupported_format_version, version);
        }
    }
    for (const SOccurrence section : { staged.types, staged.instances, staged.bulk_data })
    {
        if (section && (document.value_kind(section) != EDocumentValueKind::object))
        {
            return root_failure(diagnostic, EDataBlockReason::invalid_property_type, section);
        }
    }
    if (stripped && !document.boolean_value(stripped, staged.stripped))
    {
        return root_failure(diagnostic, EDataBlockReason::invalid_property_type, stripped);
    }
    if (require_envelope && (!stripped || !staged.stripped))
    {
        return root_failure(diagnostic, stripped ? EDataBlockReason::invalid_input : EDataBlockReason::missing_property,
            stripped ? stripped : root);
    }
    const SOccurrence sizes[]{ staged.instance_size, staged.bulk_size };
    const SOccurrence sections[]{ staged.instances, staged.bulk_data };
    std::uint64_t* const counts[]{ &staged.instance_bytes, &staged.bulk_bytes };
    for (unsigned index = 0u; index < 2u; ++index)
    {
        if (require_envelope && sections[index] && !sizes[index])
        {
            return root_failure(diagnostic, EDataBlockReason::missing_property, sections[index]);
        }
        if (!sizes[index]) continue;
        if (!sections[index]) return root_failure(diagnostic, EDataBlockReason::missing_property, sizes[index]);
        if (!document.unsigned_integer_value(sizes[index], *counts[index]))
        {
            return root_failure(diagnostic, EDataBlockReason::invalid_property_type, sizes[index]);
        }
        if (*counts[index] > memory::k_byte_size_ceiling)
        {
            return root_failure(diagnostic, EDataBlockReason::storage_limit, sizes[index]);
        }
        if ((document.child_count(sections[index]) == 0u) && (*counts[index] != 0u))
        {
            return root_failure(diagnostic, EDataBlockReason::invalid_input, sizes[index]);
        }
    }
    output = staged;
    return true;
}

bool data_block_layout(const std::uint64_t document_size, const std::uint64_t instance_size,
    const std::uint64_t bulk_size, SDataBlockLayout& layout, EDataBlockReason& reason) noexcept
{
    layout = {};
    reason = EDataBlockReason::none;
    if (document_size < baked_document_format::k_min_document_size)
    {
        reason = EDataBlockReason::invalid_document;
        return false;
    }
    std::uint64_t cursor = document_size;
    SDataBlockLayout staged;
    const std::uint64_t sizes[]{ instance_size, bulk_size };
    std::size_t* const offsets[]{ &staged.instance_offset, &staged.bulk_offset };
    for (unsigned index = 0u; index < 2u; ++index)
    {
        if (cursor > memory::k_byte_size_ceiling)
        {
            reason = EDataBlockReason::storage_limit;
            return false;
        }
        if (sizes[index] == 0u) continue;
        cursor = (cursor + k_data_block_alignment - 1u) & ~std::uint64_t{ k_data_block_alignment - 1u };
        if (sizes[index] > (std::numeric_limits<std::uint64_t>::max() - cursor))
        {
            reason = EDataBlockReason::size_overflow;
            return false;
        }
        *offsets[index] = static_cast<std::size_t>(cursor);
        cursor += sizes[index];
    }
    if (cursor > memory::k_byte_size_ceiling)
    {
        reason = EDataBlockReason::storage_limit;
        return false;
    }
    staged.file_size = static_cast<std::size_t>(cursor);
    layout = staged;
    return true;
}

bool validate_data_block(const void* const bytes, const std::size_t size,
    SDataBlockView& output, SDataBlockDiagnostic& diagnostic) noexcept
{
    output = {};
    diagnostic = {};
    if (bytes == nullptr) return fail(diagnostic, EDataBlockReason::invalid_input, EDataBlockStage::preconditions);
    if (size > memory::k_byte_size_ceiling) return fail(diagnostic, EDataBlockReason::storage_limit);
    if ((reinterpret_cast<std::uintptr_t>(bytes) & (k_data_block_alignment - 1u)) != 0u)
    {
        return fail(diagnostic, EDataBlockReason::invalid_alignment);
    }
    if (size < sizeof(SBakedDocumentHeader)) return fail(diagnostic, EDataBlockReason::invalid_document);
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
    if (!document.reset(bytes, header.total_size)) return fail(diagnostic, EDataBlockReason::invalid_document);
    SDataBlockRoot root;
    if (!read_data_block_root(CDocumentRead{ document }, root, diagnostic, true)) return false;
    SDataBlockLayout layout;
    EDataBlockReason reason;
    if (!data_block_layout(document.byte_count(), root.instance_bytes, root.bulk_bytes, layout, reason))
    {
        return fail_at_value(diagnostic, reason, document.root());
    }
    if (layout.file_size != size) return fail_at_value(diagnostic, EDataBlockReason::file_length_mismatch, document.root());
    const auto* const storage = static_cast<const std::uint8_t*>(bytes);
    const std::size_t offsets[]{ layout.instance_offset, layout.bulk_offset };
    const std::uint64_t sizes[]{ root.instance_bytes, root.bulk_bytes };
    std::size_t cursor = document.byte_count();
    SDataBlockView staged;
    CByteConstView* const views[]{ &staged.instances, &staged.bulk_data };
    for (unsigned index = 0u; index < 2u; ++index)
    {
        if (sizes[index] == 0u) continue;
        while (cursor < offsets[index])
        {
            if (storage[cursor] != 0u)
            {
                return fail_at_byte(diagnostic, EDataBlockReason::nonzero_padding, cursor);
            }
            ++cursor;
        }
        *views[index] = CByteConstView{ (storage + cursor), static_cast<std::size_t>(sizes[index]), k_data_block_alignment };
        cursor += static_cast<std::size_t>(sizes[index]);
    }
    staged.document = document;
    staged.has_instances = static_cast<bool>(root.instance_size);
    staged.has_bulk_data = static_cast<bool>(root.bulk_size);
    staged.stripped = root.stripped;
    output = staged;
    return true;
}

}   // namespace schema::detail
