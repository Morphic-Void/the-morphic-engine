
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_preparation.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Oct 26
//
//  Selected-section preparation without role binding or payload copying.

#include "schema/data_preparation.hpp"
#include "schema/data_document_validation.hpp"
#include "schema/data_storage_validation.hpp"
#include "schema/data_block_format.hpp"
#include "schema/data_reconstruction.hpp"
#include "schema/document_construction.hpp"
#include "schema/value_codec.hpp"
#include "data_model/document_copy.hpp"

#include <type_traits>
#include <utility>

namespace schema
{

using detail::CDocumentRead;
using detail::SOccurrence;
using document_translation::detail::CDocumentConstruction;

template<bool Bulk>
class CDataPreparation
{
    using TReason = std::conditional_t<Bulk, EBulkDataLoadReason, EInstanceLoadReason>;
    using TDiagnostic = std::conditional_t<Bulk, SBulkDataPreparationDiagnostic, SInstancePreparationDiagnostic>;
    using TAccess = std::conditional_t<Bulk, detail::SBulkDataHandleAccess, detail::SInstanceHandleAccess>;
    struct SGroup
    {
        SOccurrence source;
        CNodeKey output;
    };
    struct SEntry
    {
        SOccurrence source, declaration, children;
        CSchemaIndex type;
        std::uint32_t group{}, parent{ UINT32_MAX }, offset{}, count{}, extent{};
        unsigned depth{};
        bool reconstruct{};
        CNodeKey output, specialisations;
    };
    struct SFrame
    {
        SOccurrence next;
        std::uint32_t parent;
        unsigned depth;
    };

public:
    CDataPreparation(const CDocumentRead& source, const CResolvedSchema& schema,
        const CByteConstView* const payload, const bool compare, TDiagnostic& diagnostic) noexcept :
        m_source(source), m_schema(schema), m_payload(payload), m_compare(compare), m_diagnostic(diagnostic) {}

    [[nodiscard]] bool run(CLiveDocument& destination) noexcept;

private:
    [[nodiscard]] bool fail(const TReason reason, const SOccurrence occurrence = {},
        const EDataPreparationReason operation = EDataPreparationReason::invalid_source) noexcept;
    [[nodiscard]] bool build_failure(const detail::SDocumentBuildDiagnostic& error, const SOccurrence fallback = {}) noexcept;
    [[nodiscard]] bool admit_root() noexcept;
    [[nodiscard]] bool admit_entry(SEntry& entry, const SType& layout, std::uint64_t& cursor) noexcept;
    [[nodiscard]] bool validate_value(const SEntry& entry, const SType& layout) noexcept;
    [[nodiscard]] bool admit_entries() noexcept;
    [[nodiscard]] bool prepare_strings(CLiveDocument& staged) noexcept;
    [[nodiscard]] bool append(CLiveDocument& staged, const CNodeKey parent, const CNodeKey child) noexcept;
    [[nodiscard]] bool construct(CLiveDocument& staged) noexcept;
    [[nodiscard]] const std::uint8_t* bytes(const SEntry& entry) const noexcept
    {
        return entry.extent ? (m_payload->data() + entry.offset) : nullptr;
    }
    const CDocumentRead& m_source;
    const CResolvedSchema& m_schema;
    const CByteConstView* const m_payload;
    const bool m_compare;
    TDiagnostic& m_diagnostic;
    const CStringView m_section_name{ Bulk ? "data" : "instances" };
    SOccurrence m_section;
    bool m_stripped{};
    TPodVector<SGroup> m_groups;
    TPodVector<SEntry> m_entries;
};

template<bool Bulk>
bool CDataPreparation<Bulk>::run(CLiveDocument& destination) noexcept
{
    m_diagnostic = {};
    m_diagnostic.stage = EDataPreparationStage::preconditions;
    if (!m_schema.is_ready() || detail::SDocumentQueryAccess::backs(m_schema, destination))
    {
        return fail(TReason::none, {}, EDataPreparationReason::invalid_schema);
    }
    m_diagnostic.stage = EDataPreparationStage::source_validation;
    if (!admit_root() || !admit_entries())
    {
        return false;
    }
    CLiveDocument staged;
    m_diagnostic.stage = EDataPreparationStage::document_preparation;
    if (!prepare_strings(staged) || !construct(staged))
    {
        return false;
    }
    m_diagnostic.stage = EDataPreparationStage::result_validation;
    detail::SDataValidationDiagnostic error;
    if (!detail::validate_data_document(CDocumentRead{ staged }, m_schema, Bulk, error))
    {
        return fail(detail::data_validation_reason<TReason>(error.reason), {},
            ((error.reason == detail::EDataValidationReason::storage_limit) ? EDataPreparationReason::storage_limit : EDataPreparationReason::invalid_result));
    }
    destination = std::move(staged);
    m_diagnostic = {};
    return true;
}

template<bool Bulk>
bool CDataPreparation<Bulk>::fail(const TReason reason, const SOccurrence occurrence,
    const EDataPreparationReason operation) noexcept
{
    m_diagnostic.reason = (reason == TReason::allocation_failed) ? EDataPreparationReason::allocation_failed : operation;
    m_diagnostic.detail.reason = reason;
    m_diagnostic.detail.occurrence = TAccess::make(occurrence);
    return false;
}

template<bool Bulk>
bool CDataPreparation<Bulk>::build_failure(const detail::SDocumentBuildDiagnostic& error, const SOccurrence fallback) noexcept
{
    const auto reason = (error.reason == detail::EDocumentBuildReason::allocation_failed) ? EDataPreparationReason::allocation_failed :
        ((error.reason == detail::EDocumentBuildReason::storage_limit) ? EDataPreparationReason::storage_limit : EDataPreparationReason::construction_failed);
    return fail((reason == EDataPreparationReason::allocation_failed) ? TReason::allocation_failed : TReason::none,
        (error.occurrence ? error.occurrence : fallback), reason);
}

template<bool Bulk>
bool CDataPreparation<Bulk>::admit_root() noexcept
{
    const SOccurrence root = m_source.root();
    if (!m_source.is_ready() || (m_source.value_kind(root) != EDocumentValueKind::object))
    {
        return fail(TReason::invalid_input, root);
    }
    detail::SDataBlockRoot metadata;
    SDataBlockDiagnostic error;
    if (!detail::read_data_block_root(m_source, metadata, error))
    {
        const SOccurrence where = error.location.live_node ? SOccurrence{ error.location.live_node } : SOccurrence{ error.location.baked_value };
        TReason reason = TReason::invalid_input;
        if (error.reason == EDataBlockReason::unknown_property)
        {
            reason = TReason::unknown_property;
        }
        else if (error.reason == EDataBlockReason::missing_property)
        {
            reason = TReason::missing_property;
        }
        else if ((m_source.name(where) == CStringView{ "instancePayloadSize" }) || (m_source.name(where) == CStringView{ "bulkPayloadSize" }))
        {
            //  Preserve the preparation diagnostic for malformed payload lengths.
            reason = TReason::invalid_range;
        }
        return fail(reason, where);
    }
    m_section = Bulk ? metadata.bulk_data : metadata.instances;
    m_stripped = metadata.stripped;
    const SOccurrence payload_size = Bulk ? metadata.bulk_size : metadata.instance_size;
    const std::uint64_t bytes = Bulk ? metadata.bulk_bytes : metadata.instance_bytes;
    if (!m_payload && (payload_size || m_stripped)) return fail(TReason::binary_required, payload_size ? payload_size : root);
    if (m_payload && payload_size && (bytes != m_payload->size())) return fail(TReason::invalid_range, payload_size);
    if (m_payload && ((m_payload->size() > memory::k_byte_size_ceiling) ||
        (m_payload->size() && (!m_payload->is_ready() ||
            ((reinterpret_cast<std::uintptr_t>(m_payload->data()) & (detail::k_data_block_alignment - 1u)) != 0u))) ||
        ((!m_section || (m_source.child_count(m_section) == 0u)) && m_payload->size())))
    {
        return fail(TReason::invalid_range, m_section);
    }
    return true;
}

template<bool Bulk>
bool CDataPreparation<Bulk>::admit_entry(SEntry& entry, const SType& layout, std::uint64_t& cursor) noexcept
{
    if (entry.depth >= document_limits::k_max_recursion_depth)
    {
        return fail(TReason::invalid_range, entry.source, EDataPreparationReason::storage_limit);
    }
    if (!m_source.is_object_entry(entry.source) || (m_source.value_kind(entry.source) != EDocumentValueKind::object))
    {
        return fail(TReason::invalid_input, entry.source);
    }
    SOccurrence locator;
    for (SOccurrence property = m_source.first_child(entry.source); property; property = m_source.next_sibling(property))
    {
        const CStringView name = m_source.name(property);
        if (!detail::data_entry_property(name, Bulk, true))
        {
            return fail(TReason::unknown_property, property);
        }
        if (name == CStringView{ "locator" }) locator = property;
        else if (name == CStringView{ "specialisation" }) entry.children = property;
        else entry.declaration = property;
    }
    if (entry.children && (m_source.value_kind(entry.children) != EDocumentValueKind::object))
    {
        return fail(TReason::invalid_input, entry.children);
    }
    if (entry.declaration && m_stripped)
    {
        if constexpr (Bulk)
        {
            return fail(TReason::invalid_input, entry.declaration);
        }
        else
        {
            return fail(TReason::invalid_declaration, entry.declaration);
        }
    }
    if constexpr (Bulk)
    {
        if (entry.declaration && (m_source.value_kind(entry.declaration) != EDocumentValueKind::array))
        {
            return fail(TReason::invalid_input, entry.declaration);
        }
    }
    if (locator && (m_source.value_kind(locator) != EDocumentValueKind::object))
    {
        return fail(TReason::invalid_locator, locator);
    }
    if (m_payload && !locator)
    {
        return fail(TReason::missing_property, entry.source);
    }
    detail::SDataLocator location;
    if (locator)
    {
        TReason reason{};
        SOccurrence occurrence;
        if (!detail::read_data_locator(m_source, locator, Bulk, layout.size, location, reason, occurrence))
        {
            return fail(reason, occurrence);
        }
    }
    std::uint64_t count = 1u, extent{};
    if constexpr (Bulk)
    {
        if (entry.declaration)
        {
            count = m_source.child_count(entry.declaration);
            if (!count || (location.count_node && (location.count != count)))
            {
                return fail(TReason::invalid_count, (location.count_node ? location.count_node : entry.declaration));
            }
        }
        else
        {
            if (!m_payload)
            {
                return fail(TReason::binary_required, entry.source);
            }
            count = location.count_node ? location.count :
                ((location.size_node && layout.size && ((location.size % layout.size) == 0u)) ? (location.size / layout.size) : 0u);
        }
    }
    if (!detail::data_extent(layout, count, extent))
    {
        return fail(TReason::invalid_count, (locator ? locator : entry.source));
    }
    if (location.size_node && (location.size != extent))
    {
        return fail(TReason::invalid_range, location.size_node);
    }
    if (m_payload)
    {
        if (!location.valid)
        {
            return fail(TReason::invalid_locator, location.valid_node);
        }
        TReason reason{};
        if (!detail::check_data_range(location.offset, extent, layout.alignment, cursor, true, m_payload->size(), reason))
        {
            return fail(reason, locator);
        }
        if (extent || location.offset) cursor = location.offset + extent;
    }
    else if (location.valid)
    {
        return fail(TReason::binary_required, locator);
    }
    entry.offset = static_cast<std::uint32_t>(location.offset);
    entry.count = static_cast<std::uint32_t>(count);
    entry.extent = static_cast<std::uint32_t>(extent);
    entry.reconstruct = Bulk ? !entry.declaration : m_stripped;
    return entry.reconstruct || validate_value(entry, layout);
}

template<bool Bulk>
bool CDataPreparation<Bulk>::validate_value(const SEntry& entry, const SType& layout) noexcept
{
    const bool compare = m_payload && m_compare;
    SOccurrence value = Bulk ? m_source.first_child(entry.declaration) : entry.declaration;
    for (std::uint32_t i = 0u; i < entry.count; ++i)
    {
        if constexpr (Bulk)
        {
            if (m_source.is_object_entry(value) &&
                !((layout.category == ECategory::structure) ? m_schema.find_member(entry.type, m_source.name(value)) :
                    ((layout.category == ECategory::bit_structure) ? m_schema.find_field(entry.type, m_source.name(value)) : CSchemaIndex{})))
            {
                return fail(TReason::invalid_input, value);
            }
        }
        detail::SValueDiagnostic error;
        bool matches = true;
        bool valid{};
        if (!compare)
        {
            valid = detail::validate_declaration(m_schema, m_source, entry.type, value,
                (Bulk ? detail::EConstructionMode::complete_bulk_data : detail::EConstructionMode::instance),
                error, (Bulk && m_source.is_object_entry(value)));
        }
        else if (!Bulk && (entry.parent != UINT32_MAX))
        {
            const SEntry& parent = m_entries[entry.parent];
            valid = detail::check_alternative(m_schema, m_source, entry.type, value,
                bytes(parent), parent.extent, bytes(entry), entry.extent, matches, error);
        }
        else
        {
            const std::uint8_t* const actual = layout.size ? (bytes(entry) + (layout.size * i)) : nullptr;
            valid = detail::check_value(m_schema, m_source, entry.type, value, actual, static_cast<std::size_t>(layout.size),
                (Bulk ? detail::EConstructionMode::complete_bulk_data : detail::EConstructionMode::instance),
                matches, error, (Bulk && m_source.is_object_entry(value)));
        }
        if (!valid || !matches)
        {
            TReason invalid{};
            if constexpr (Bulk) invalid = TReason::incomplete_record;
            else invalid = TReason::invalid_declaration;
            const auto operation = (error.reason == EReason::storage_limit) ? EDataPreparationReason::storage_limit : EDataPreparationReason::invalid_source;
            return fail(detail::value_failure(error, (valid ? TReason::embedded_mismatch : invalid)),
                (error.occurrence ? error.occurrence : entry.source), operation);
        }
        if constexpr (Bulk) value = m_source.next_sibling(value);
    }
    return true;
}

template<bool Bulk>
bool CDataPreparation<Bulk>::admit_entries() noexcept
{
    std::uint64_t cursor{};
    TPodVector<SFrame> parents;
    std::size_t expected_entries{};
    const auto reserve_entries = [&](const SOccurrence container) noexcept
    {
        const auto count = m_source.child_count(container);
        if (count > (memory::t_max_elements<SEntry>() - expected_entries))
        {
            return fail(TReason::invalid_range, container, EDataPreparationReason::storage_limit);
        }
        expected_entries += count;
        return m_entries.reserve(expected_entries) || fail(TReason::allocation_failed, container);
    };
    for (SOccurrence group = m_source.first_child(m_section); group; group = m_source.next_sibling(group))
    {
        if (!m_source.is_object_entry(group) || (m_source.value_kind(group) != EDocumentValueKind::object))
        {
            return fail(TReason::invalid_input, group);
        }
        const CSchemaIndex type = m_schema.find_type(m_source.name(group));
        SType layout;
        if (!m_schema.type(type, layout))
        {
            return fail(TReason::unknown_type, group);
        }
        const auto group_index = static_cast<std::uint32_t>(m_groups.size());
        if (!group_index && !m_groups.reserve(m_source.child_count(m_section)))
        {
            return fail(TReason::allocation_failed, group);
        }
        if (!reserve_entries(group))
        {
            return false;
        }
        if (!m_groups.push_back({ group, {} }))
        {
            return fail(TReason::allocation_failed, group);
        }
        SFrame frame{ m_source.first_child(group), UINT32_MAX, 2u };
        while (frame.next || parents.size())
        {
            if (!frame.next)
            {
                (void)parents.pop_back(frame);
                continue;
            }
            SEntry entry;
            entry.source = frame.next;
            entry.parent = frame.parent;
            entry.depth = frame.depth;
            entry.group = group_index;
            entry.type = type;
            frame.next = m_source.next_sibling(frame.next);
            if (!admit_entry(entry, layout, cursor))
            {
                return false;
            }
            if (m_entries.size() >= UINT32_MAX)
            {
                return fail(TReason::invalid_range, entry.source, EDataPreparationReason::storage_limit);
            }
            const auto index = static_cast<std::uint32_t>(m_entries.size());
            if (!m_entries.push_back(entry))
            {
                return fail(TReason::allocation_failed, entry.source);
            }
            const SOccurrence child = m_source.first_child(entry.children);
            if (child)
            {
                if (!reserve_entries(entry.children))
                {
                    return false;
                }
                if (!parents.push_back(frame))
                {
                    return fail(TReason::allocation_failed, entry.source);
                }
                frame = { child, index, (entry.depth + 2u) };
            }
        }
    }
    return true;
}

template<bool Bulk>
bool CDataPreparation<Bulk>::prepare_strings(CLiveDocument& staged) noexcept
{
    detail::CDocumentStrings strings;
    //  Root, selected section, groups and entries are known without expanding types.
    std::uint64_t structural_nodes = 4u + (2u * (static_cast<std::uint64_t>(m_groups.size()) + m_entries.size()));
    if (!strings.name(m_section_name) || !strings.name(CStringView{ Bulk ? "data" : "declaration" }) ||
        (!Bulk && !strings.name(CStringView{ "specialisation" })))
    {
        return build_failure(strings.diagnostic());
    }
    for (std::size_t index = 0u; index < m_groups.size(); ++index)
    {
        const SGroup& group = m_groups[index];
        if (!strings.name(m_source.name(group.source)))
        {
            return build_failure(strings.diagnostic(), group.source);
        }
    }
    for (std::size_t index = 0u; index < m_entries.size(); ++index)
    {
        const SEntry& entry = m_entries[index];
        if (entry.children) structural_nodes += 2u;
        if (Bulk && entry.reconstruct) structural_nodes += 2u;
        if (!strings.name(m_source.name(entry.source)) ||
            !strings.collect(m_source, entry.declaration, (entry.depth + 1u)) ||
            (entry.reconstruct && !strings.reconstruction_type(m_schema, entry.type)))
        {
            return build_failure(strings.diagnostic(), entry.source);
        }
        if (entry.children && ((entry.depth + 1u) >= document_limits::k_max_recursion_depth))
        {
            return fail(TReason::invalid_range, entry.children, EDataPreparationReason::storage_limit);
        }
    }
    return (strings.initialise(staged, structural_nodes) && strings.preload(staged)) || build_failure(strings.diagnostic());
}

template<bool Bulk>
bool CDataPreparation<Bulk>::append(CLiveDocument& staged, const CNodeKey parent, const CNodeKey child) noexcept
{
    return (child && CDocumentConstruction::append(staged, parent, child)) || build_failure({});
}

template<bool Bulk>
bool CDataPreparation<Bulk>::construct(CLiveDocument& staged) noexcept
{
    const CNodeKey section = staged.create_object(m_section_name);
    if (!append(staged, staged.root(), section))
    {
        return false;
    }
    for (std::size_t index = 0u; index < m_groups.size(); ++index)
    {
        SGroup& group = m_groups[index];
        group.output = staged.create_object(m_source.name(group.source));
        if (!append(staged, section, group.output))
        {
            return false;
        }
    }
    CByteBuffer defaults;
    std::uint32_t default_group = UINT32_MAX;
    for (std::size_t index = 0u; index < m_entries.size(); ++index)
    {
        SEntry& entry = m_entries[index];
        entry.output = staged.create_object(m_source.name(entry.source));
        const CNodeKey parent = (entry.parent == UINT32_MAX) ? m_groups[entry.group].output : m_entries[entry.parent].specialisations;
        if (!append(staged, parent, entry.output))
        {
            return false;
        }
        if (entry.reconstruct)
        {
            TReason reason{};
            bool success{};
            if constexpr (Bulk)
            {
                success = detail::reconstruct_bulk_records(staged, m_schema, entry.output, entry.type,
                    bytes(entry), entry.count, reason, (document_limits::k_max_recursion_depth - entry.depth - 2u));
            }
            else
            {
                const bool base = entry.parent == UINT32_MAX;
                if (base && (default_group != entry.group))
                {
                    defaults.deallocate();
                    if (entry.extent && (!defaults.allocate(entry.extent, k_max_alignment) || !defaults.set_size(entry.extent)))
                    {
                        return fail(TReason::allocation_failed, entry.source);
                    }
                    detail::SValueDiagnostic error;
                    if (!detail::construct_value(m_schema, CDocumentRead{ staged }, entry.type, {}, defaults.data(), defaults.size(),
                        detail::EConstructionMode::instance, error))
                    {
                        return fail(detail::value_failure(error, TReason::unrepresentable_value), entry.source,
                            ((error.reason == EReason::storage_limit) ? EDataPreparationReason::storage_limit : EDataPreparationReason::invalid_source));
                    }
                    default_group = entry.group;
                }
                const std::uint8_t* const baseline = base ? defaults.data() : bytes(m_entries[entry.parent]);
                CNodeKey declaration;
                success = detail::reconstruct_instance(staged, m_schema, entry.type, bytes(entry), baseline, entry.extent,
                    base, declaration, reason, (document_limits::k_max_recursion_depth - entry.depth - 1u));
                if (success && declaration && !append(staged, entry.output, declaration))
                {
                    return false;
                }
            }
            if (!success)
            {
                return fail(reason, entry.source, ((reason == TReason::invalid_range) ? EDataPreparationReason::storage_limit : EDataPreparationReason::invalid_source));
            }
        }

        //  Retained property order survives; locators alone are omitted.
        for (SOccurrence property = m_source.first_child(entry.source); property; property = m_source.next_sibling(property))
        {
            if (property == entry.children)
            {
                entry.specialisations = staged.create_object(CStringView{ "specialisation" });
                if (!append(staged, entry.output, entry.specialisations))
                {
                    return false;
                }
            }
            else if (property == entry.declaration)
            {
                detail::SDocumentBuildDiagnostic error;
                const CNodeKey copied = detail::copy_document_subtree(m_source, property, staged, (entry.depth + 1u), error);
                if (!copied)
                {
                    return build_failure(error);
                }
                if (!append(staged, entry.output, copied))
                {
                    return false;
                }
            }
        }
    }
    return true;
}


bool prepare_instances_document(const CInstanceDocumentQuery& source, const CResolvedSchema& source_schema,
    CLiveDocument& destination, SInstancePreparationDiagnostic& diagnostic) noexcept
{
    CDataPreparation<false> operation{ detail::SDocumentQueryAccess::read(source), source_schema, nullptr, false, diagnostic };
    return operation.run(destination);
}

bool prepare_instances_document(const CInstanceDocumentQuery& source, const CResolvedSchema& source_schema,
    const CByteConstView& payload, const bool compare_embedded, CLiveDocument& destination, SInstancePreparationDiagnostic& diagnostic) noexcept
{
    CDataPreparation<false> operation{ detail::SDocumentQueryAccess::read(source), source_schema, &payload, compare_embedded, diagnostic };
    return operation.run(destination);
}

bool prepare_bulk_data_document(const CBulkDataDocumentQuery& source, const CResolvedSchema& source_schema,
    CLiveDocument& destination, SBulkDataPreparationDiagnostic& diagnostic) noexcept
{
    CDataPreparation<true> operation{ detail::SDocumentQueryAccess::read(source), source_schema, nullptr, false, diagnostic };
    return operation.run(destination);
}

bool prepare_bulk_data_document(const CBulkDataDocumentQuery& source, const CResolvedSchema& source_schema,
    const CByteConstView& payload, const bool compare_embedded, CLiveDocument& destination, SBulkDataPreparationDiagnostic& diagnostic) noexcept
{
    CDataPreparation<true> operation{ detail::SDocumentQueryAccess::read(source), source_schema, &payload, compare_embedded, diagnostic };
    return operation.run(destination);
}

}   // namespace schema
