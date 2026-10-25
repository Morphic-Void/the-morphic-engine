
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema_runtime.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    25 Oct 26
//
//  Construct independent runtime owners using the aggregate's contained types.

#include "schema/schema.hpp"
#include "schema/live_instances.hpp"
#include "schema/live_bulk_data.hpp"
#include "schema/baked_data_block.hpp"
#include "schema/data_block_construction.hpp"
#include "schema/document_construction.hpp"
#include "schema/instance_selection.hpp"
#include "schema/data_reconstruction.hpp"
#include "schema/data_document_validation.hpp"
#include "data_model/document_copy.hpp"

#include <cstring>
#include <utility>

namespace schema
{

using detail::CDocumentRead;
using detail::SOccurrence;
using document_translation::detail::CDocumentConstruction;

[[nodiscard]] static bool publish_import(CSchema& destination, CLiveDocument&& document, SSchemaDiagnostic& diagnostic) noexcept
{
    if (destination.try_adopt(std::move(document), diagnostic)) return true;
    //  Admission sees private staging; retain reasons, never its borrowed handles.
    diagnostic.first = {};
    diagnostic.second = {};
    diagnostic.types.occurrence = {};
    diagnostic.types.enclosing_type = {};
    diagnostic.types.enclosing_member = {};
    diagnostic.types.related = {};
    diagnostic.instances.occurrence = {};
    diagnostic.bulk_data.occurrence = {};
    diagnostic.block.location = {};
    return false;
}

[[nodiscard]] static bool copy_sections(const CDocumentRead* const sources[3], CLiveDocument& destination,
    SSchemaDiagnostic& diagnostic) noexcept
{
    const CStringView names[]{ CStringView{ "types" }, CStringView{ "instances" }, CStringView{ "data" } };
    detail::SDocumentSection sections[3]{};
    for (unsigned i = 0u; i < 3u; ++i)
    {
        if (sources[i]) sections[i] = { sources[i], sources[i]->object_child(sources[i]->root(), names[i]) };
    }
    detail::SDocumentBuildDiagnostic error;
    if (!detail::copy_document_sections(sections, 3u, destination, error))
    {
        diagnostic.reason = (error.reason == detail::EDocumentBuildReason::storage_limit) ? ESchemaReason::storage_limit :
            ((error.reason == detail::EDocumentBuildReason::allocation_failed) ? ESchemaReason::allocation_failed : ESchemaReason::construction_failed);
        return false;
    }
    return true;
}

bool CSchema::compose_from(const CSchemaDocumentQuery& types, const CSchemaDocumentQuery& instances,
    const CSchemaDocumentQuery& bulk_data, SSchemaDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const CDocumentRead* sources[]{ types.is_ready() ? &types.m_read : nullptr,
        instances.is_ready() ? &instances.m_read : nullptr, bulk_data.is_ready() ? &bulk_data.m_read : nullptr };
    const CStringView names[]{ CStringView{ "types" }, CStringView{ "instances" }, CStringView{ "data" } };
    const ESchemaSource origins[]{ ESchemaSource::types, ESchemaSource::instances, ESchemaSource::bulk_data };
    for (unsigned index = 0u; index < 3u; ++index)
    {
        if (!sources[index]) continue;
        if (!admit(*sources[index], diagnostic))
        {
            diagnostic.source = origins[index];
            return false;
        }
        if (!sources[index]->object_child(sources[index]->root(), names[index]))
        {
            diagnostic.stage = ESchemaStage::structure;
            diagnostic.reason = ESchemaReason::invalid_source;
            diagnostic.source = origins[index];
            diagnostic.first = CSchemaHandle{ sources[index]->root() };
            return false;
        }
    }
    diagnostic.stage = ESchemaStage::copying;
    CLiveDocument staged;
    if (!copy_sections(sources, staged, diagnostic)) return false;
    m_document = std::move(staged);
    diagnostic = {};
    return true;
}

namespace detail
{

enum class EAggregateConstruction : std::uint8_t { validation = 0u, live, baked };

//  One proof and optional construction plan. Source references never escape the
//  operation; materialised type indices belong to the independently owned tree.
class CSchemaConstruction
{
public:
    explicit CSchemaConstruction(const CLiveDocument& document) noexcept : m_document(document), m_source(document) {}
    [[nodiscard]] bool prepare(const EAggregateConstruction target, SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool materialise(CLiveTypes& types, CLiveInstances& instances, CLiveBulkData& bulk,
        SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool bake(CBakedDataBlock& destination, SSchemaDiagnostic& diagnostic) noexcept;

private:
    void map_types_diagnostic(SDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool build_section(SDataConstructionPlan& plan, const bool bulk, CLiveDocument& target,
        SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool encode(const SDataConstructionPlan& plan, const bool bulk, std::uint8_t* bytes,
        SSchemaDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] static bool construction_failure(const bool bulk, const SOccurrence source,
        const EReason reason, SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] static bool block_failure(SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] static bool allocate_payload(const std::size_t size, CByteBuffer& bytes) noexcept;

    const CLiveDocument& m_document;
    CDocumentRead m_source;
    CLiveTypes m_owned_types;
    CResolvedTypes m_borrowed_types;
    const CResolvedTypes* m_types{};
    SOccurrence m_definitions, m_instance_section, m_bulk_section;
    SDataConstructionPlan m_instances, m_bulk;
};

void CSchemaConstruction::map_types_diagnostic(SDiagnostic& diagnostic) const noexcept
{
    const CDocumentRead copied{ SDocumentQueryAccess::read(m_owned_types.document_query()) };
    CTypesHandle* handles[]{ &diagnostic.occurrence, &diagnostic.enclosing_type, &diagnostic.enclosing_member, &diagnostic.related };
    SOccurrence locations[4]{};
    for (unsigned i = 0u; i < 4u; ++i)
    {
        locations[i] = STypesHandleAccess::occurrence(*handles[i]);
        *handles[i] = locations[i] == copied.root() ? STypesHandleAccess::make(m_source.root()) : CTypesHandle{};
    }
    const SOccurrence root = copied.object_child(copied.root(), CStringView{ "types" });
    SOccurrence from = root, to = m_definitions;
    //  Copying preserves subtree order. Only failures need this allocation-free
    //  correspondence walk, including nested defaults and all related locations.
    while (from)
    {
        for (unsigned i = 0u; i < 4u; ++i)
        {
            if (locations[i] == from) *handles[i] = STypesHandleAccess::make(to);
        }
        if (const auto child = copied.first_child(from))
        {
            from = child;
            to = m_source.first_child(to);
            continue;
        }
        while (from != root && !copied.next_sibling(from))
        {
            from = copied.parent(from);
            to = m_source.parent(to);
        }
        if (from == root) break;
        from = copied.next_sibling(from);
        to = m_source.next_sibling(to);
    }
}

bool CSchemaConstruction::prepare(const EAggregateConstruction target, SSchemaDiagnostic& diagnostic) noexcept
{
    m_definitions = m_source.object_child(m_source.root(), CStringView{ "types" });
    m_instance_section = m_source.object_child(m_source.root(), CStringView{ "instances" });
    m_bulk_section = m_source.object_child(m_source.root(), CStringView{ "data" });
    if (!m_definitions)
    {
        diagnostic.stage = ESchemaStage::types;
        diagnostic.reason = ESchemaReason::missing_types;
        diagnostic.first = CSchemaHandle{ m_source.root() };
        return false;
    }
    const bool independent = target == EAggregateConstruction::live;
    if (independent)
    {
        diagnostic.stage = ESchemaStage::materialisation;
        const CDocumentRead* sources[]{ &m_source, nullptr, nullptr };
        CLiveDocument definitions;
        if (!copy_sections(sources, definitions, diagnostic)) return false;
        if (!m_owned_types.try_adopt(std::move(definitions)))
        {
            diagnostic.reason = ESchemaReason::construction_failed;
            return false;
        }
    }
    const bool resolved = independent ? m_owned_types.resolve(diagnostic.types) : m_borrowed_types.resolve(m_document, diagnostic.types);
    if (!resolved)
    {
        if (independent) map_types_diagnostic(diagnostic.types);
        diagnostic.stage = ESchemaStage::types;
        diagnostic.reason = diagnostic.types.reason == EReason::allocation_failed ? ESchemaReason::allocation_failed :
            (diagnostic.types.reason == EReason::storage_limit ? ESchemaReason::storage_limit : ESchemaReason::invalid_types);
        diagnostic.first = CSchemaHandle{ STypesHandleAccess::occurrence(diagnostic.types.occurrence) };
        return false;
    }
    m_types = independent ? m_owned_types.resolved() : &m_borrowed_types;
    for (const bool bulk : { false, true })
    {
        SDataValidationDiagnostic error;
        auto* plan = target == EAggregateConstruction::validation ? nullptr : (bulk ? &m_bulk : &m_instances);
        if (!validate_data_section(m_source, bulk ? m_bulk_section : m_instance_section, *m_types, bulk, error, plan))
        {
            diagnostic.stage = bulk ? ESchemaStage::bulk_data : ESchemaStage::instances;
            diagnostic.reason = error.reason == EDataValidationReason::allocation_failed ? ESchemaReason::allocation_failed :
                (error.reason == EDataValidationReason::storage_limit ? ESchemaReason::storage_limit : ESchemaReason::invalid_data);
            diagnostic.first = CSchemaHandle{ error.occurrence };
            if (bulk)
            {
                diagnostic.bulk_data.reason = data_validation_reason<EBulkDataLoadReason>(error.reason);
                diagnostic.bulk_data.occurrence = SBulkDataHandleAccess::make(error.occurrence);
            }
            else
            {
                diagnostic.instances.reason = data_validation_reason<EInstanceLoadReason>(error.reason);
                diagnostic.instances.occurrence = SInstanceHandleAccess::make(error.occurrence);
            }
            return false;
        }
    }
    diagnostic = {};
    return true;
}

bool CSchemaConstruction::construction_failure(const bool bulk, const SOccurrence source,
    const EReason reason, SSchemaDiagnostic& diagnostic) noexcept
{
    diagnostic.stage = ESchemaStage::materialisation;
    diagnostic.reason = reason == EReason::allocation_failed ? ESchemaReason::allocation_failed :
        (reason == EReason::storage_limit ? ESchemaReason::storage_limit : ESchemaReason::construction_failed);
    diagnostic.first = CSchemaHandle{ source };
    const SValueDiagnostic error{ reason, source };
    if (bulk)
    {
        diagnostic.bulk_data.reason = value_failure(error, EBulkDataLoadReason::incomplete_record);
        diagnostic.bulk_data.occurrence = SBulkDataHandleAccess::make(source);
    }
    else
    {
        diagnostic.instances.reason = value_failure(error, EInstanceLoadReason::invalid_declaration);
        diagnostic.instances.occurrence = SInstanceHandleAccess::make(source);
    }
    return false;
}

bool CSchemaConstruction::build_section(SDataConstructionPlan& plan, const bool bulk, CLiveDocument& target,
    SSchemaDiagnostic& diagnostic) noexcept
{
    const auto section = target.create_object(CStringView{ bulk ? "data" : "instances" });
    if (!section || !CDocumentConstruction::append(target, target.root(), section))
    {
        return construction_failure(bulk, bulk ? m_bulk_section : m_instance_section, EReason::allocation_failed, diagnostic);
    }
    for (std::size_t g = 0u; g < plan.groups.size(); ++g)
    {
        const auto& group = plan.groups[g];
        const auto output_group = target.create_object(m_source.name(group.source));
        if (!output_group || !CDocumentConstruction::append(target, section, output_group))
        {
            return construction_failure(bulk, group.source, EReason::allocation_failed, diagnostic);
        }
        for (std::uint32_t i = group.first; i < group.first + group.count; ++i)
        {
            auto& entry = plan.entries[i];
            if (m_source.name(entry.source).empty())
            {
                (void)construction_failure(bulk, entry.source, EReason::invalid_input, diagnostic);
                if (bulk) diagnostic.bulk_data.reason = EBulkDataLoadReason::invalid_input;
                else diagnostic.instances.reason = EInstanceLoadReason::invalid_input;
                return false;
            }
            entry.target = target.create_object(m_source.name(entry.source));
            const auto locator = bulk ? CLiveBulkData::make_locator(target, entry.offset, entry.count, entry.extent) :
                CLiveInstances::make_locator(target, entry.offset, entry.extent);
            const auto parent = entry.parent == UINT32_MAX ? output_group : plan.entries[entry.parent].specialisations;
            if (!entry.target || !locator || !CDocumentConstruction::append(target, entry.target, locator) ||
                !CDocumentConstruction::append(target, parent, entry.target))
            {
                return construction_failure(bulk, entry.source, EReason::allocation_failed, diagnostic);
            }
            if (!bulk && entry.declaration)
            {
                SValueDiagnostic error;
                entry.selection = strip_selection(target, *m_types, group.type, { &m_source, entry.declaration }, CStringView{ "declaration" }, error);
                if (error.reason != EReason::none || !entry.selection || !CDocumentConstruction::append(target, entry.target, entry.selection))
                {
                    return construction_failure(false, entry.source, error.reason == EReason::none ? EReason::allocation_failed : error.reason, diagnostic);
                }
            }
            if (entry.children)
            {
                entry.specialisations = target.create_object(CStringView{ "specialisation" });
                if (!entry.specialisations || !CDocumentConstruction::append(target, entry.target, entry.specialisations))
                {
                    return construction_failure(false, entry.source, EReason::allocation_failed, diagnostic);
                }
            }
        }
    }
    return true;
}

bool CSchemaConstruction::encode(const SDataConstructionPlan& plan, const bool bulk, std::uint8_t* bytes,
    SSchemaDiagnostic& diagnostic) const noexcept
{
    //  Authored construction starts with zero padding and gaps. Alternatives
    //  subsequently copy complete immediate-parent snapshots before applying changes.
    if (plan.payload_size) std::memset(bytes, 0, plan.payload_size);
    for (std::size_t g = 0u; g < plan.groups.size(); ++g)
    {
        const auto& group = plan.groups[g];
        SType layout;
        (void)m_types->type(group.type, layout);
        for (std::uint32_t i = group.first; i < group.first + group.count; ++i)
        {
            const auto& entry = plan.entries[i];
            auto* destination = entry.extent ? bytes + entry.offset : nullptr;
            SValueDiagnostic error;
            if (bulk)
            {
                auto value = m_source.first_child(entry.declaration);
                for (std::uint32_t ordinal = 0u; ordinal < entry.count; ++ordinal)
                {
                    if (!construct_value(*m_types, m_source, group.type, value,
                        layout.size ? destination + layout.size * ordinal : nullptr, static_cast<std::size_t>(layout.size),
                        EConstructionMode::complete_bulk_data, error, m_source.is_object_entry(value)))
                    {
                        return construction_failure(true, entry.source, error.reason, diagnostic);
                    }
                    value = m_source.next_sibling(value);
                }
            }
            else
            {
                const bool built = entry.parent == UINT32_MAX ?
                    construct_value(*m_types, m_source, group.type, entry.declaration, destination, entry.extent, EConstructionMode::instance, error) :
                    construct_alternative(*m_types, m_source, group.type, entry.declaration,
                        entry.extent ? bytes + plan.entries[entry.parent].offset : nullptr, entry.extent, destination, entry.extent, error);
                if (!built) return construction_failure(false, entry.source, error.reason, diagnostic);
            }
        }
    }
    return true;
}

bool CSchemaConstruction::allocate_payload(const std::size_t size, CByteBuffer& bytes) noexcept
{
    return !size || (bytes.allocate(size, k_max_alignment) && bytes.set_size(size));
}

bool CSchemaConstruction::materialise(CLiveTypes& types, CLiveInstances& instances, CLiveBulkData& bulk,
    SSchemaDiagnostic& diagnostic) noexcept
{
    diagnostic.stage = ESchemaStage::materialisation;
    CLiveInstances staged_instances;
    CLiveBulkData staged_bulk;
    for (const bool is_bulk : { false, true })
    {
        const auto section = is_bulk ? m_bulk_section : m_instance_section;
        if (!section) continue;
        auto& plan = is_bulk ? m_bulk : m_instances;
        auto& document = is_bulk ? staged_bulk.m_document : staged_instances.m_document;
        auto& payload = is_bulk ? staged_bulk.m_payload : staged_instances.m_payload;
        if (!document.initialise() || !build_section(plan, is_bulk, document, diagnostic) ||
            !allocate_payload(plan.payload_size, payload))
        {
            if (diagnostic.reason == ESchemaReason::none) (void)construction_failure(is_bulk, section, EReason::allocation_failed, diagnostic);
            return false;
        }
        if (!encode(plan, is_bulk, payload.data(), diagnostic)) return false;
        const bool reserved = is_bulk ? staged_bulk.m_records.reserve(plan.entries.size()) : staged_instances.m_records.reserve(plan.entries.size());
        if (!reserved) return construction_failure(is_bulk, section, EReason::allocation_failed, diagnostic);
        for (std::size_t g = 0u; g < plan.groups.size(); ++g)
        {
            const auto& group = plan.groups[g];
            for (std::uint32_t i = group.first; i < group.first + group.count; ++i)
            {
                const auto& entry = plan.entries[i];
                const bool added = is_bulk ? staged_bulk.m_records.push_back({ entry.target, group.type, entry.count, entry.offset, entry.extent }) :
                    staged_instances.m_records.push_back({ entry.target, entry.selection, group.type, entry.parent, entry.offset, entry.extent });
                if (!added) return construction_failure(is_bulk, entry.source, EReason::allocation_failed, diagnostic);
            }
        }
    }
    //  All fallible work is complete. No temporary role borrows the staged types;
    //  transfer the unchanged tree/resolution before linking the published owners.
    if (!types.try_take_from(std::move(m_owned_types)))
    {
        diagnostic.reason = ESchemaReason::construction_failed;
        return false;
    }
    if (m_instance_section)
    {
        (void)staged_instances.m_binding.bind(types);
        staged_instances.m_loaded = true;
        instances.take_from(staged_instances);
    }
    if (m_bulk_section)
    {
        (void)staged_bulk.m_binding.bind(types);
        staged_bulk.m_loaded = true;
        bulk.take_from(staged_bulk);
    }
    diagnostic = {};
    return true;
}

bool CSchemaConstruction::block_failure(SSchemaDiagnostic& diagnostic) noexcept
{
    diagnostic.stage = ESchemaStage::materialisation;
    diagnostic.reason = diagnostic.block.reason == EDataBlockReason::allocation_failed ? ESchemaReason::allocation_failed :
        (diagnostic.block.reason == EDataBlockReason::storage_limit ? ESchemaReason::storage_limit : ESchemaReason::construction_failed);
    //  Block preparation/adoption inspect private descriptions and storage.
    diagnostic.block.location = {};
    return false;
}

bool CSchemaConstruction::bake(CBakedDataBlock& destination, SSchemaDiagnostic& diagnostic) noexcept
{
    diagnostic.stage = ESchemaStage::materialisation;
    CLiveDocument description;
    const CDocumentRead* sources[]{ &m_source, nullptr, nullptr };
    if (!copy_sections(sources, description, diagnostic) ||
        (m_instance_section && !build_section(m_instances, false, description, diagnostic)) ||
        (m_bulk_section && !build_section(m_bulk, true, description, diagnostic))) return false;
    CByteBuffer storage;
    SDataBlockLayout layout;
    if (!prepare_common_data_block_storage(description, m_instance_section ? &m_instances.payload_size : nullptr,
        m_bulk_section ? &m_bulk.payload_size : nullptr, storage, layout, diagnostic.block)) return block_failure(diagnostic);
    if (!encode(m_instances, false, m_instances.payload_size ? storage.data() + layout.instance_offset : nullptr, diagnostic) ||
        !encode(m_bulk, true, m_bulk.payload_size ? storage.data() + layout.bulk_offset : nullptr, diagnostic)) return false;
    CBakedDocumentBlock block;
    if (!block.adopt_buffer(std::move(storage)))
    {
        diagnostic.block.stage = EDataBlockStage::assembly;
        diagnostic.block.reason = EDataBlockReason::assembly_failed;
        return block_failure(diagnostic);
    }
    if (!destination.adopt_local(std::move(block), diagnostic.block)) return block_failure(diagnostic);
    diagnostic = {};
    return true;
}

}   // namespace detail

bool CSchema::validate(SSchemaDiagnostic& diagnostic) const noexcept
{
    if (!admit(CDocumentRead{ m_document }, diagnostic)) return false;
    detail::CSchemaConstruction context{ m_document };
    return context.prepare(detail::EAggregateConstruction::validation, diagnostic);
}

bool CSchema::materialise(CLiveTypes& types, CLiveInstances& instances, CLiveBulkData& bulk_data,
    SSchemaDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (types.document_ready() || types.reference_count() || instances.document_ready() || bulk_data.document_ready())
    {
        diagnostic.stage = ESchemaStage::materialisation;
        diagnostic.reason = ESchemaReason::invalid_source;
        return false;
    }
    if (!admit(CDocumentRead{ m_document }, diagnostic)) return false;
    detail::CSchemaConstruction context{ m_document };
    return context.prepare(detail::EAggregateConstruction::live, diagnostic) && context.materialise(types, instances, bulk_data, diagnostic);
}

bool CSchema::bake(CBakedDataBlock& destination, SSchemaDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (destination.backing() != EEntityBacking::none)
    {
        diagnostic.stage = ESchemaStage::materialisation;
        diagnostic.reason = ESchemaReason::invalid_source;
        return false;
    }
    if (!admit(CDocumentRead{ m_document }, diagnostic)) return false;
    detail::CSchemaConstruction context{ m_document };
    return context.prepare(detail::EAggregateConstruction::baked, diagnostic) && context.bake(destination, diagnostic);
}


bool CSchema::import_types(const CTypesDocumentQuery& source, SSchemaDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    diagnostic.stage = ESchemaStage::copying;
    if (!source.is_ready())
    {
        diagnostic.reason = ESchemaReason::invalid_source;
        return false;
    }
    const CDocumentRead* sources[]{ &detail::SDocumentQueryAccess::read(source), nullptr, nullptr };
    CLiveDocument staged;
    return copy_sections(sources, staged, diagnostic) && publish_import(*this, std::move(staged), diagnostic);
}

template<class TSource>
[[nodiscard]] static bool import_instance_group(const TSource& source, const CResolvedTypes& types,
    const CInstanceDocumentQuery& query, const CInstanceHandle group, CLiveDocument& output,
    const CNodeKey target, SSchemaDiagnostic& diagnostic, const unsigned depth = 2u) noexcept
{
    const auto& read = detail::SDocumentQueryAccess::read(query);
    for (auto entry = query.first_child(group); entry; entry = query.next_sibling(entry))
    {
        if (depth + 1u >= document_limits::k_max_recursion_depth)
        {
            diagnostic.reason = ESchemaReason::storage_limit;
            diagnostic.instances = { EInstanceLoadReason::invalid_range, entry };
            return false;
        }
        SInstanceEntryView value;
        if (!source.entry(entry, value)) return false;
        const auto result = output.create_object(query.name(entry));
        if (!result || !CDocumentConstruction::append(output, target, result)) return false;
        detail::SValueDiagnostic error;
        const auto declaration = detail::export_selection(output, types, value.type,
            { &read, detail::SInstanceHandleAccess::occurrence(value.declaration) }, value.bytes, CStringView{ "declaration" }, error);
        if (error.reason != EReason::none)
        {
            diagnostic.instances.reason = (error.reason == EReason::allocation_failed) ? EInstanceLoadReason::allocation_failed : EInstanceLoadReason::unrepresentable_value;
            diagnostic.instances.occurrence = entry;
            return false;
        }
        if (declaration && !CDocumentConstruction::append(output, result, declaration)) return false;
        const auto nested = query.object_child(entry, CStringView{ "specialisation" });
        if (nested)
        {
            const auto children = output.create_object(CStringView{ "specialisation" });
            if (!children || !CDocumentConstruction::append(output, result, children) ||
                !import_instance_group(source, types, query, nested, output, children, diagnostic, depth + 2u)) return false;
        }
    }
    return true;
}

template<class TSource>
bool CSchema::import_instances(const TSource& source, SSchemaDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    diagnostic.stage = ESchemaStage::copying;
    const auto* types = source.m_binding.resolved();
    if (!source.loaded_ready() || !types)
    {
        diagnostic.reason = ESchemaReason::invalid_source;
        return false;
    }
    CLiveDocument staged;
    if (!staged.initialise())
    {
        diagnostic.reason = ESchemaReason::allocation_failed;
        return false;
    }
    const auto query = source.document_query();
    const auto root = query.object_child(query.root(), CStringView{ "instances" });
    if (root)
    {
        const auto section = staged.create_object(CStringView{ "instances" });
        if (!section || !CDocumentConstruction::append(staged, staged.root(), section))
        {
            diagnostic.reason = ESchemaReason::allocation_failed;
            return false;
        }
        for (auto group = query.first_child(root); group; group = query.next_sibling(group))
        {
            const auto target = staged.create_object(query.name(group));
            if (!target || !CDocumentConstruction::append(staged, section, target) ||
                !import_instance_group(source, *types, query, group, staged, target, diagnostic))
            {
                if (diagnostic.reason == ESchemaReason::none)
                {
                    diagnostic.reason = (diagnostic.instances.reason == EInstanceLoadReason::unrepresentable_value) ?
                        ESchemaReason::invalid_data : ESchemaReason::allocation_failed;
                }
                return false;
            }
        }
    }
    return publish_import(*this, std::move(staged), diagnostic);
}

template<class TSource>
bool CSchema::import_bulk(const TSource& source, SSchemaDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    diagnostic.stage = ESchemaStage::copying;
    const auto* types = source.m_binding.resolved();
    if (!source.loaded_ready() || !types)
    {
        diagnostic.reason = ESchemaReason::invalid_source;
        return false;
    }
    CLiveDocument staged;
    if (!staged.initialise())
    {
        diagnostic.reason = ESchemaReason::allocation_failed;
        return false;
    }
    const auto query = source.document_query();
    const auto root = query.object_child(query.root(), CStringView{ "data" });
    if (root)
    {
        const auto section = staged.create_object(CStringView{ "data" });
        if (!section || !CDocumentConstruction::append(staged, staged.root(), section))
        {
            diagnostic.reason = ESchemaReason::allocation_failed;
            return false;
        }
        for (auto group = query.first_child(root); group; group = query.next_sibling(group))
        {
            const auto target = staged.create_object(query.name(group));
            if (!target || !CDocumentConstruction::append(staged, section, target))
            {
                diagnostic.reason = ESchemaReason::allocation_failed;
                return false;
            }
            for (auto entry = query.first_child(group); entry; entry = query.next_sibling(entry))
            {
                SBulkDataEntryView value;
                diagnostic.bulk_data.occurrence = entry;
                const bool available = source.entry(entry, value);
                if (!available || !value.count)
                {
                    diagnostic.bulk_data.reason = available ? EBulkDataLoadReason::invalid_count : EBulkDataLoadReason::invalid_input;
                    diagnostic.reason = ESchemaReason::invalid_data;
                    return false;
                }
                const auto created = staged.create_object(query.name(entry));
                if (!created || !CDocumentConstruction::append(staged, target, created))
                {
                    diagnostic.bulk_data.reason = EBulkDataLoadReason::allocation_failed;
                    diagnostic.reason = ESchemaReason::allocation_failed;
                    return false;
                }
                if (!detail::reconstruct_bulk_records(staged, *types, created, value.type, value.bytes, value.count, diagnostic.bulk_data.reason))
                {
                    diagnostic.reason = diagnostic.bulk_data.reason == EBulkDataLoadReason::allocation_failed ?
                        ESchemaReason::allocation_failed : ESchemaReason::invalid_data;
                    return false;
                }
            }
        }
    }
    return publish_import(*this, std::move(staged), diagnostic);
}

bool CSchema::import_from(const CLiveTypes& source, SSchemaDiagnostic& diagnostic) noexcept { return import_types(source.document_query(), diagnostic); }
bool CSchema::import_from(const CBakedTypes& source, SSchemaDiagnostic& diagnostic) noexcept { return import_types(source.document_query(), diagnostic); }
bool CSchema::import_from(const CLiveInstances& source, SSchemaDiagnostic& diagnostic) noexcept { return import_instances(source, diagnostic); }
bool CSchema::import_from(const CBakedInstances& source, SSchemaDiagnostic& diagnostic) noexcept { return import_instances(source, diagnostic); }
bool CSchema::import_from(const CLiveBulkData& source, SSchemaDiagnostic& diagnostic) noexcept { return import_bulk(source, diagnostic); }
bool CSchema::import_from(const CBakedBulkData& source, SSchemaDiagnostic& diagnostic) noexcept { return import_bulk(source, diagnostic); }

}   // namespace schema
