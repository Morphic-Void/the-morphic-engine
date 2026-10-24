
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_construction.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Metadata preparation and contiguous document-plus-payload assembly.

#include "schema/data_block_construction.hpp"
#include "data_model/document_translation.hpp"

#include <cstring>

namespace schema::detail
{

static bool construction_failure(SDataBlockDiagnostic& diagnostic, const EDataBlockStage stage, const EDataBlockReason reason) noexcept
{
    diagnostic = {};
    diagnostic.stage = stage;
    diagnostic.reason = reason;
    return false;
}

static bool append_metadata(CLiveDocument& document, const CNodeKey value, SDataBlockDiagnostic& diagnostic) noexcept
{   //  Node creation/attachment does not expose a precise resource diagnosis.
    if (!value.is_valid() || !document.append_child(document.root(), value).succeeded())
    {
        return construction_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
    }
    return true;
}

bool prepare_materialisation(const CLiveDocument& source, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept
{
    if (!document_translation::bake(source, output))
    {
        return construction_failure(diagnostic, EDataBlockStage::document_bake, EDataBlockReason::document_bake_failed);
    }
    return true;
}

bool prepare_materialisation(const CBakedDocument& source, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept
{
    CLiveDocument description;
    if (!document_translation::promote(source, description))
    {
        return construction_failure(diagnostic, EDataBlockStage::document_promotion, EDataBlockReason::document_promotion_failed);
    }
    return prepare_materialisation(description, output, diagnostic);
}

static bool strip_instance_declarations(CLiveDocument& document, const CNodeKey group, const unsigned depth = 0u) noexcept
{
    if (depth >= document_limits::k_max_recursion_depth) return false;
    for (CNodeKey entry = document.first_child(group); entry; entry = document.next_sibling(entry))
    {
        const CNodeKey declaration = document.object_child(entry, CStringView{ "declaration" });
        if (declaration && !document.erase(declaration)) return false;
        const CNodeKey children = document.object_child(entry, CStringView{ "specialisation" });
        if (children && !strip_instance_declarations(document, children, depth + 1u)) return false;
    }
    return true;
}

//  Missing locators are appended only in private staging; original child ordinals
//  remain valid for mapping diagnostics back to the caller's document.
static bool prepare_group_locators(CLiveDocument& document, const CNodeKey group, const bool bulk, const unsigned depth = 0u) noexcept
{
    if (depth >= document_limits::k_max_recursion_depth) return false;
    if (document.value_type(group) != ELiveValueType::object) return true;
    for (CNodeKey entry = document.first_child(group); entry; entry = document.next_sibling(entry))
    {
        if (document.value_type(entry) != ELiveValueType::object) continue;
        if (!document.object_child(entry, CStringView{ "locator" }))
        {
            const CNodeKey locator = document.create_object(CStringView{ "locator" });
            if (!locator ||
                !document.append_child(locator, document.create_unsigned_integer(0u, CStringView{ "offset" })).succeeded() ||
                !document.append_child(locator, document.create_boolean(false, CStringView{ "valid" })).succeeded() ||
                !document.append_child(entry, locator).succeeded()) return false;
        }
        const CNodeKey children = bulk ? CNodeKey{} : document.object_child(entry, CStringView{ "specialisation" });
        if (children && !prepare_group_locators(document, children, false, depth + 1u)) return false;
    }
    return true;
}

bool prepare_data_block_locators(CLiveDocument& document, const bool bulk, SDataBlockDiagnostic& diagnostic) noexcept
{
    const CNodeKey section = document.object_child(document.root(), CStringView{ bulk ? "data" : "instances" });
    for (CNodeKey group = document.first_child(section); group; group = document.next_sibling(group))
    {
        if (!prepare_group_locators(document, group, bulk))
        {
            return construction_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
        }
    }
    return true;
}

bool prepare_missing_data_document(const CBakedDocument& source, const bool bulk,
    CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept
{
    CLiveDocument staging;
    if (!document_translation::promote(source, staging))
    {
        return construction_failure(diagnostic, EDataBlockStage::document_promotion, EDataBlockReason::document_promotion_failed);
    }
    return prepare_data_block_locators(staging, bulk, diagnostic) && prepare_materialisation(staging, output, diagnostic);
}

bool prepare_data_block_output(const CBakedDocument& source, const EDataBlockKind kind,
    const EDataOutputForm form, const CBakedBulkData* const bulk_data, CLiveDocument& output, SDataBlockDiagnostic& diagnostic) noexcept
{
    //  The source has passed role loading. Copy only its selected section;
    //  persistence metadata belongs to the newly assembled file.
    const CStringView section_name{ (kind == EDataBlockKind::instances) ? "instances" : "data" };
    if (!document_translation::promote_root_member(source, section_name, output))
    {
        return construction_failure(diagnostic, EDataBlockStage::document_promotion, EDataBlockReason::document_promotion_failed);
    }
    CNodeKey section = output.object_child(output.root(), section_name);
    if (!section)
    {
        section = output.create_object(section_name);
        if (!append_metadata(output, section, diagnostic)) return false;
    }
    for (CNodeKey group = output.first_child(section); group; group = output.next_sibling(group))
    {
        if (kind == EDataBlockKind::instances)
        {
            if ((form == EDataOutputForm::stripped) && !strip_instance_declarations(output, group))
            {
                return construction_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
            }
            continue;
        }
        for (CNodeKey entry = output.first_child(group); entry; entry = output.next_sibling(entry))
        {
            const CNodeKey values = output.object_child(entry, CStringView{ "data" });
            const CNodeKey locator = output.object_child(entry, CStringView{ "locator" });

            //  An inferred count must survive removal of the values, including
            //  for zero-byte records whose count cannot be derived from size.
            if (!output.object_child(locator, CStringView{ "count" }))
            {
                std::uint32_t record_count = output.child_count(values);
                if (bulk_data)
                {
                    SBulkDataEntryView record;
                    if (!bulk_data->entry(bulk_data->find_entry(output.name(group), output.name(entry)), record))
                    {
                        return construction_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
                    }
                    record_count = record.count;
                }
                const CNodeKey count = output.create_unsigned_integer(record_count, CStringView{ "count" });
                if (!count || !output.append_child(locator, count).succeeded())
                {
                    return construction_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
                }
            }
            if (values && (form != EDataOutputForm::embedded) && !output.erase(values))
            {
                return construction_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
            }
        }
    }
    if ((form == EDataOutputForm::stripped) && !append_metadata(output, output.create_boolean(true, CStringView{ "stripped" }), diagnostic))
    {
        return false;
    }
    return true;
}

bool prepare_common_data_block_storage(CLiveDocument& description, const std::size_t* instance_size,
    const std::size_t* bulk_size, CByteBuffer& storage, SDataBlockLayout& layout, SDataBlockDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    layout = {};
    if (storage.is_ready())
    {
        return construction_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::destination_not_empty);
    }
    SDataBlockRoot root;
    if (!read_data_block_root(CDocumentRead{ description }, root, diagnostic)) return false;
    if ((instance_size && !root.instances) || (bulk_size && !root.bulk_data))
    {
        return construction_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::missing_property);
    }
    for (const char* const name : { "fileType", "version", "instancePayloadSize", "bulkPayloadSize" })
    {
        const CNodeKey field = description.object_child(description.root(), CStringView{ name });
        if (field && !description.erase(field))
        {
            return construction_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
        }
    }
    if (!append_metadata(description, description.create_string(CStringView{ "morphic-schema-data" }, CStringView{ "fileType" }), diagnostic) ||
        !append_metadata(description, description.create_unsigned_integer(k_data_block_version, CStringView{ "version" }), diagnostic) ||
        (instance_size && !append_metadata(description, description.create_unsigned_integer(*instance_size, CStringView{ "instancePayloadSize" }), diagnostic)) ||
        (bulk_size && !append_metadata(description, description.create_unsigned_integer(*bulk_size, CStringView{ "bulkPayloadSize" }), diagnostic)))
    {
        return false;
    }
    if (!read_data_block_root(CDocumentRead{ description }, root, diagnostic, true)) return false;
    CBakedDocumentBlock prefix;
    if (!document_translation::bake(description, prefix))
    {
        return construction_failure(diagnostic, EDataBlockStage::document_bake, EDataBlockReason::document_bake_failed);
    }
    EDataBlockReason reason;
    if (!data_block_layout(prefix.bytes().size(), root.instance_bytes, root.bulk_bytes, layout, reason))
    {
        return construction_failure(diagnostic, EDataBlockStage::assembly, reason);
    }
    if (!storage.allocate(layout.file_size, k_data_block_alignment) || !storage.set_size(layout.file_size))
    {
        return construction_failure(diagnostic, EDataBlockStage::assembly, EDataBlockReason::allocation_failed);
    }
    const CByteConstView document_bytes = prefix.bytes();
    std::memcpy(storage.data(), document_bytes.data(), document_bytes.size());
    std::size_t cursor = document_bytes.size();
    if (root.instance_bytes)
    {
        std::memset((storage.data() + cursor), 0, (layout.instance_offset - cursor));
        cursor = layout.instance_offset + static_cast<std::size_t>(root.instance_bytes);
    }
    if (root.bulk_bytes)
    {
        std::memset((storage.data() + cursor), 0, (layout.bulk_offset - cursor));
    }
    return true;
}

bool prepare_data_block_storage(CLiveDocument& description, const std::size_t payload_size,
    const EDataBlockKind kind, CByteBuffer& storage, std::uint8_t*& payload, SDataBlockDiagnostic& diagnostic) noexcept
{
    payload = nullptr;
    if ((kind != EDataBlockKind::instances) && (kind != EDataBlockKind::bulk_data))
    {
        return construction_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::invalid_option);
    }
    SDataBlockLayout layout;
    if (!prepare_common_data_block_storage(description, ((kind == EDataBlockKind::instances) ? &payload_size : nullptr),
        ((kind == EDataBlockKind::bulk_data) ? &payload_size : nullptr), storage, layout, diagnostic)) return false;
    if (payload_size) payload = storage.data() + ((kind == EDataBlockKind::instances) ? layout.instance_offset : layout.bulk_offset);
    return true;
}

bool assemble_data_block(CLiveDocument& description, const CByteConstView* instances,
    const CByteConstView* bulk_data, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept
{
    if (output.buffer_capacity() != 0u)
    {
        return construction_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::destination_not_empty);
    }
    for (const auto* const payload : { instances, bulk_data })
    {
        if (payload && payload->size() && !payload->is_ready())
        {
            return construction_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::invalid_input);
        }
    }
    const std::size_t instance_size = instances ? instances->size() : 0u;
    const std::size_t bulk_size = bulk_data ? bulk_data->size() : 0u;
    CByteBuffer storage;
    SDataBlockLayout layout;
    if (!prepare_common_data_block_storage(description, (instances ? &instance_size : nullptr),
        (bulk_data ? &bulk_size : nullptr), storage, layout, diagnostic)) return false;
    if (instance_size) std::memcpy((storage.data() + layout.instance_offset), instances->data(), instance_size);
    if (bulk_size) std::memcpy((storage.data() + layout.bulk_offset), bulk_data->data(), bulk_size);
    if (!output.adopt_buffer(std::move(storage)))
    {
        return construction_failure(diagnostic, EDataBlockStage::assembly, EDataBlockReason::assembly_failed);
    }
    return true;
}

}   // namespace schema::detail
