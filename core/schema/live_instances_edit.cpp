
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_instances_edit.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    25 Oct 26
//
//  Transactional instance operations with explicit descendant policies.

#include "schema/live_instances.hpp"
#include "schema/instance_selection.hpp"
#include "data_model/document_copy.hpp"
#include "memory/memory_policies.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace schema
{

using detail::CDocumentRead;
using detail::SOccurrence;
using detail::SSelectionView;
using document_translation::detail::CDocumentConstruction;

//  Never reuse an edit identity, including when an owner is reconstructed at
//  the same address. Saturation prevents wraparound from reviving old tokens.
static std::atomic<std::uint64_t> s_edit_serial{};

[[nodiscard]] static std::uint64_t next_edit_serial() noexcept
{
    auto previous = s_edit_serial.load(std::memory_order_relaxed);
    while (previous != UINT64_MAX)
    {
        if (s_edit_serial.compare_exchange_weak(previous, previous + 1u, std::memory_order_relaxed)) return previous + 1u;
    }
    return 0u;
}

struct SResolvedSelectionStep
{
    EInstanceSelectionStepKind kind{};
    std::uint32_t ordinal{};
    CTypesIndex child_type;
    CStringView name;
};

[[nodiscard]] static bool resolve_steps(const CResolvedTypes& schema, const CTypesIndex root,
    const SInstanceSelectionStep* const steps, const std::size_t count,
    TPodVector<SResolvedSelectionStep>& resolved, EInstanceLoadReason& error) noexcept
{
    error = EInstanceLoadReason::invalid_declaration;
    CTypesIndex current = root;
    for (std::size_t i = 0u; i < count; ++i)
    {
        SType layout;
        if (!schema.type(current, layout))
        {
            return false;
        }
        SResolvedSelectionStep step;
        step.kind = steps[i].kind;
        if ((layout.category == ECategory::array) && (steps[i].kind == EInstanceSelectionStepKind::element))
        {
            if (steps[i].element >= layout.count)
            {
                return false;
            }
            step.ordinal = steps[i].element;
            step.child_type = layout.element_or_storage;
        }
        else if ((layout.category == ECategory::structure) && (steps[i].kind == EInstanceSelectionStepKind::member))
        {
            const CTypesIndex member_index = schema.find_member(current, steps[i].member);
            SMember member;
            if (!member_index || !schema.member(member_index, member))
            {
                return false;
            }
            step.child_type = member.type;
            step.name = schema.name(member.name);
            for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
            {
                if (schema.member_at(current, ordinal) == member_index)
                {
                    step.ordinal = ordinal;
                    break;
                }
            }
        }
        else if ((layout.category == ECategory::bit_structure) && (steps[i].kind == EInstanceSelectionStepKind::member))
        {
            const CTypesIndex field_index = schema.find_field(current, steps[i].member);
            SField field;
            if (!field_index || !schema.field(field_index, field))
            {
                return false;
            }
            step.child_type = field.type;
            step.name = schema.name(field.name);
            for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
            {
                if (schema.field_at(current, ordinal) == field_index)
                {
                    step.ordinal = ordinal;
                    break;
                }
            }
        }
        else
        {
            return false;
        }
        if (!resolved.push_back(step))
        {
            error = EInstanceLoadReason::allocation_failed;
            return false;
        }
        current = step.child_type;
    }
    error = EInstanceLoadReason::none;
    return true;
}

[[nodiscard]] static bool valid_policy(const EInstanceDescendantPolicy policy) noexcept
{
    return policy == EInstanceDescendantPolicy::preserve || policy == EInstanceDescendantPolicy::propagate;
}

[[nodiscard]] static bool allocate_bytes(CByteBuffer& buffer, const std::size_t size) noexcept
{
    return !size || (buffer.allocate(size, k_max_alignment) && buffer.set_size(size));
}

[[nodiscard]] static bool report_value(const detail::SValueDiagnostic& error, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic.reason = detail::value_failure(error, EInstanceLoadReason::invalid_declaration);
    diagnostic.occurrence = detail::SInstanceHandleAccess::make(error.occurrence);
    return false;
}

static void erase_detached(CLiveDocument& document, const CNodeKey value) noexcept
{
    if (value && document.contains(value) && document.is_detached(value)) (void)document.erase(value);
}

[[nodiscard]] static CNodeKey new_container(CLiveDocument& document, const SType& layout, const CStringView& name) noexcept
{
    return layout.category == ECategory::array ? document.create_array(name) : document.create_object(name);
}

//  Wrap a detached component in its root-relative path. Array positions before
//  the component are holes, so inserting a distant selection does not author them.
[[nodiscard]] static CNodeKey wrap_patch(CLiveDocument& document, const CResolvedTypes& types,
    const CTypesIndex root_type, const TPodVector<SResolvedSelectionStep>& steps, CNodeKey leaf) noexcept
{
    if (!leaf) return {};
    for (std::size_t depth = steps.size(); depth > 0u; --depth)
    {
        const auto& step = steps[depth - 1u];
        SType layout;
        (void)types.type((depth == 1u) ? root_type : steps[depth - 2u].child_type, layout);
        const CNodeKey parent = new_container(document, layout, {});
        if (!parent || !document.set_name(leaf, step.name))
        {
            erase_detached(document, parent);
            erase_detached(document, leaf);
            return {};
        }
        if (step.kind == EInstanceSelectionStepKind::element)
        {
            for (std::uint32_t i = 0u; i < step.ordinal; ++i)
            {
                const auto hole = document.create_null();
                if (!hole || !CDocumentConstruction::append(document, parent, hole))
                {
                    erase_detached(document, hole);
                    erase_detached(document, parent);
                    erase_detached(document, leaf);
                    return {};
                }
            }
        }
        if (!CDocumentConstruction::append(document, parent, leaf))
        {
            erase_detached(document, parent);
            erase_detached(document, leaf);
            return {};
        }
        leaf = parent;
    }
    if (!document.set_name(leaf, CStringView{ "declaration" }))
    {
        erase_detached(document, leaf);
        return {};
    }
    return leaf;
}

[[nodiscard]] static CNodeKey selected_path(CLiveDocument& document, CNodeKey root,
    const TPodVector<SResolvedSelectionStep>& steps) noexcept
{
    for (std::size_t i = 0u; root && i < steps.size(); ++i)
    {
        const auto& step = steps[i];
        if (step.kind == EInstanceSelectionStepKind::member) root = document.object_child(root, step.name);
        else
        {
            root = document.first_child(root);
            for (std::uint32_t j = 0u; root && j < step.ordinal; ++j) root = document.next_sibling(root);
            if (root && document.value_type(root) == ELiveValueType::null_value) return {};
        }
    }
    return root;
}

bool CLiveInstances::baseline(const std::uint32_t index, CByteBuffer& defaults,
    const std::uint8_t*& bytes, SInstanceDiagnostic& diagnostic) const noexcept
{
    const SRecord& record = m_records[index];
    if (record.parent != k_no_parent)
    {
        bytes = record.extent ? m_payload.data() + m_records[record.parent].offset : nullptr;
        return true;
    }
    if (!allocate_bytes(defaults, record.extent))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const CDocumentRead document{ m_document };
    detail::SValueDiagnostic error;
    if (!detail::construct_value(*m_binding.resolved(), document, record.type, {}, defaults.data(), defaults.size(),
        detail::EConstructionMode::instance, error)) return report_value(error, diagnostic);
    bytes = defaults.data();
    return true;
}

struct SStagedEdit
{
    std::uint32_t index{}, offset{};
    CNodeKey declaration;
    bool replace_selection{};
};

bool CLiveInstances::edit_existing(const std::uint32_t index, const CNodeKey selection,
    const CByteConstView& complete, const CNodeKey scope, const bool full_scope,
    const EInstanceDescendantPolicy policy, SInstanceDiagnostic& diagnostic) noexcept
{
    const CResolvedTypes& types = *m_binding.resolved();
    const CDocumentRead document{ m_document };
    const auto affected = full_scope ? SSelectionView::complete() : SSelectionView{ &document, SOccurrence{ scope } };
    const auto report = [&](const detail::SValueDiagnostic& error) noexcept
    {
        (void)report_value(error, diagnostic);
        diagnostic.occurrence = detail::SInstanceHandleAccess::make(SOccurrence{ m_records[index].entry });
        return false;
    };
    TPodVector<SStagedEdit> changes;
    TPodVector<std::uint32_t> positions;
    const auto cleanup = [&]() noexcept
    {
        for (std::size_t i = 0u; i < changes.size(); ++i) erase_detached(m_document, changes[i].declaration);
        erase_detached(m_document, selection);
    };
    const std::size_t candidates = m_records.size() - index;
    if (policy == EInstanceDescendantPolicy::propagate &&
        (!positions.allocate(candidates) || !positions.set_size(candidates)))
    {
        cleanup();
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (positions.size()) std::fill_n(positions.data(), positions.size(), k_no_parent);
    std::size_t total{};
    for (std::uint32_t i = index; i < m_records.size(); ++i)
    {
        const auto& record = m_records[i];
        if (i != index)
        {
            if (policy == EInstanceDescendantPolicy::preserve)
            {
                if (record.parent != index) continue;
            }
            else if (record.parent == k_no_parent || record.parent < index || positions[record.parent - index] == k_no_parent) continue;
        }
        const bool copies_bytes = (i == index) || (policy == EInstanceDescendantPolicy::propagate);
        if (copies_bytes) total = (total + k_max_alignment - 1u) & ~(static_cast<std::size_t>(k_max_alignment) - 1u);
        if ((copies_bytes && (total > memory::k_byte_size_ceiling || record.extent > memory::k_byte_size_ceiling - total)) ||
            !changes.push_back({ i, copies_bytes ? static_cast<std::uint32_t>(total) : 0u, (i == index) ? selection : CNodeKey{},
                (i == index) || policy == EInstanceDescendantPolicy::preserve }))
        {
            cleanup();
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
        if (policy == EInstanceDescendantPolicy::propagate) positions[i - index] = static_cast<std::uint32_t>(changes.size() - 1u);
        if (copies_bytes) total += record.extent;
    }
    CByteBuffer staged;
    if (!allocate_bytes(staged, total))
    {
        cleanup();
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    detail::SValueDiagnostic error;
    for (std::size_t i = 0u; i < changes.size(); ++i)
    {
        auto& change = changes[i];
        const auto& record = m_records[change.index];
        const auto* old = record.extent ? m_payload.data() + record.offset : nullptr;
        if (change.index == index)
        {
            if (record.extent) std::memcpy(staged.data() + change.offset, complete.data(), record.extent);
        }
        else if (policy == EInstanceDescendantPolicy::preserve)
        {
            change.declaration = detail::extend_selection(m_document, types, record.type,
                { &document, SOccurrence{ record.declaration } }, affected, complete.data(), old, false,
                CStringView{ "declaration" }, error);
        }
        else
        {
            auto* target = record.extent ? staged.data() + change.offset : nullptr;
            const auto parent_offset = changes[positions[record.parent - index]].offset;
            const auto* parent = record.extent ? staged.data() + parent_offset : nullptr;
            if (record.extent) std::memcpy(target, old, record.extent);
            if (!detail::inherit_selection(types, record.type, { &document, SOccurrence{ record.declaration } },
                affected, parent, target, error))
            {
                cleanup();
                return report(error);
            }
        }
        if (error.reason != EReason::none)
        {
            cleanup();
            return report(error);
        }
    }
    //  All fallible construction is complete. These disjoint detached trees
    //  replace known properties; entry keys and the payload allocation stay put.
    for (std::size_t i = 0u; i < changes.size(); ++i)
    {
        auto& change = changes[i];
        auto& record = m_records[change.index];
        if (change.replace_selection)
        {
            const auto old = record.declaration;
            if ((old && !m_document.detach(old)) ||
                (change.declaration && !CDocumentConstruction::append(m_document, record.entry, change.declaration)))
            {
                disable();
                cleanup();
                diagnostic.reason = EInstanceLoadReason::invalid_input;
                return false;
            }
            record.declaration = change.declaration;
            change.declaration = {};
            erase_detached(m_document, old);
        }
    }
    for (std::size_t i = 0u; i < changes.size(); ++i)
    {
        const auto& change = changes[i];
        const auto& record = m_records[change.index];
        if (record.extent && (change.index == index || policy == EInstanceDescendantPolicy::propagate))
        {
            std::memcpy(m_payload.data() + record.offset, staged.data() + change.offset, record.extent);
        }
    }
    return true;
}

bool CLiveInstances::overwrite_record(const std::uint32_t index, const CByteConstView& complete,
    const EInstanceDescendantPolicy policy, SInstanceDiagnostic& diagnostic) noexcept
{
    const SRecord& record = m_records[index];
    SType layout;
    if (!valid_policy(policy) || !m_binding.resolved()->type(record.type, layout) ||
        (record.extent && (!complete.is_ready() || complete.size() < record.extent ||
            (reinterpret_cast<std::uintptr_t>(complete.data()) & (layout.alignment - 1u)))))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_range;
        return false;
    }
    CByteBuffer defaults;
    const std::uint8_t* inherited{};
    if (!baseline(index, defaults, inherited, diagnostic)) return false;
    const CDocumentRead document{ m_document };
    detail::SValueDiagnostic error;
    const auto mask = detail::extend_selection(m_document, *m_binding.resolved(), record.type,
        { &document, SOccurrence{ record.declaration } }, SSelectionView::complete(), inherited, complete.data(), false,
        CStringView{ "declaration" }, error);
    if (error.reason != EReason::none) return report_value(error, diagnostic);
    return edit_existing(index, mask, complete, {}, true, policy, diagnostic);
}

bool CLiveInstances::overwrite(const CInstanceHandle instance, const CByteConstView& complete,
    const EInstanceDescendantPolicy policy, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    std::uint32_t index{};
    if (pending_edit() || !record_index(instance, index))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    return overwrite_record(index, complete, policy, diagnostic);
}

bool CLiveInstances::specialise(const CInstanceHandle instance, const SInstanceSelectionStep* steps,
    const std::size_t step_count, const CInstanceDocumentQuery& source, const CInstanceHandle value,
    const EInstanceDescendantPolicy policy, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    std::uint32_t index{};
    const auto supplied = detail::SInstanceHandleAccess::occurrence(value);
    if (pending_edit() || !valid_policy(policy) || !record_index(instance, index) || (step_count && !steps) || !source.m_query.contains(supplied))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    const auto& record = m_records[index];
    const auto& types = *m_binding.resolved();
    TPodVector<SResolvedSelectionStep> resolved;
    if (!resolve_steps(types, record.type, steps, step_count, resolved, diagnostic.reason)) return false;
    const auto leaf = document_translation::detail::copy_valid_subtree(m_document, source.m_query, supplied, {});
    const auto patch = wrap_patch(m_document, types, record.type, resolved, leaf);
    if (!patch)
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const CDocumentRead document{ m_document };
    detail::SValueDiagnostic error;
    CByteBuffer encoded;
    CNodeKey scope, mask;
    const auto cleanup = [&]() noexcept
    {
        erase_detached(m_document, patch);
        erase_detached(m_document, scope);
        erase_detached(m_document, mask);
    };
    if (!allocate_bytes(encoded, record.extent))
    {
        cleanup();
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (!detail::construct_alternative(types, document, record.type, SOccurrence{ patch },
        record.extent ? m_payload.data() + record.offset : nullptr, record.extent, encoded.data(), encoded.size(), error))
    {
        cleanup();
        (void)report_value(error, diagnostic);
        diagnostic.occurrence = value;
        return false;
    }
    scope = detail::strip_selection(m_document, types, record.type, { &document, SOccurrence{ patch } }, {}, error);
    if (error.reason == EReason::none)
    {
        mask = detail::extend_selection(m_document, types, record.type, { &document, SOccurrence{ record.declaration } },
            { &document, SOccurrence{ scope } }, nullptr, nullptr, true, CStringView{ "declaration" }, error);
    }
    if (error.reason != EReason::none)
    {
        cleanup();
        (void)report_value(error, diagnostic);
        diagnostic.occurrence = value;
        return false;
    }
    const bool changed = edit_existing(index, mask, encoded.const_view(), scope, false, policy, diagnostic);
    cleanup();
    return changed;
}

bool CLiveInstances::specialise(const CInstanceHandle instance, const SInstanceSelectionStep* steps,
    const std::size_t step_count, const CByteConstView& complete_component,
    const EInstanceDescendantPolicy policy, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    std::uint32_t index{};
    if (pending_edit() || !valid_policy(policy) || !record_index(instance, index) || (step_count && !steps))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    const auto& record = m_records[index];
    const auto& types = *m_binding.resolved();
    TPodVector<SResolvedSelectionStep> resolved;
    if (!resolve_steps(types, record.type, steps, step_count, resolved, diagnostic.reason)) return false;
    CTypesIndex selected_type = record.type;
    SType layout;
    SField field;
    bool bit_field{};
    std::uint64_t offset{};
    std::size_t word_size{};
    for (std::size_t i = 0u; i < resolved.size(); ++i)
    {
        (void)types.type(selected_type, layout);
        const auto& step = resolved[i];
        if (layout.category == ECategory::array) offset += layout.stride * step.ordinal;
        else if (layout.category == ECategory::structure)
        {
            SMember member;
            (void)types.member(types.member_at(selected_type, step.ordinal), member);
            offset += member.offset;
        }
        else
        {
            bit_field = true;
            word_size = static_cast<std::size_t>(layout.size);
            (void)types.field(types.field_at(selected_type, step.ordinal), field);
        }
        selected_type = step.child_type;
    }
    (void)types.type(selected_type, layout);
    if (layout.size && (!complete_component.is_ready() || complete_component.size() < layout.size ||
        (reinterpret_cast<std::uintptr_t>(complete_component.data()) & (layout.alignment - 1u))))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_range;
        return false;
    }
    std::uint64_t field_bits{};
    if (bit_field)
    {
        field_bits = read_scalar_bits(complete_component.data(), static_cast<std::size_t>(layout.size));
        const unsigned storage_width = static_cast<unsigned>(layout.size * 8u);
        const auto low_mask = field.width == 64u ? UINT64_MAX : ((UINT64_C(1) << field.width) - 1u);
        const auto storage_mask = storage_width == 64u ? UINT64_MAX : ((UINT64_C(1) << storage_width) - 1u);
        const bool negative = field.signed_value && ((field_bits >> (field.width - 1u)) & 1u);
        const auto expected_high = negative ? (storage_mask & ~low_mask) : 0u;
        if ((field_bits & ~low_mask) != expected_high)
        {
            diagnostic.reason = EInstanceLoadReason::invalid_range;
            return false;
        }
    }
    CByteBuffer encoded;
    if (!allocate_bytes(encoded, record.extent))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (record.extent) std::memcpy(encoded.data(), m_payload.data() + record.offset, record.extent);
    detail::SValueDiagnostic error;
    if (bit_field)
    {
        const auto word = read_scalar_bits(encoded.data() + offset, word_size);
        write_scalar_bits(encoded.data() + offset, word_size, (word & ~field.mask) | ((field_bits << field.shift) & field.mask));
    }
    else if (!detail::inherit_selection(types, selected_type, {}, SSelectionView::complete(), complete_component.data(),
        encoded.data() ? encoded.data() + offset : nullptr, error))
    {
        (void)report_value(error, diagnostic);
        diagnostic.occurrence = instance;
        return false;
    }
    const auto leaf = detail::copy_selection(m_document, types, selected_type, SSelectionView::complete(), {}, error);
    const auto scope = wrap_patch(m_document, types, record.type, resolved, leaf);
    if (!scope)
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const CDocumentRead document{ m_document };
    const auto mask = detail::extend_selection(m_document, types, record.type, { &document, SOccurrence{ record.declaration } },
        { &document, SOccurrence{ scope } }, nullptr, nullptr, true, CStringView{ "declaration" }, error);
    if (error.reason != EReason::none)
    {
        erase_detached(m_document, scope);
        (void)report_value(error, diagnostic);
        diagnostic.occurrence = instance;
        return false;
    }
    const bool changed = edit_existing(index, mask, encoded.const_view(), scope, false, policy, diagnostic);
    erase_detached(m_document, scope);
    erase_detached(m_document, mask);
    return changed;
}

bool CLiveInstances::remove_selection(const CInstanceHandle instance, const SInstanceSelectionStep* steps,
    const std::size_t step_count, const EInstanceDescendantPolicy policy, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    std::uint32_t index{};
    if (pending_edit() || !valid_policy(policy) || !record_index(instance, index) || (step_count && !steps))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    const auto& record = m_records[index];
    const auto& types = *m_binding.resolved();
    TPodVector<SResolvedSelectionStep> resolved;
    if (!resolve_steps(types, record.type, steps, step_count, resolved, diagnostic.reason)) return false;
    const CDocumentRead document{ m_document };
    detail::SValueDiagnostic error;
    CNodeKey mask = detail::copy_selection(m_document, types, record.type, { &document, SOccurrence{ record.declaration } },
        CStringView{ "declaration" }, error);
    if (error.reason != EReason::none) return report_value(error, diagnostic);
    const auto removed = selected_path(m_document, mask, resolved);
    if (!removed)
    {
        erase_detached(m_document, mask);
        return true;
    }
    CNodeKey scope = document_translation::detail::copy_valid_subtree(m_document, document, SOccurrence{ removed }, {});
    scope = wrap_patch(m_document, types, record.type, resolved, scope);
    const auto cleanup = [&]() noexcept
    {
        erase_detached(m_document, scope);
        erase_detached(m_document, mask);
    };
    if (!scope)
    {
        cleanup();
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (!step_count)
    {
        (void)m_document.erase(mask);
        mask = {};
    }
    else if (resolved[step_count - 1u].kind == EInstanceSelectionStepKind::element)
    {
        const auto old = m_document.detach_payload(removed);
        const auto hole = m_document.create_null();
        if (!old || !hole || !m_document.attach_payload(removed, hole))
        {
            erase_detached(m_document, old);
            erase_detached(m_document, hole);
            cleanup();
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
        (void)m_document.erase(old);
    }
    else (void)m_document.erase(removed);
    CByteBuffer encoded, defaults;
    const std::uint8_t* inherited{};
    if (!allocate_bytes(encoded, record.extent) || !baseline(index, defaults, inherited, diagnostic))
    {
        cleanup();
        if (diagnostic.reason == EInstanceLoadReason::none) diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (record.extent) std::memcpy(encoded.data(), m_payload.data() + record.offset, record.extent);
    if (!detail::inherit_selection(types, record.type, {}, { &document, SOccurrence{ scope } }, inherited, encoded.data(), error))
    {
        cleanup();
        (void)report_value(error, diagnostic);
        diagnostic.occurrence = instance;
        return false;
    }
    const bool changed = edit_existing(index, mask, encoded.const_view(), scope, false, policy, diagnostic);
    cleanup();
    return changed;
}

bool CLiveInstances::begin_edit(const CInstanceHandle instance, CInstanceEditToken& token,
    SMutableInstanceEntryView& value, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    std::uint32_t index{};
    SMutableInstanceEntryView staged;
    if (pending_edit() || !record_index(instance, index) || !mutable_entry(instance, staged))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    m_edit_generation = next_edit_serial();
    if (!m_edit_generation)
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    m_pending = index;
    value = staged;
    token.m_owner = this;
    token.m_generation = m_edit_generation;
    return true;
}

bool CLiveInstances::complete_edit(const CInstanceEditToken& token, const EInstanceDescendantPolicy policy,
    SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!pending_edit() || token.m_owner != this || token.m_generation != m_edit_generation || !m_loaded || !m_binding.is_usable())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    const auto& record = m_records[m_pending];
    const auto bytes = record.extent ? m_payload.const_view().subview(record.offset, record.extent) : CByteConstView{};
    if (!overwrite_record(m_pending, bytes, policy, diagnostic)) return false;
    m_pending = k_no_parent;
    return true;
}

CInstanceHandle CLiveInstances::capture_base(const CStringView& type, const CStringView& name,
    const CByteConstView& complete, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const auto* types = m_binding.resolved();
    const auto index = types ? types->find_type(type) : CTypesIndex{};
    SType layout;
    if (!loaded_ready() || !types || !types->type(index, layout) || complete.size() != layout.size ||
        (layout.size && (!complete.is_ready() || (reinterpret_cast<std::uintptr_t>(complete.data()) & (layout.alignment - 1u)))))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return {};
    }
    CByteBuffer defaults;
    if (layout.size && (!defaults.allocate(static_cast<std::size_t>(layout.size), k_max_alignment) ||
        !defaults.set_size(static_cast<std::size_t>(layout.size))))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    const CDocumentRead document{ m_document };
    detail::SValueDiagnostic error;
    if (!detail::construct_value(*types, document, index, {}, defaults.data(), defaults.size(), detail::EConstructionMode::instance, error))
    {
        diagnostic.reason = detail::value_failure(error, EInstanceLoadReason::invalid_declaration);
        return {};
    }
    CLiveDocument selections;
    if (!selections.initialise())
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    const auto mask = detail::extend_selection(selections, *types, index, {}, SSelectionView::complete(), defaults.data(),
        complete.data(), false, CStringView{ "declaration" }, error);
    if (error.reason != EReason::none)
    {
        diagnostic.reason = detail::value_failure(error, EInstanceLoadReason::invalid_declaration);
        return {};
    }
    return append_instance(index, k_no_parent, type, name, CDocumentRead{ selections }, SOccurrence{ mask }, complete, diagnostic, true);
}

}   // namespace schema
