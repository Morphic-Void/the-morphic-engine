
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_copy.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Copy complete target dependencies once per source context, preserving ranges.

#include "schema/blocks/data_copy_internal.hpp"
#include "schema/operations/data_operation_internal.hpp"
#include "schema/operations/operation_meter.hpp"
#include "schema/schema.hpp"
#include "schema/types/type_compatibility.hpp"

namespace schema::detail
{

bool CDataCopy::handle(const std::uint32_t source, const CDataEntryHandle entry,
    EDataEntryKind& kind, std::uint32_t& ordinal) noexcept
{
    return ((source < m_count) && m_sources[source] && m_sources[source]->entry_identity(entry, kind, ordinal)) ||
        m_operation.fail(EDataOperationReason::stale_handle);
}

bool CDataCopy::prepare(const CDataBlockView* const* const sources, const std::size_t count,
    const SDataCopyName* const names, const std::size_t name_count, CLiveTypes& types) noexcept
{
    m_operation.stage(EDataOperationStage::planning);
    if (!sources || !count || (count > UINT32_MAX) || (name_count && !names) || !m_operation.valid())
    {
        return m_operation.fail(EDataOperationReason::invalid_input);
    }
    m_sources = sources;
    m_count = count;
    m_names = names;
    m_name_count = name_count;
    if (!m_states.resize(count) || !m_canonical.resize(count))
    {
        return m_operation.fail(EDataOperationReason::allocation_failed);
    }
    CSchema catalogue;
    SSchemaDiagnostic schema_error;
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        if (!m_operation.work()) return false;
        if (!sources[index] || !sources[index]->is_ready())
        {
            return m_operation.fail(EDataOperationReason::invalid_input);
        }
        m_canonical[index] = index;
        for (std::uint32_t prior = 0u; prior < index; ++prior)
        {
            if (!m_operation.work()) return false;
            if (sources[index]->file_bytes().data() == sources[prior]->file_bytes().data())
            {
                m_canonical[index] = m_canonical[prior];
                break;
            }
        }
        if (m_canonical[index] != index) continue;
        m_operation.source_ordinal(index);
        if (!m_states[index].emplace(m_operation))
        {
            return m_operation.fail(EDataOperationReason::allocation_failed);
        }
        SDataViewState& state = *m_states[index];
        if (!state.load_bytes(sources[index]->file_bytes()))
        {
            return false;
        }
        CSchema incoming;
        if (!incoming.import_from(state.types, schema_error))
        {
            return m_operation.fail(EDataOperationReason::invalid_input);
        }
        if (!catalogue.document_ready()) catalogue = std::move(incoming);
        else if (!catalogue.merge_from(catalogue.document_query(), incoming.document_query(), schema_error))
        {
            return m_operation.fail((schema_error.reason == ESchemaReason::allocation_failed) ?
                EDataOperationReason::allocation_failed : EDataOperationReason::type_mismatch);
        }
    }
    SDiagnostic type_error;
    if (!admit_data_document(catalogue.document()) || !types.try_adopt(std::move(catalogue.edit())) || !types.resolve(type_error))
    {
        return m_operation.fail(EDataOperationReason::invalid_input);
    }
    for (std::size_t index = 0u; index < name_count; ++index)
    {
        EDataEntryKind kind{};
        std::uint32_t ordinal{};
        if (!m_operation.work() || !handle(names[index].source, names[index].entry, kind, ordinal))
        {
            return false;
        }
        if (!names[index].name.length())
        {
            return m_operation.fail(EDataOperationReason::invalid_input);
        }
        for (std::size_t prior = 0u; prior < index; ++prior)
        {
            if (!m_operation.work()) return false;
            EDataEntryKind prior_kind{};
            std::uint32_t prior_ordinal{};
            if (!handle(names[prior].source, names[prior].entry, prior_kind, prior_ordinal))
            {
                return false;
            }
            if ((m_canonical[names[prior].source] == m_canonical[names[index].source]) &&
                (prior_kind == kind) && (prior_ordinal == ordinal))
            {
                return m_operation.fail(EDataOperationReason::naming_conflict);
            }
        }
    }
    return true;
}

CStringView CDataCopy::name(const std::uint32_t source, const EDataEntryKind kind,
    const std::uint32_t index, const CStringView& original) noexcept
{
    for (std::size_t ordinal = 0u; ordinal < m_name_count; ++ordinal)
    {
        if (!m_operation.work()) return {};
        const SDataCopyName& mapping = m_names[ordinal];
        if (m_canonical[mapping.source] != source) continue;
        EDataEntryKind mapped_kind{};
        std::uint32_t mapped_ordinal{};
        if (!handle(mapping.source, mapping.entry, mapped_kind, mapped_ordinal))
        {
            return {};
        }
        if ((mapped_kind == kind) && (mapped_ordinal == index))
        {
            return mapping.name;
        }
    }
    return original;
}

bool CDataCopy::selected(CDataCandidate& candidate, const SDataCopyRoot* const roots, const std::size_t count) noexcept
{
    if (count && !roots)
    {
        return m_operation.fail(EDataOperationReason::invalid_input);
    }
    for (std::size_t index = 0u; index < count; ++index)
    {
        EDataEntryKind kind{};
        std::uint32_t ordinal{};
        if (!m_operation.work() || !handle(roots[index].source, roots[index].entry, kind, ordinal))
        {
            return false;
        }
        std::uint32_t destination{};
        if (!import(candidate, m_canonical[roots[index].source], kind, ordinal, destination))
        {
            return false;
        }
    }
    return true;
}

bool CDataCopy::all(CDataCandidate& candidate) noexcept
{
    for (std::uint32_t source = 0u; source < m_count; ++source)
    {
        if (m_canonical[source] != source) continue;
        const CBakedDocument& document = m_states[source]->document;
        candidate.present_domains[0] |= static_cast<bool>(document.object_child(document.root(), CStringView{ "instances" }));
        candidate.present_domains[1] |= static_cast<bool>(document.object_child(document.root(), CStringView{ "data" }));
        for (const EDataEntryKind kind : { EDataEntryKind::instance, EDataEntryKind::collection })
        {
            const std::size_t count = (kind == EDataEntryKind::instance) ? m_states[source]->snapshots.size() : m_states[source]->collections.size();
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                std::uint32_t destination{};
                if (!import(candidate, source, kind, index, destination))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

bool CDataCopy::import(CDataCandidate& candidate, const std::uint32_t source, const EDataEntryKind kind,
    const std::uint32_t index, std::uint32_t& destination, const unsigned depth) noexcept
{
    if (!m_operation.work() || !m_operation.depth(depth))
    {
        return false;
    }
    for (std::size_t ordinal = 0u; ordinal < m_mappings.size(); ++ordinal)
    {
        if (!m_operation.work()) return false;
        const SMapping& mapping = m_mappings[ordinal];
        if ((mapping.source == source) && (mapping.kind == kind) && (mapping.index == index))
        {
            destination = mapping.destination;
            return true;
        }
    }
    SDataViewState& state = *m_states[source];
    CBakedValueIndex metadata, selection;
    CTypesIndex source_type;
    std::uint32_t parent = UINT32_MAX, count{}, offset{}, extent{};
    CByteConstView payload;
    if (kind == EDataEntryKind::instance)
    {
        const SBakedInstanceRecord& entry = state.snapshots[index];
        metadata = entry.entry;
        selection = entry.declaration;
        source_type = entry.type;
        count = 1u;
        offset = entry.offset;
        extent = entry.extent;
        payload = state.instance_bytes;
        if ((entry.parent != UINT32_MAX) && !import(candidate, source, kind, entry.parent, parent, (depth + 1u)))
        {
            return false;
        }
    }
    else
    {
        const SBakedBulkRecord& entry = state.collections[index];
        metadata = entry.entry;
        source_type = entry.type;
        count = entry.count;
        offset = entry.offset;
        extent = entry.extent;
        payload = state.bulk_bytes;
    }
    SType layout;
    if (!state.types.resolved()->type(source_type, layout))
    {
        return m_operation.fail(EDataOperationReason::invalid_input);
    }
    const CStringView type_name = (layout.category == ECategory::primitive) ? primitive_name(layout.primitive) : state.types.resolved()->name(layout.name);
    const CTypesIndex type = candidate.types.resolved()->find_type(type_name);
    CTypeCompatibility compatibility{ *state.types.resolved(), *candidate.types.resolved(), ETypeMatch::remapping };
    if (compatibility.compare(source_type, type) != ETypeMatchResult::match)
    {
        return m_operation.fail(EDataOperationReason::type_mismatch);
    }
    const CStringView destination_name = name(source, kind, index, state.document.name(metadata));
    if (!candidate.append(type, kind, destination_name, parent, count, destination))
    {
        return false;
    }
    if (!m_mappings.push_back({ source, index, destination, kind }))
    {
        return m_operation.fail(EDataOperationReason::allocation_failed);
    }
    if (!candidate.copy_bytes(candidate.entries[destination], CByteConstView{ (extent ? payload.data() + offset : nullptr), extent, 1u }))
    {
        return false;
    }
    if (selection)
    {
        const CNodeKey copied = candidate.copy_node(CDocumentRead{ state.document }, SOccurrence{ selection });
        if (!copied || !candidate.document.append_child(candidate.entries[destination].metadata, copied).succeeded())
        {
            return m_operation.fail(EDataOperationReason::allocation_failed);
        }
        candidate.entries[destination].selection = copied;
    }
    std::uint8_t* const bytes = candidate.entries[destination].bytes.data();
    for (std::uint32_t record = 0u; record < count; ++record)
    {
        SRelocation relocation{ *this, candidate, source, (layout.size ? bytes + (layout.size * record) : nullptr), depth };
        if (!visit_reference_slots(*state.types.resolved(), source_type, 0u, m_operation, &relocation, &SRelocation::slot, depth))
        {
            return false;
        }
    }
    return true;
}

bool CDataCopy::SRelocation::slot(void* const context, const SType& type, const SDataReferenceSlot slot) noexcept
{
    SRelocation& relocation = *static_cast<SRelocation*>(context);
    SDataViewState& source = *relocation.copy.m_states[relocation.source];
    std::uint8_t* const bytes = relocation.bytes + slot.offset;
    std::uint32_t id = static_cast<std::uint32_t>(read_scalar_bits(bytes, 4u));
    if (!id || (id == UINT32_MAX))
    {
        return true;
    }
    if (type.category == ECategory::text)
    {
        SDataTextView text;
        if (!source.text_id(type, bytes, text) || !relocation.candidate.strings.intern(text.stored_text, id))
        {
            return false;
        }
    }
    else
    {
        std::uint32_t collection{}, first{}, count{}, destination{};
        bool absent{};
        if (!source.span_id(type, bytes, collection, first, count, absent) ||
            !relocation.copy.import(relocation.candidate, relocation.source, EDataEntryKind::collection, collection,
                destination, (slot.depth + 1u))) return false;
        id = destination + 1u;
    }
    write_scalar_bits(bytes, 4u, id);
    return true;
}

}   // namespace schema::detail

namespace schema
{

bool copy_data(const CDataBlockView* const* const sources, const std::size_t source_count,
    const SDataCopyRoot* const roots, const std::size_t root_count, const SDataCopyName* const names,
    const std::size_t name_count, const SDataOperationLimits limits, CBakedDataBlock& output,
    SDataOperationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    detail::CDataOperation operation{ limits };
    const detail::CDataOperationScope scope{ operation };
    CLiveTypes types;
    detail::CDataCopy copy{ operation };
    detail::CDataCandidate candidate{ operation, types };
    const bool success = !output.loaded_ready() && copy.prepare(sources, source_count, names, name_count, types) &&
        candidate.initialise() && copy.selected(candidate, roots, root_count) && candidate.publish(output);
    if (!success)
    {
        if (output.loaded_ready()) (void)operation.fail(EDataOperationReason::destination_not_empty);
        operation.diagnose(diagnostic);
    }
    return success;
}

bool repack_data(const CDataBlockView& source, const SDataOperationLimits limits,
    CBakedDataBlock& output, SDataOperationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    detail::CDataOperation operation{ limits };
    const detail::CDataOperationScope scope{ operation };
    CLiveTypes types;
    detail::CDataCopy copy{ operation };
    detail::CDataCandidate candidate{ operation, types };
    const CDataBlockView* const sources[]{ &source };
    const bool success = !output.loaded_ready() && copy.prepare(sources, 1u, nullptr, 0u, types) &&
        candidate.initialise() && copy.all(candidate) && candidate.publish(output);
    if (!success)
    {
        if (output.loaded_ready()) (void)operation.fail(EDataOperationReason::destination_not_empty);
        operation.diagnose(diagnostic);
    }
    return success;
}

}   // namespace schema
