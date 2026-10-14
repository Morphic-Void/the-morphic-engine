
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

static bool construction_failure(SDataBlockDiagnostic& diagnostic, const EDataBlockStage stage,
    const EDataBlockReason reason) noexcept
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

bool assemble_data_block(CLiveDocument& description, const CByteConstView payload,
    const EDataBlockKind kind, CBakedDocumentBlock& output, SDataBlockDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (output.buffer_capacity() != 0u)
    {
        return construction_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::destination_not_empty);
    }
    if ((kind != EDataBlockKind::instances) && (kind != EDataBlockKind::bulk_data))
    {
        return construction_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::invalid_option);
    }

    //  This helper takes a fresh role-prepared description, not an existing file.
    const CNodeKey root = description.root();
    if (!description.is_ready() || (description.value_type(root) != ELiveValueType::object) ||
        description.object_child(root, CStringView{ "fileType" }) ||
        description.object_child(root, CStringView{ "version" }) ||
        description.object_child(root, CStringView{ "payloadSize" }))
    {
        return construction_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::invalid_input);
    }
    const CStringView identifier{ kind == EDataBlockKind::instances ? "morphic-instance-data" : "morphic-bulk-data" };
    if (!append_metadata(description, description.create_string(identifier, CStringView{ "fileType" }), diagnostic) ||
        !append_metadata(description, description.create_unsigned_integer(k_data_block_version, CStringView{ "version" }), diagnostic) ||
        !append_metadata(description, description.create_unsigned_integer(payload.size(), CStringView{ "payloadSize" }), diagnostic))
    {
        return false;
    }
    CBakedDocumentBlock prefix;
    if (!document_translation::bake(description, prefix))
    {
        return construction_failure(diagnostic, EDataBlockStage::document_bake, EDataBlockReason::document_bake_failed);
    }
    SDataBlockLayout layout;
    EDataBlockReason reason;
    if (!data_block_layout(prefix.bytes().size(), payload.size(), layout, reason))
    {
        return construction_failure(diagnostic, EDataBlockStage::assembly, reason);
    }
    CByteBuffer storage;
    if (!storage.allocate(layout.file_size, k_data_block_alignment))
    {
        return construction_failure(diagnostic, EDataBlockStage::assembly, EDataBlockReason::allocation_failed);
    }
    if (!storage.set_size(layout.file_size))
    {
        return construction_failure(diagnostic, EDataBlockStage::assembly, EDataBlockReason::assembly_failed);
    }
    const CByteConstView document_bytes = prefix.bytes();
    std::memcpy(storage.data(), document_bytes.data(), document_bytes.size());
    if (payload.size() != 0u)
    {
        std::memset(storage.data() + document_bytes.size(), 0, layout.payload_offset - document_bytes.size());
        std::memcpy(storage.data() + layout.payload_offset, payload.data(), payload.size());
    }

    //  Generic adoption validates the prefix and preserves the entire buffer.
    //  Its Boolean failure does not establish an allocation-failure diagnosis.
    if (!output.adopt_buffer(std::move(storage)))
    {
        return construction_failure(diagnostic, EDataBlockStage::assembly, EDataBlockReason::assembly_failed);
    }
    return true;
}

}   // namespace schema::detail
