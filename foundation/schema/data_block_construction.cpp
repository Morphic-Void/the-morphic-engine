
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_construction.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Metadata preparation and contiguous document-plus-payload assembly.

#include "schema/data_block_construction.hpp"
#include "schema/operation_meter.hpp"
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

bool prepare_common_data_block_storage(CLiveDocument& description, const std::size_t* const instance_size,
    const std::size_t* const bulk_size, CByteBuffer& storage, SDataBlockLayout& layout, SDataBlockDiagnostic& diagnostic,
    document_translation::CStringValueRemap* const strings) noexcept
{
    diagnostic = {};
    layout = {};
    if (storage.is_ready())
    {
        return construction_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::destination_not_empty);
    }
    SDataBlockRoot root;
    if (!read_data_block_root(CDocumentRead{ description }, root, diagnostic)) return false;
    if ((static_cast<bool>(instance_size) != static_cast<bool>(root.instances)) ||
        (static_cast<bool>(bulk_size) != static_cast<bool>(root.bulk_data)))
    {
        return construction_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::missing_property);
    }
    for (const char* const name : { "fileType", "version", "instancePayloadSize", "bulkPayloadSize", "stripped" })
    {
        const CNodeKey field = description.object_child(description.root(), CStringView{ name });
        if (field && !description.erase(field))
        {
            return construction_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
        }
    }
    if (!append_metadata(description, description.create_string(CStringView{ "morphic-schema-data" }, CStringView{ "fileType" }), diagnostic) ||
        !append_metadata(description, description.create_boolean(true, CStringView{ "stripped" }), diagnostic) ||
        !append_metadata(description, description.create_unsigned_integer(k_data_block_version, CStringView{ "version" }), diagnostic) ||
        (instance_size && !append_metadata(description, description.create_unsigned_integer(*instance_size, CStringView{ "instancePayloadSize" }), diagnostic)) ||
        (bulk_size && !append_metadata(description, description.create_unsigned_integer(*bulk_size, CStringView{ "bulkPayloadSize" }), diagnostic)))
    {
        return false;
    }
    if (!description.object_child(description.root(), CStringView{ "retainedText" }) &&
        !append_metadata(description, description.create_array(CStringView{ "retainedText" }), diagnostic))
    {
        return false;
    }
    std::uint64_t collection_id{};
    const CNodeKey data = description.object_child(description.root(), CStringView{ "data" });
    for (CNodeKey group = description.first_child(data); group; group = description.next_sibling(group))
    {
        if (description.value_type(group) != ELiveValueType::object) continue;
        for (CNodeKey entry = description.first_child(group); entry; entry = description.next_sibling(entry))
        {
            if ((description.value_type(entry) != ELiveValueType::object) ||
                !description.object_child(entry, CStringView{ "locator" }))
            {
                continue;
            }
            if (!data_work() || (++collection_id > INT32_MAX))
            {
                return construction_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::storage_limit);
            }
            if (!description.object_child(entry, CStringView{ "collectionId" }))
            {
                const CNodeKey id = description.create_unsigned_integer(collection_id, CStringView{ "collectionId" });
                if (!id || !description.append_child(entry, id).succeeded())
                {
                    return construction_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::allocation_failed);
                }
            }
        }
    }
    if (!read_data_block_root(CDocumentRead{ description }, root, diagnostic, true)) return false;
    //  The generic baker is schema-independent. Account for its bounded whole-
    //  tree planning, analysis, emission and validation passes here.
    if (!prepare_data_document_bake(description))
    {
        return construction_failure(diagnostic, EDataBlockStage::document_bake, EDataBlockReason::storage_limit);
    }
    CBakedDocumentBlock prefix;
    if (!(strings ? document_translation::bake(description, prefix, *strings) : document_translation::bake(description, prefix)))
    {
        return construction_failure(diagnostic, EDataBlockStage::document_bake, EDataBlockReason::document_bake_failed);
    }
    EDataBlockReason reason;
    if (!data_block_layout(prefix.bytes().size(), root.instance_bytes, root.bulk_bytes, layout, reason))
    {
        return construction_failure(diagnostic, EDataBlockStage::assembly, reason);
    }
    if (!data_output(layout.file_size))
    {
        return construction_failure(diagnostic, EDataBlockStage::assembly, EDataBlockReason::storage_limit);
    }
    if (!storage.allocate(layout.file_size, k_data_block_alignment) || !storage.set_size(layout.file_size))
    {
        return construction_failure(diagnostic, EDataBlockStage::assembly, EDataBlockReason::allocation_failed);
    }
    const CByteConstView document_bytes = prefix.bytes();
    if (!data_bytes(document_bytes.size())) return construction_failure(diagnostic, EDataBlockStage::assembly, EDataBlockReason::storage_limit);
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
    const EDataDomain domain, CByteBuffer& storage, std::uint8_t*& payload, SDataBlockDiagnostic& diagnostic) noexcept
{
    payload = nullptr;
    if ((domain != EDataDomain::instances) && (domain != EDataDomain::bulk_data))
    {
        return construction_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::invalid_option);
    }
    SDataBlockLayout layout;
    if (!prepare_common_data_block_storage(description, ((domain == EDataDomain::instances) ? &payload_size : nullptr),
        ((domain == EDataDomain::bulk_data) ? &payload_size : nullptr), storage, layout, diagnostic)) return false;
    if (payload_size) payload = storage.data() + ((domain == EDataDomain::instances) ? layout.instance_offset : layout.bulk_offset);
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
