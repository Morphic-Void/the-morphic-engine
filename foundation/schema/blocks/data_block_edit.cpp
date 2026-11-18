
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_edit.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Clone candidate state per command; failed commands retain the prior candidate.

#include "schema/data_block_edit.hpp"
#include "schema/operations/data_operation_internal.hpp"
#include "schema/operations/operation_meter.hpp"
#include "schema/blocks/data_copy_internal.hpp"
#include "schema/document/document_construction.hpp"
#include "schema/document/data_document_validation.hpp"
#include "schema/schema.hpp"

#include <atomic>
#include <cstring>

namespace schema::detail
{

struct SDataEditOwned
{
    //  Candidate bindings retire before the catalogue they borrow.
    CLiveTypes types;
    TInstance<CDataCandidate> candidate;
};

struct SDataEditState
{
    explicit SDataEditState(const SDataOperationLimits limits) noexcept :
        operation(limits), owned(operation.memory_context()) {}
    //  The stable session operation outlives all metadata allocated through it.
    CDataOperation operation;
    TInstance<SDataEditOwned> owned;

    [[nodiscard]] bool initialise() noexcept
    {
        const CDataOperationScope scope{ operation };
        return owned.emplace() && owned->candidate.emplace(operation, owned->types);
    }

    template<class FCommand>
    [[nodiscard]] bool command(const FCommand& apply, SDataOperationDiagnostic& diagnostic) noexcept
    {
        diagnostic = {};
        operation.command();
        operation.stage(EDataOperationStage::editing);
        const CDataOperationScope scope{ operation };
        TInstance<CDataCandidate> staged = TInstance<CDataCandidate>::create(operation, owned->types);
        if (!staged || !owned->candidate->clone(*staged) || !apply(*staged) || !data_healthy())
        {
            operation.diagnose(diagnostic);
            if (diagnostic.reason == EDataOperationReason::none) diagnostic.reason = EDataOperationReason::allocation_failed;
            return false;
        }
        owned->candidate = std::move(staged);
        return true;
    }
};

static bool allocated(CDataCandidate& candidate, CByteBuffer& bytes, const std::uint64_t size) noexcept
{
    if (size > memory::k_byte_size_ceiling)
    {
        return candidate.operation.fail(EDataOperationReason::invalid_input);
    }
    return !size || (bytes.allocate(static_cast<std::size_t>(size), k_max_alignment) && bytes.set_size(static_cast<std::size_t>(size))) ||
        candidate.operation.fail(EDataOperationReason::allocation_failed);
}

static bool set_selection(CDataCandidate& candidate, const std::uint32_t index, const CNodeKey selection) noexcept
{
    SDataCandidateEntry& entry = candidate.entries[index];
    if ((entry.selection && !candidate.document.erase(entry.selection)) ||
        (selection && !candidate.document.append_child(entry.metadata, selection).succeeded()))
    {
        return candidate.operation.fail(EDataOperationReason::invalid_input);
    }
    entry.selection = selection;
    return true;
}

static bool create_records(CDataCandidate& candidate, const std::uint32_t index,
    const CLiveDocument& input, const CNodeKey records) noexcept
{
    if ((input.value_type(records) != ELiveValueType::array) || !input.child_count(records))
    {
        return candidate.operation.fail(EDataOperationReason::invalid_input);
    }
    const CTypesIndex type = candidate.entries[index].type;
    SType layout;
    if (!candidate.types.resolved()->type(type, layout))
    {
        return candidate.operation.fail(EDataOperationReason::invalid_input);
    }
    const std::uint32_t count = input.child_count(records);
    std::uint64_t extent{};
    if (!data_extent(layout, count, extent) || !candidate.check_records(index, count) ||
        !candidate.admitted_nodes(CDocumentRead{ input }, SOccurrence{ records })) return false;
    CByteBuffer bytes;
    if (!allocated(candidate, bytes, extent)) return false;
    if (extent) std::memset(bytes.data(), 0, static_cast<std::size_t>(extent));
    const SReferenceEncoding references = candidate.encoding();
    SValueDiagnostic error;
    std::uint32_t ordinal{};
    for (CNodeKey record = input.first_child(records); record; record = input.next_sibling(record))
    {
        candidate.operation.location(candidate.document.name(candidate.entries[index].metadata), ordinal);
        if (!construct_value(candidate.operation.value_work(), *candidate.types.resolved(), CDocumentRead{ input }, type,
            SOccurrence{ record }, (layout.size ? bytes.data() + (layout.size * ordinal) : nullptr), static_cast<std::size_t>(layout.size),
            EConstructionMode::complete_bulk_data, error, input.is_object_entry(record), &references)) return candidate.error(error.reason);
        ++ordinal;
    }
    candidate.entries[index].bytes = std::move(bytes);
    candidate.entries[index].count = count;
    return true;
}

static CNodeKey path_patch(CDataCandidate& candidate, const CTypesIndex root,
    const SInstanceSelectionStep* const steps, const std::size_t count, const CLiveDocument& input, const CNodeKey value) noexcept
{
    if ((count && !steps) || !input.contains(value) ||
        !candidate.operation.depth(static_cast<std::uint32_t>((count > UINT32_MAX) ? UINT32_MAX : count)))
    {
        (void)candidate.operation.fail(EDataOperationReason::invalid_input);
        return {};
    }
    struct SStep { bool array; CStringView name; std::uint32_t ordinal; };
    TPodVector<SStep> resolved;
    CTypesIndex current = root;
    for (std::size_t depth = 0u; depth < count; ++depth)
    {
        if (!candidate.operation.work()) return {};
        SType layout;
        if (!candidate.types.resolved()->type(current, layout))
        {
            return {};
        }
        SStep step{};
        if ((layout.category == ECategory::array) && (steps[depth].kind == EInstanceSelectionStepKind::element) &&
            (steps[depth].element < layout.count))
        {
            step.array = true;
            step.ordinal = steps[depth].element;
            current = layout.element_or_storage;
        }
        else if ((layout.category == ECategory::structure) && (steps[depth].kind == EInstanceSelectionStepKind::member))
        {
            SMember member;
            if (!candidate.types.resolved()->member(candidate.types.resolved()->find_member(current, steps[depth].member), member))
            {
                return {};
            }
            step.name = candidate.types.resolved()->name(member.name);
            current = member.type;
        }
        else if ((layout.category == ECategory::bit_structure) && (steps[depth].kind == EInstanceSelectionStepKind::member))
        {
            SField field;
            if (!candidate.types.resolved()->field(candidate.types.resolved()->find_field(current, steps[depth].member), field))
            {
                return {};
            }
            step.name = candidate.types.resolved()->name(field.name);
            current = field.type;
        }
        else return {};
        if (!resolved.push_back(step)) return {};
    }
    CNodeKey patch = candidate.copy_node(CDocumentRead{ input }, SOccurrence{ value });
    for (std::size_t depth = count; patch && depth; --depth)
    {
        const SStep& step = resolved[depth - 1u];
        if (!candidate.operation.nodes(static_cast<std::uint64_t>(step.ordinal) + 1u))
        {
            return {};
        }
        const CNodeKey parent = step.array ? candidate.document.create_array() : candidate.document.create_object();
        if (!parent) return {};
        for (std::uint32_t hole = 0u; hole < step.ordinal; ++hole)
        {
            if (!candidate.document.append_child(parent, candidate.document.create_null()).succeeded())
            {
                return {};
            }
        }
        const bool named = step.array ? candidate.document.remove_name(patch) : candidate.document.set_name(patch, step.name);
        if (!named || !candidate.document.append_child(parent, patch).succeeded())
        {
            return {};
        }
        patch = parent;
    }
    return patch;
}

static bool write_record(CDataCandidate& candidate, const std::uint32_t index, const std::uint32_t record,
    const SInstanceSelectionStep* const path, const std::size_t path_count, const CLiveDocument& input,
    const CNodeKey value, const EInstanceDescendantPolicy policy) noexcept
{
    if ((policy != EInstanceDescendantPolicy::preserve) && (policy != EInstanceDescendantPolicy::propagate))
    {
        return candidate.operation.fail(EDataOperationReason::invalid_input);
    }
    const CTypesIndex type = candidate.entries[index].type;
    const bool instance = candidate.entries[index].kind == EDataEntryKind::instance;
    if (record >= candidate.entries[index].count)
    {
        return candidate.operation.fail(EDataOperationReason::invalid_input);
    }
    SType layout;
    if (!candidate.types.resolved()->type(type, layout))
    {
        return candidate.operation.fail(EDataOperationReason::invalid_input);
    }
    const CNodeKey patch = path_patch(candidate, type, path, path_count, input, value);
    if (!patch)
    {
        return candidate.operation.fail(EDataOperationReason::invalid_input);
    }
    const CDocumentRead document{ candidate.document };
    const SReferenceEncoding references = candidate.encoding();
    CByteBuffer changed;
    if (!allocated(candidate, changed, layout.size))
    {
        return false;
    }
    SValueDiagnostic error;
    const std::uint8_t* const before = layout.size ? candidate.entries[index].bytes.data() + (layout.size * record) : nullptr;
    if (!construct_alternative(candidate.operation.value_work(), *candidate.types.resolved(), document, type,
        SOccurrence{ patch }, before, static_cast<std::size_t>(layout.size), changed.data(), changed.size(), error, &references))
    {
        return candidate.error(error.reason);
    }
    const CNodeKey scope = strip_selection(candidate.document, *candidate.types.resolved(), type,
        { &document, SOccurrence{ patch } }, {}, error);
    if (error.reason != EReason::none)
    {
        return candidate.error(error.reason);
    }
    if (instance)
    {
        const CNodeKey selection = extend_selection(candidate.document, *candidate.types.resolved(), type,
            { &document, SOccurrence{ candidate.entries[index].selection } }, { &document, SOccurrence{ scope } },
            nullptr, nullptr, true, CStringView{ "declaration" }, error);
        if ((error.reason != EReason::none) || !set_selection(candidate, index, selection))
        {
            return candidate.error(error.reason);
        }
    }
    if (!candidate.operation.bytes_work(layout.size))
    {
        return false;
    }
    if (layout.size) std::memcpy((candidate.entries[index].bytes.data() + (layout.size * record)), changed.data(), changed.size());
    if (!instance) return true;
    TPodVector<std::uint8_t> changed_entries;
    if (!changed_entries.resize(candidate.entries.size()))
    {
        return candidate.operation.fail(EDataOperationReason::allocation_failed);
    }
    std::memset(changed_entries.data(), 0, changed_entries.size());
    changed_entries[index] = 1u;
    for (std::uint32_t child = index + 1u; child < candidate.entries.size(); ++child)
    {
        if (!candidate.operation.work()) return false;
        SDataCandidateEntry& entry = candidate.entries[child];
        if (!entry.alive || (entry.kind != EDataEntryKind::instance) || (entry.parent == UINT32_MAX) ||
            (entry.parent >= child) || !changed_entries[entry.parent])
        {
            continue;
        }
        const std::uint8_t* const parent = candidate.entries[entry.parent].bytes.data();
        if (policy == EInstanceDescendantPolicy::preserve)
        {
            const CNodeKey selection = extend_selection(candidate.document, *candidate.types.resolved(), type,
                { &document, SOccurrence{ entry.selection } }, { &document, SOccurrence{ scope } }, parent,
                entry.bytes.data(), false, CStringView{ "declaration" }, error);
            if ((error.reason != EReason::none) || !set_selection(candidate, child, selection))
            {
                return candidate.error(error.reason);
            }
        }
        else
        {
            if (!inherit_selection(*candidate.types.resolved(), type, { &document, SOccurrence{ entry.selection } },
                { &document, SOccurrence{ scope } }, parent, entry.bytes.data(), error)) return candidate.error(error.reason);
            changed_entries[child] = 1u;
        }
    }
    return true;
}

}   // namespace schema::detail

namespace schema
{

static std::atomic<std::uint64_t> s_data_edit_generation{};

CDataBlockEdit::~CDataBlockEdit() noexcept { cancel(); }

bool CDataBlockEdit::start(const SDataOperationLimits limits, SDataOperationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (m_state)
    {
        diagnostic.reason = EDataOperationReason::destination_not_empty;
        return false;
    }
    std::uint64_t generation = s_data_edit_generation.load(std::memory_order_relaxed);
    do
    {
        if (generation == UINT64_MAX)
        {
            diagnostic.reason = EDataOperationReason::invalid_input;
            return false;
        }
    } while (!s_data_edit_generation.compare_exchange_weak(generation, (generation + 1u), std::memory_order_relaxed));
    m_generation = generation + 1u;
    memory::CMemoryContext* const parent = memory::get_ambient_memory_context();
    const std::size_t size = parent ? parent->condition_bytes(alignof(detail::SDataEditState), sizeof(detail::SDataEditState)) : 0u;
    if (!size || (size > limits.owned_bytes))
    {
        diagnostic.reason = EDataOperationReason::limit_exceeded;
        diagnostic.limit = EDataOperationLimit::owned_bytes;
        diagnostic.requested = size;
        diagnostic.allowed = limits.owned_bytes;
        return false;
    }
    void* const storage = parent->allocate(alignof(detail::SDataEditState), size);
    if (!storage)
    {
        diagnostic.reason = EDataOperationReason::allocation_failed;
        return false;
    }
    m_state = new (storage) detail::SDataEditState(limits);
    (void)m_state->operation.reserve_external(size);
    const bool valid_limits = m_state->operation.valid() || m_state->operation.fail(EDataOperationReason::invalid_input);
    if (!valid_limits || !m_state->initialise())
    {
        m_state->operation.diagnose(diagnostic);
        cancel();
        return false;
    }
    return true;
}

bool CDataBlockEdit::begin(const CDataBlockView& source, const SDataOperationLimits limits,
    SDataOperationDiagnostic& diagnostic) noexcept
{
    if (!start(limits, diagnostic)) return false;
    bool success{};
    {
        const detail::CDataOperationScope scope{ m_state->operation };
        const CDataBlockView* const sources[]{ &source };
        detail::CDataCopy copy{ m_state->operation };
        success = copy.prepare(sources, 1u, nullptr, 0u, m_state->owned->types) &&
            m_state->owned->candidate->initialise() && copy.all(*m_state->owned->candidate);
    }
    if (!success)
    {
        m_state->operation.diagnose(diagnostic);
        cancel();
    }
    return success;
}

bool CDataBlockEdit::begin_empty(const CTypesDocumentQuery& types, const SDataOperationLimits limits,
    SDataOperationDiagnostic& diagnostic) noexcept
{
    if (!start(limits, diagnostic)) return false;
    bool success{};
    {
        const detail::CDataOperationScope scope{ m_state->operation };
        const detail::CDocumentRead& read = detail::SDocumentQueryAccess::read(types);
        const detail::SOccurrence root = read.object_child(read.root(), CStringView{ "types" });
        const detail::SDocumentSection section{ &read, root };
        detail::SDocumentBuildDiagnostic error;
        CLiveDocument copied;
        SDiagnostic type_error;
        success = root && m_state->owned->candidate->admitted_nodes(read, root) &&
            detail::copy_document_sections(&section, 1u, copied, error) && m_state->owned->types.try_adopt(std::move(copied)) &&
            m_state->owned->types.resolve(type_error) && m_state->owned->candidate->initialise();
    }
    if (!success)
    {
        (void)m_state->operation.fail(EDataOperationReason::invalid_input);
        m_state->operation.diagnose(diagnostic);
        cancel();
    }
    return success;
}

bool CDataBlockEdit::contains(const CDataEditHandle handle) const noexcept
{
    return m_state && (handle.m_owner == this) && (handle.m_generation == m_generation) &&
        (handle.m_index < m_state->owned->candidate->entries.size()) && m_state->owned->candidate->entries[handle.m_index].alive;
}

CDataEditHandle CDataBlockEdit::handle(const std::uint32_t index) const noexcept
{
    CDataEditHandle result;
    result.m_owner = this;
    result.m_generation = m_generation;
    result.m_index = index;
    return result;
}

CDataEditHandle CDataBlockEdit::find(const CStringView& type, const EDataEntryKind kind, const CStringView& name,
    const CDataEditHandle parent) noexcept
{
    if (!m_state || (parent && !contains(parent)))
    {
        return {};
    }
    detail::CDataCandidate& candidate = *m_state->owned->candidate;
    const std::uint32_t index = candidate.find(candidate.types.resolved()->find_type(type), kind, name, (parent ? parent.m_index : UINT32_MAX));
    return ((index != UINT32_MAX) && candidate.entries[index].alive) ? handle(index) : CDataEditHandle{};
}

CDataEditHandle CDataBlockEdit::create_instance(const CStringView& type, const CStringView& name,
    const CLiveDocument& input, const CNodeKey declaration, const CDataEditHandle parent,
    SDataOperationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!m_state || (parent && !contains(parent)))
    {
        diagnostic.reason = EDataOperationReason::stale_handle;
        return {};
    }
    std::uint32_t index{};
    const bool success = m_state->command([&](detail::CDataCandidate& candidate) noexcept
    {
        const CTypesIndex resolved = candidate.types.resolved()->find_type(type);
        if (parent && ((candidate.entries[parent.m_index].type != resolved) ||
            (candidate.entries[parent.m_index].kind != EDataEntryKind::instance)))
        {
            return candidate.operation.fail(EDataOperationReason::type_mismatch);
        }
        if (!candidate.append(resolved, EDataEntryKind::instance, name, (parent ? parent.m_index : UINT32_MAX), 1u, index))
        {
            return false;
        }
        SType layout;
        if (!candidate.types.resolved()->type(resolved, layout))
        {
            return candidate.operation.fail(EDataOperationReason::invalid_input);
        }
        CByteBuffer bytes;
        const CLiveDocument& effective_input = declaration ? input : candidate.document;
        if (!detail::allocated(candidate, bytes, layout.size) || !candidate.admitted_nodes(detail::CDocumentRead{ effective_input }, detail::SOccurrence{ declaration }))
        {
            return false;
        }
        const detail::SReferenceEncoding references = candidate.encoding();
        detail::SValueDiagnostic error;
        const bool built = parent ? detail::construct_alternative(candidate.operation.value_work(), *candidate.types.resolved(),
            detail::CDocumentRead{ effective_input }, resolved, detail::SOccurrence{ declaration }, candidate.entries[parent.m_index].bytes.data(),
            static_cast<std::size_t>(layout.size), bytes.data(), bytes.size(), error, &references) :
            detail::construct_value(candidate.operation.value_work(), *candidate.types.resolved(), detail::CDocumentRead{ effective_input },
                resolved, detail::SOccurrence{ declaration }, bytes.data(), bytes.size(), detail::EConstructionMode::instance, error, false, &references);
        if (!built) return candidate.error(error.reason);
        candidate.entries[index].bytes = std::move(bytes);
        if (declaration)
        {
            const detail::CDocumentRead read{ input };
            const CNodeKey selection = detail::strip_selection(candidate.document, *candidate.types.resolved(), resolved,
                { &read, detail::SOccurrence{ declaration } }, CStringView{ "declaration" }, error);
            if ((error.reason != EReason::none) || !detail::set_selection(candidate, index, selection))
            {
                return candidate.error(error.reason);
            }
        }
        return true;
    }, diagnostic);
    return success ? handle(index) : CDataEditHandle{};
}

CDataEditHandle CDataBlockEdit::create_collection(const CStringView& type, const CStringView& name,
    const CLiveDocument& input, const CNodeKey records, SDataOperationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!m_state)
    {
        diagnostic.reason = EDataOperationReason::invalid_input;
        return {};
    }
    std::uint32_t index{};
    const bool success = m_state->command([&](detail::CDataCandidate& candidate) noexcept
    {
        return candidate.append(candidate.types.resolved()->find_type(type), EDataEntryKind::collection, name, UINT32_MAX, 0u, index) &&
            detail::create_records(candidate, index, input, records);
    }, diagnostic);
    return success ? handle(index) : CDataEditHandle{};
}

bool CDataBlockEdit::replace_collection(const CDataEditHandle target, const CLiveDocument& input,
    const CNodeKey records, SDataOperationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!contains(target))
    {
        diagnostic.reason = EDataOperationReason::stale_handle;
        return false;
    }
    return m_state->command([&](detail::CDataCandidate& candidate) noexcept
    {
        return ((candidate.entries[target.m_index].kind == EDataEntryKind::collection) && detail::create_records(candidate, target.m_index, input, records)) ||
            candidate.operation.fail(EDataOperationReason::invalid_input);
    }, diagnostic);
}

bool CDataBlockEdit::write(const CDataEditHandle target, const std::uint32_t record,
    const SInstanceSelectionStep* const path, const std::size_t path_count, const CLiveDocument& input,
    const CNodeKey value, const EInstanceDescendantPolicy policy, SDataOperationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!contains(target))
    {
        diagnostic.reason = EDataOperationReason::stale_handle;
        return false;
    }
    return m_state->command([&](detail::CDataCandidate& candidate) noexcept
    {
        return detail::write_record(candidate, target.m_index, record, path, path_count, input, value, policy);
    }, diagnostic);
}

bool CDataBlockEdit::rename(const CDataEditHandle target, const CStringView& name, SDataOperationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!contains(target))
    {
        diagnostic.reason = EDataOperationReason::stale_handle;
        return false;
    }
    return m_state->command([&](detail::CDataCandidate& candidate) noexcept
    {
        const detail::SDataCandidateEntry& entry = candidate.entries[target.m_index];
        const std::uint32_t found = candidate.find(entry.type, entry.kind, name, entry.parent);
        if (!name.length() || ((found != UINT32_MAX) && (found != target.m_index)))
        {
            return candidate.operation.fail(EDataOperationReason::naming_conflict);
        }
        return candidate.operation.bytes_work(name.length()) && (candidate.document.set_name(entry.metadata, name) ||
            candidate.operation.fail(EDataOperationReason::allocation_failed));
    }, diagnostic);
}

bool CDataBlockEdit::erase(const CDataEditHandle target, SDataOperationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!contains(target))
    {
        diagnostic.reason = EDataOperationReason::stale_handle;
        return false;
    }
    return m_state->command([&](detail::CDataCandidate& candidate) noexcept
    {
        candidate.entries[target.m_index].alive = false;
        candidate.entries[target.m_index].bytes = {};
        return true;
    }, diagnostic);
}

bool CDataBlockEdit::commit(CBakedDataBlock& output, SDataOperationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!m_state)
    {
        diagnostic.reason = EDataOperationReason::invalid_input;
        return false;
    }
    m_state->operation.command();
    bool success{};
    {
        const detail::CDataOperationScope scope{ m_state->operation };
        success = m_state->owned->candidate->publish(output);
    }
    if (success) cancel();
    else m_state->operation.diagnose(diagnostic);
    return success;
}

void CDataBlockEdit::cancel() noexcept
{
    if (!m_state) return;
    memory::CMemoryContext* const parent = m_state->operation.parent_context();
    const std::size_t size = parent->condition_bytes(alignof(detail::SDataEditState), sizeof(detail::SDataEditState));
    m_state->operation.release_external(size);
    m_state->~SDataEditState();
    parent->deallocate(alignof(detail::SDataEditState), size, m_state);
    m_state = nullptr;
}

}   // namespace schema
