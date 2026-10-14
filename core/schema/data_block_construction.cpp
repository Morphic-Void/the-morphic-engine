
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
    if (depth >= 256u) return false;
    for (CNodeKey entry = document.first_child(group); entry; entry = document.next_sibling(entry))
    {
        const CNodeKey declaration = document.object_child(entry, CStringView{ "declaration" });
        if (declaration && !document.erase(declaration)) return false;
        const CNodeKey children = document.object_child(entry, CStringView{ "specialisation" });
        if (children && !strip_instance_declarations(document, children, depth + 1u)) return false;
    }
    return true;
}

bool prepare_materialised_output(const CBakedDocument& source, const EDataBlockKind kind,
    const EDataOutputForm form, CLiveDocument& output, SDataBlockDiagnostic& diagnostic) noexcept
{
    //  The source has passed role materialisation. Copy only its selected section;
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
                const CNodeKey count = output.create_unsigned_integer(output.child_count(values), CStringView{ "count" });
                if (!count || !output.append_child(locator, count).succeeded())
                {
                    return construction_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
                }
            }
            if ((form != EDataOutputForm::embedded) && !output.erase(values))
            {
                return construction_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
            }
        }
    }
    if ((form == EDataOutputForm::stripped) &&
        !append_metadata(output, output.create_boolean(true, CStringView{ "stripped" }), diagnostic)) return false;
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
    const CStringView identifier{ (kind == EDataBlockKind::instances) ? "morphic-instance-data" : "morphic-bulk-data" };
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
        std::memset((storage.data() + document_bytes.size()), 0, (layout.payload_offset - document_bytes.size()));
        std::memcpy((storage.data() + layout.payload_offset), payload.data(), payload.size());
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
