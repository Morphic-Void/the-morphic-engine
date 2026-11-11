
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_data_block.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    24 Oct 26
//
//  Common storage attachment and transactional aggregate construction.

#include "schema/baked_data_block.hpp"
#include "schema/data_block_construction.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_copy.hpp"
#include "containers/TPodVector.hpp"

#include <limits>
#include <utility>

namespace schema
{

static bool build_failure(SDataBlockDiagnostic& diagnostic, const EDataBlockStage stage, const EDataBlockReason reason) noexcept
{
    diagnostic = {};
    diagnostic.stage = stage;
    diagnostic.reason = reason;
    return false;
}

bool CBakedDataBlock::adopt_local(CBakedDocumentBlock&& block, SDataBlockDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (loaded_ready()) return build_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::destination_not_empty);
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
    if (loaded_ready()) return build_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::destination_not_empty);
    if (!asset.is_valid()) return build_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::invalid_input);
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

//  Admit referenced text in lexical order to avoid repeated sorted-index shifts.
//  Live IDs need not equal their ranks; the native copier maps them separately.
static bool preload_assembly_strings(const CLiveDocument& source, const SLiveDocumentStringAnalysis& strings,
    CLiveDocument& description) noexcept
{
    using document_translation::detail::CDocumentConstruction;
    for (std::uint32_t rank = 1u; rank < strings.property_name_references.size(); ++rank)
    {
        const CPropertyNameId id = source.property_name_id_at_rank(rank);
        if (!id || (id.query_value() >= strings.property_name_references.size())) return false;
        if (strings.property_name_references[id.query_value()])
        {
            const CStringView text = source.property_name(id);
            if (text.empty() || (text.length() == 0u) || !CDocumentConstruction::import_name(description, text)) return false;
        }
    }
    for (std::uint32_t rank = 1u; rank < strings.string_value_references.size(); ++rank)
    {
        const CStringValueId id = source.string_value_id_at_rank(rank);
        if (!id || (id.query_value() >= strings.string_value_references.size())) return false;
        if (strings.string_value_references[id.query_value()])
        {
            const CStringView text = source.string_value(id);
            if (text.empty() || (text.length() == 0u) || !CDocumentConstruction::import_string(description, text)) return false;
        }
    }
    return true;
}

//  Copy only the root-reachable tree. Planning iteratively preserves whole-document
//  translation's depth behaviour and reserves both value and aggregate nodes.
static bool copy_assembly_description(const CLiveDocument& source, CLiveDocument& description) noexcept
{
    struct SValue
    {
        CNodeKey source;
        CNodeKey destination;
        std::uint32_t parent_index;
    };
    SLiveDocumentAnalysis analysis;
    SLiveDocumentStringAnalysis strings;
    TPodVector<SValue> values;
    if (!source.analyse(analysis, &strings) || !values.resize(analysis.value_count)) return false;
    values[0u] = { source.root(), {}, 0u };
    std::uint32_t next_value = 1u;
    std::uint64_t node_count = analysis.value_count;
    for (std::uint32_t index = 0u; index < next_value; ++index)
    {
        const ELiveValueType type = source.value_type(values[index].source);
        if ((type == ELiveValueType::array) || (type == ELiveValueType::object)) ++node_count;
        const std::uint32_t first_child_index = next_value;
        for (CNodeKey child = source.first_child(values[index].source); child; child = source.next_sibling(child))
        {
            if (next_value >= values.size()) return false;
            values[next_value++] = { child, {}, index };
        }
        if ((next_value - first_child_index) != source.child_count(values[index].source)) return false;
    }
    if ((next_value != values.size()) || (node_count > std::numeric_limits<std::uint32_t>::max()) ||
        !description.initialise(static_cast<std::uint32_t>(node_count)) ||
        !preload_assembly_strings(source, strings, description)) return false;

    values[0u].destination = description.root();
    document_translation::detail::CNativeValueCopier<CLiveDocument> copier{ description, source };
    for (std::uint32_t index = 1u; index < values.size(); ++index)
    {
        SValue& value = values[index];
        value.destination = copier.copy(value.source, {}, false);
        if (!value.destination || !document_translation::detail::CDocumentConstruction::append(
            description, values[value.parent_index].destination, value.destination)) return false;
    }
    return true;
}

static bool prepare_assembly_description(const CLiveDocument& source, CLiveDocument& description,
    SDataBlockDiagnostic& diagnostic) noexcept
{
    return copy_assembly_description(source, description) ||
        build_failure(diagnostic, EDataBlockStage::description_preparation, EDataBlockReason::description_preparation_failed);
}

static bool prepare_assembly_description(const CBakedDocument& source, CLiveDocument& description,
    SDataBlockDiagnostic& diagnostic) noexcept
{
    return document_translation::promote(source, description) ||
        build_failure(diagnostic, EDataBlockStage::document_promotion, EDataBlockReason::document_promotion_failed);
}

template<class TDocument>
[[nodiscard]] static bool assemble_common(const TDocument& source, CBakedDataBlock& destination,
    const SDataBlockPayloads& payloads, SDataBlockDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (destination.loaded_ready()) return build_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::destination_not_empty);
    detail::SDataBlockRoot root;
    if (!detail::read_data_block_root(detail::CDocumentRead{ source }, root, diagnostic)) return false;
    const bool instances_missing = static_cast<bool>(root.instances) != static_cast<bool>(payloads.instances);
    const bool bulk_missing = static_cast<bool>(root.bulk_data) != static_cast<bool>(payloads.bulk_data);
    if (instances_missing || bulk_missing)
    {
        (void)build_failure(diagnostic, EDataBlockStage::preconditions, EDataBlockReason::missing_property);
        const auto section = instances_missing ? root.instances : root.bulk_data;
        const auto occurrence = section ? section : detail::CDocumentRead{ source }.root();
        diagnostic.location.kind = occurrence.live ? EDataBlockLocationKind::source_live_node : EDataBlockLocationKind::source_baked_value;
        diagnostic.location.live_node = occurrence.live;
        diagnostic.location.baked_value = occurrence.baked;
        return false;
    }
    CLiveDocument description;
    if (!prepare_assembly_description(source, description, diagnostic)) return false;
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

}   // namespace schema
