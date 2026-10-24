
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_data_block.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    24 Oct 26
//
//  Common storage attachment and transactional aggregate construction.

#include "schema/baked_data_block.hpp"
#include "schema/data_block_attachment.hpp"
#include "schema/data_block_construction.hpp"
#include "schema/data_preparation.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_copy.hpp"

#include <utility>

namespace schema
{

bool CBakedDataBlock::adopt_local(CBakedDocumentBlock&& block, SDataBlockDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (loaded_ready()) return detail::data_block_precondition(diagnostic, EDataBlockReason::destination_not_empty);
    const CByteConstView bytes = static_cast<const CBakedDocumentBlock&>(block).buffer_bytes();
    detail::SDataBlockView view;
    if (!detail::validate_data_block(bytes.data(), bytes.size(), view, diagnostic)) return false;
    m_local = std::move(block);
    m_document = view.document;
    m_instances = view.instances;
    m_bulk_data = view.bulk_data;
    m_has_instances = view.has_instances;
    m_has_bulk_data = view.has_bulk_data;
    m_bytes = bytes;
    m_backing = EEntityBacking::local;
    return true;
}

bool CBakedDataBlock::attach_asset(const CAssetId asset, const CByteConstView bytes, SDataBlockDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (loaded_ready()) return detail::data_block_precondition(diagnostic, EDataBlockReason::destination_not_empty);
    if (!asset.is_valid()) return detail::data_block_precondition(diagnostic, EDataBlockReason::invalid_input);
    detail::SDataBlockView view;
    if (!detail::validate_data_block(bytes.data(), bytes.size(), view, diagnostic)) return false;
    m_document = view.document;
    m_instances = view.instances;
    m_bulk_data = view.bulk_data;
    m_has_instances = view.has_instances;
    m_has_bulk_data = view.has_bulk_data;
    m_bytes = bytes;
    m_asset = asset;
    m_backing = EEntityBacking::asset;
    return true;
}

void CBakedDataBlock::clear() noexcept
{
    m_document.clear();
    m_instances = {};
    m_bulk_data = {};
    m_has_instances = m_has_bulk_data = false;
    m_bytes = {};
    m_asset = {};
    m_backing = EEntityBacking::none;
    m_local.deallocate();
}

bool CBakedDataBlock::release_local(CBakedDocumentBlock& output) noexcept
{
    if ((m_backing != EEntityBacking::local) || (output.buffer_capacity() != 0u)) return false;
    output = std::move(m_local);
    clear();
    return true;
}

bool CBakedDataBlock::has_types() const noexcept
{
    return static_cast<bool>(m_document.object_child(m_document.root(), CStringView{ "types" }));
}

bool CBakedDataBlock::has_instances() const noexcept
{
    return static_cast<bool>(m_document.object_child(m_document.root(), CStringView{ "instances" }));
}

bool CBakedDataBlock::has_bulk_data() const noexcept
{
    return static_cast<bool>(m_document.object_child(m_document.root(), CStringView{ "data" }));
}

static bool build_failure(SDataBlockDiagnostic& diagnostic, const EDataBlockStage stage, const EDataBlockReason reason) noexcept
{
    diagnostic = {};
    diagnostic.stage = stage;
    diagnostic.reason = reason;
    return false;
}

template<class TDocument>
[[nodiscard]] static bool assemble_common(const TDocument& source, CBakedDataBlock& destination,
    const SDataBlockPayloads& payloads, SDataBlockDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (destination.loaded_ready()) return detail::data_block_precondition(diagnostic, EDataBlockReason::destination_not_empty);
    detail::SDataBlockRoot root;
    if (!detail::read_data_block_root(detail::CDocumentRead{ source }, root, diagnostic)) return false;
    CBakedDocumentBlock copied;
    if (!detail::prepare_materialisation(source, copied, diagnostic)) return false;
    CLiveDocument description;
    if (!document_translation::promote(copied.document(), description))
    {
        return build_failure(diagnostic, EDataBlockStage::document_promotion, EDataBlockReason::document_promotion_failed);
    }
    copied.deallocate();
    CBakedDocumentBlock complete;
    if (!detail::assemble_data_block(description, payloads.instances, payloads.bulk_data, complete, diagnostic) ||
        !destination.adopt_local(std::move(complete), diagnostic))
    {   //  Private staging handles must not escape.
        diagnostic.location = {};
        return false;
    }
    return true;
}

bool CBakedDataBlock::assemble(const CLiveDocument& source, const SDataBlockPayloads& payloads, SDataBlockDiagnostic& diagnostic) noexcept
{
    return assemble_common(source, *this, payloads, diagnostic);
}

bool CBakedDataBlock::assemble(const CBakedDocument& source, const SDataBlockPayloads& payloads, SDataBlockDiagnostic& diagnostic) noexcept
{
    return assemble_common(source, *this, payloads, diagnostic);
}

template<class TSource>
static bool append_section(CLiveDocument& destination, const TSource& source, const char* const name) noexcept
{
    const auto section = source.object_child(source.root(), CStringView{ name });
    if (!section) return true;
    const CNodeKey copy = document_translation::copy_subtree(destination, source, section, CStringView{ name });
    return copy && destination.append_child(destination.root(), copy).succeeded();
}

//  Reattach prepared values to the original locator-bearing description. Binary
//  bytes and offsets are preserved, including gaps and noncanonical stored bits.
static bool restore_group_values(CLiveDocument& target, const CNodeKey target_group,
    const CLiveDocument& source, const CNodeKey source_group, const bool bulk, const unsigned depth = 0u) noexcept
{
    if (depth >= document_limits::k_max_recursion_depth) return false;
    for (CNodeKey entry = source.first_child(source_group); entry; entry = source.next_sibling(entry))
    {
        const CNodeKey destination = target.object_child(target_group, source.name(entry));
        if (!destination) return false;
        const CStringView field{ bulk ? "data" : "declaration" };
        const CNodeKey value = source.object_child(entry, field);
        if (value)
        {
            const CNodeKey old = target.object_child(destination, field);
            if (old && !target.erase(old)) return false;
            const CNodeKey copied = document_translation::copy_subtree(target, source, value, field);
            if (!copied || !target.append_child(destination, copied).succeeded()) return false;
        }
        if (!bulk)
        {
            const CNodeKey children = source.object_child(entry, CStringView{ "specialisation" });
            if (children && !restore_group_values(target, target.object_child(destination, CStringView{ "specialisation" }),
                source, children, false, depth + 1u)) return false;
        }
    }
    return true;
}

static bool restore_section_values(CLiveDocument& target, const CLiveDocument& source, const bool bulk) noexcept
{
    const CStringView name{ bulk ? "data" : "instances" };
    const CNodeKey source_section = source.object_child(source.root(), name);
    const CNodeKey target_section = target.object_child(target.root(), name);
    for (CNodeKey group = source.first_child(source_section); group; group = source.next_sibling(group))
    {
        if (!restore_group_values(target, target.object_child(target_section, source.name(group)), source, group, bulk)) return false;
    }
    return true;
}

template<class TSchema>
[[nodiscard]] static bool materialise_resolved(const CBakedDocument& source, TSchema* schema,
    const SDataBlockPayloads& supplied, const EDataOutputForm form,
    CBakedDataBlock& destination, SDataBlockBuildDiagnostic& diagnostic, const bool compare_embedded) noexcept
{
    detail::SDataBlockRoot root;
    if (!detail::read_data_block_root(detail::CDocumentRead{ source }, root, diagnostic.block)) return false;
    if ((supplied.instances && !root.instances) || (supplied.bulk_data && !root.bulk_data))
    {
        return detail::data_block_precondition(diagnostic.block, EDataBlockReason::invalid_input);
    }
    if (root.stripped && ((root.instances && !supplied.instances) || (root.bulk_data && !supplied.bulk_data)))
    {
        if (root.instances && !supplied.instances)
        {
            diagnostic.instances.reason = EInstanceLoadReason::binary_required;
            diagnostic.instances.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source.object_child(source.root(), CStringView{ "stripped" }) });
        }
        else
        {
            diagnostic.bulk_data.reason = EBulkDataLoadReason::binary_required;
            diagnostic.bulk_data.occurrence = detail::SBulkDataHandleAccess::make(detail::SOccurrence{ source.object_child(source.root(), CStringView{ "stripped" }) });
        }
        return build_failure(diagnostic.block, EDataBlockStage::materialisation, EDataBlockReason::specialised_failure);
    }
    if ((schema && !schema->resolved_ready()) || (!schema &&
        (source.child_count(root.instances.baked) || source.child_count(root.bulk_data.baked))))
    {
        return build_failure(diagnostic.block, EDataBlockStage::schema_binding, EDataBlockReason::schema_unavailable);
    }
    //  A declared supplied range cannot be silently reconstructed if its bytes
    //  were omitted by the caller, including a declared zero-byte range.
    if ((root.instance_size && (!supplied.instances || (root.instance_bytes != supplied.instances->size()))) ||
        (root.bulk_size && (!supplied.bulk_data || (root.bulk_bytes != supplied.bulk_data->size()))))
    {
        return detail::data_block_precondition(diagnostic.block, EDataBlockReason::invalid_input);
    }
    CLiveDocument staging;
    if (!document_translation::promote(source, staging))
    {
        return build_failure(diagnostic.block, EDataBlockStage::document_promotion, EDataBlockReason::document_promotion_failed);
    }
    const char* const sections[]{ "instances", "data" };
    if ((!supplied.instances && !detail::prepare_data_block_locators(staging, false, diagnostic.block)) ||
        (!supplied.bulk_data && !detail::prepare_data_block_locators(staging, true, diagnostic.block))) return false;
    CBakedDocumentBlock prepared;
    if (!detail::prepare_materialisation(staging, prepared, diagnostic.block)) return false;
    CBakedInstances instances;
    CBakedBulkData bulk;
    CByteBuffer instance_owner, bulk_owner;
    if (root.instances && schema && (!instances.set_document(CMutableBakedDocument{ prepared }) ||
        (supplied.instances ? !instances.attach_schema(*schema, *supplied.instances, compare_embedded, diagnostic.instances) :
            !instances.attach_schema(*schema, instance_owner, diagnostic.instances))))
    {
        diagnostic.instances.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{
            detail::materialisation_source_value(source, prepared.document(), detail::SInstanceHandleAccess::occurrence(diagnostic.instances.occurrence).baked) });
        return build_failure(diagnostic.block, (supplied.instances ? EDataBlockStage::specialised_loading : EDataBlockStage::materialisation), EDataBlockReason::specialised_failure);
    }
    if (root.bulk_data && schema && (!bulk.set_document(CMutableBakedDocument{ prepared }) ||
        (supplied.bulk_data ?
            !bulk.attach_schema(*schema, *supplied.bulk_data, compare_embedded, diagnostic.bulk_data) : !bulk.attach_schema(*schema, bulk_owner, diagnostic.bulk_data))))
    {
        diagnostic.bulk_data.occurrence = detail::SBulkDataHandleAccess::make(detail::SOccurrence{
            detail::materialisation_source_value(source, prepared.document(), detail::SBulkDataHandleAccess::occurrence(diagnostic.bulk_data.occurrence).baked) });
        return build_failure(diagnostic.block, (supplied.bulk_data ? EDataBlockStage::specialised_loading : EDataBlockStage::materialisation), EDataBlockReason::specialised_failure);
    }
    CLiveDocument description;
    if (!description.initialise() || !description.set_root_type(ELiveValueType::object) ||
        !append_section(description, source, "types"))
    {
        return build_failure(diagnostic.block, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
    }
    for (unsigned index = 0u; index < 2u; ++index)
    {
        if (!(index ? root.bulk_data : root.instances)) continue;
        CLiveDocument domain;
        if (!detail::prepare_data_block_output(prepared.document(), (index ? detail::EDataBlockKind::bulk_data : detail::EDataBlockKind::instances),
            form, (index ? &bulk : nullptr), domain, diagnostic.block))
        {
            if (diagnostic.block.reason == EDataBlockReason::none)
            {
                (void)build_failure(diagnostic.block, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
            }
            diagnostic.block.location = {};
            return false;
        }
        if (schema && (form != EDataOutputForm::stripped) &&
            (((index == 0u) && root.stripped) || ((index == 1u) && (form == EDataOutputForm::embedded))))
        {
            CLiveDocument values;
            bool restored{};
            if (index == 0u)
            {
                SInstancePreparationDiagnostic error;
                restored = prepare_instances_document(instances.document_query(), *schema->resolved(), instances.payload_view(), false, values, error);
                if (!restored) diagnostic.instances = error.detail;
            }
            else
            {
                SBulkDataPreparationDiagnostic error;
                restored = prepare_bulk_data_document(bulk.document_query(), *schema->resolved(), bulk.payload_view(), false, values, error);
                if (!restored) diagnostic.bulk_data = error.detail;
            }
            if (!restored || !restore_section_values(domain, values, (index == 1u)))
            {
                //  Reconstruction diagnostics borrow the temporary working document.
                diagnostic.instances.occurrence = {};
                diagnostic.bulk_data.occurrence = {};
                return build_failure(diagnostic.block, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
            }
        }
        if (!append_section(description, domain, sections[index]))
        {
            return build_failure(diagnostic.block, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
        }
    }
    if (form == EDataOutputForm::stripped)
    {
        const CNodeKey marker = description.create_boolean(true, CStringView{ "stripped" });
        if (!marker || !description.append_child(description.root(), marker).succeeded())
        {
            return build_failure(diagnostic.block, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
        }
    }
    const CByteConstView instance_bytes = supplied.instances ? *supplied.instances : instance_owner.const_view();
    const CByteConstView bulk_bytes = supplied.bulk_data ? *supplied.bulk_data : bulk_owner.const_view();
    CBakedDocumentBlock complete;
    if (!detail::assemble_data_block(description, (root.instances ? &instance_bytes : nullptr), (root.bulk_data ? &bulk_bytes : nullptr),
        complete, diagnostic.block) || !destination.adopt_local(std::move(complete), diagnostic.block))
    {
        diagnostic.block.location = {};
        return false;
    }
    diagnostic = {};
    return true;
}

template<class TDocument, class TSchema>
[[nodiscard]] static bool materialise_common(const TDocument& source, TSchema* external_schema,
    CBakedDataBlock& destination, const EDataOutputForm form, SDataBlockBuildDiagnostic& diagnostic,
    const SDataBlockPayloads& payloads, const bool compare_embedded = true) noexcept
{
    diagnostic = {};
    if (destination.loaded_ready()) return detail::data_block_precondition(diagnostic.block, EDataBlockReason::destination_not_empty);
    if (!source.is_ready()) return detail::data_block_precondition(diagnostic.block, EDataBlockReason::invalid_input);
    if ((form != EDataOutputForm::embedded) && (form != EDataOutputForm::external) && (form != EDataOutputForm::stripped))
    {
        return detail::data_block_precondition(diagnostic.block, EDataBlockReason::invalid_option);
    }
    detail::SDataBlockRoot root;
    if (!detail::read_data_block_root(detail::CDocumentRead{ source }, root, diagnostic.block)) return false;
    CBakedDocumentBlock copied;
    if (!detail::prepare_materialisation(source, copied, diagnostic.block)) return false;
    CBakedSchema embedded;
    bool success{};
    if (root.types)
    {
        if (!embedded.set_document(copied.document()) || !embedded.resolve(diagnostic.schema))
        {
            (void)build_failure(diagnostic.block, EDataBlockStage::schema_binding, EDataBlockReason::specialised_failure);
        }
        else success = materialise_resolved(copied.document(), &embedded, payloads, form, destination, diagnostic, compare_embedded);
    }
    else success = materialise_resolved(copied.document(), external_schema, payloads, form, destination, diagnostic, compare_embedded);
    if (!success)
    {
        diagnostic.schema.occurrence = detail::SSchemaHandleAccess::make(detail::SOccurrence{
            detail::materialisation_source_value(source, copied.document(), detail::SSchemaHandleAccess::occurrence(diagnostic.schema.occurrence).baked) });
        diagnostic.schema.related = {};
        diagnostic.schema.enclosing_type = {};
        diagnostic.schema.enclosing_member = {};
        diagnostic.instances.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{
            detail::materialisation_source_value(source, copied.document(), detail::SInstanceHandleAccess::occurrence(diagnostic.instances.occurrence).baked) });
        diagnostic.bulk_data.occurrence = detail::SBulkDataHandleAccess::make(detail::SOccurrence{
            detail::materialisation_source_value(source, copied.document(), detail::SBulkDataHandleAccess::occurrence(diagnostic.bulk_data.occurrence).baked) });
        diagnostic.block.location = {};
        const detail::SOccurrence where = diagnostic.instances.occurrence ? detail::SInstanceHandleAccess::occurrence(diagnostic.instances.occurrence) :
            detail::SBulkDataHandleAccess::occurrence(diagnostic.bulk_data.occurrence);
        if (where.live)
        {
            diagnostic.block.location.kind = EDataBlockLocationKind::source_live_node;
            diagnostic.block.location.live_node = where.live;
        }
        else if (where.baked)
        {
            diagnostic.block.location.kind = EDataBlockLocationKind::source_baked_value;
            diagnostic.block.location.baked_value = where.baked;
        }
    }
    return success;
}

bool CBakedDataBlock::materialise(const CLiveDocument& source, const EDataOutputForm form,
    SDataBlockBuildDiagnostic& diagnostic, const SDataBlockPayloads& payloads) noexcept
{
    return materialise_common(source, static_cast<CBakedSchema*>(nullptr), *this, form, diagnostic, payloads);
}

bool CBakedDataBlock::materialise(const CLiveDocument& source, CBakedSchema& schema, const EDataOutputForm form,
    SDataBlockBuildDiagnostic& diagnostic, const SDataBlockPayloads& payloads) noexcept
{
    return materialise_common(source, &schema, *this, form, diagnostic, payloads);
}

bool CBakedDataBlock::materialise(const CLiveDocument& source, CLiveSchema& schema, const EDataOutputForm form,
    SDataBlockBuildDiagnostic& diagnostic, const SDataBlockPayloads& payloads) noexcept
{
    return materialise_common(source, &schema, *this, form, diagnostic, payloads);
}

bool CBakedDataBlock::materialise(const CBakedDocument& source, const EDataOutputForm form,
    SDataBlockBuildDiagnostic& diagnostic, const SDataBlockPayloads& payloads) noexcept
{
    return materialise_common(source, static_cast<CBakedSchema*>(nullptr), *this, form, diagnostic, payloads);
}

bool CBakedDataBlock::materialise(const CBakedDocument& source, CBakedSchema& schema, const EDataOutputForm form,
    SDataBlockBuildDiagnostic& diagnostic, const SDataBlockPayloads& payloads) noexcept
{
    return materialise_common(source, &schema, *this, form, diagnostic, payloads);
}

bool CBakedDataBlock::materialise(const CBakedDocument& source, CLiveSchema& schema, const EDataOutputForm form,
    SDataBlockBuildDiagnostic& diagnostic, const SDataBlockPayloads& payloads) noexcept
{
    return materialise_common(source, &schema, *this, form, diagnostic, payloads);
}

bool CBakedDataBlock::strip_to(CBakedDataBlock& destination, SDataBlockBuildDiagnostic& diagnostic) const noexcept
{
    const SDataBlockPayloads payloads{ m_has_instances ? &m_instances : nullptr, m_has_bulk_data ? &m_bulk_data : nullptr };
    return materialise_common(m_document, static_cast<CBakedSchema*>(nullptr), destination, EDataOutputForm::stripped, diagnostic, payloads, false);
}

bool CBakedDataBlock::strip_to(CBakedDataBlock& destination, CBakedSchema& schema, SDataBlockBuildDiagnostic& diagnostic) const noexcept
{
    const SDataBlockPayloads payloads{ m_has_instances ? &m_instances : nullptr, m_has_bulk_data ? &m_bulk_data : nullptr };
    return materialise_common(m_document, &schema, destination, EDataOutputForm::stripped, diagnostic, payloads, false);
}

bool CBakedDataBlock::strip_to(CBakedDataBlock& destination, CLiveSchema& schema, SDataBlockBuildDiagnostic& diagnostic) const noexcept
{
    const SDataBlockPayloads payloads{ m_has_instances ? &m_instances : nullptr, m_has_bulk_data ? &m_bulk_data : nullptr };
    return materialise_common(m_document, &schema, destination, EDataOutputForm::stripped, diagnostic, payloads, false);
}


}   // namespace schema

