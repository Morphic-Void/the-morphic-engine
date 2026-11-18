
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_view.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Checked borrowed entry, field, text and typed-range observations.

#include "schema/blocks/data_reference_context.hpp"
#include "schema/operations/data_operation_internal.hpp"
#include "containers/TInstance.hpp"
#include "memory/memory_policies.hpp"

#include <atomic>
#include <utility>

namespace schema::detail
{

struct SDataViewOwner
{
    explicit SDataViewOwner(const SDataOperationLimits limits) noexcept :
        operation(limits), metadata(operation.memory_context()) {}

    //  The operation remains at a stable address until all metadata is gone.
    //  Reverse member destruction preserves its allocator through that teardown.
    CDataOperation operation;
    TInstance<SDataViewState> metadata;
};

}   // namespace schema::detail

namespace schema
{

static std::atomic<std::uint64_t> s_view_generation{ 1u };

static void destroy_view_owner(detail::SDataViewOwner* const owner) noexcept
{
    if (!owner) return;
    memory::CMemoryContext* const parent = owner->operation.parent_context();
    const std::size_t size = parent->condition_bytes(alignof(detail::SDataViewOwner), sizeof(detail::SDataViewOwner));
    owner->operation.release_external(size);
    owner->~SDataViewOwner();
    parent->deallocate(alignof(detail::SDataViewOwner), size, owner);
}

class CDataViewOwnerGuard
{
public:
    explicit CDataViewOwnerGuard(detail::SDataViewOwner* const owner) noexcept : m_owner(owner) {}
    ~CDataViewOwnerGuard() noexcept { destroy_view_owner(m_owner); }
    CDataViewOwnerGuard(const CDataViewOwnerGuard&) = delete;
    CDataViewOwnerGuard& operator=(const CDataViewOwnerGuard&) = delete;

    [[nodiscard]] detail::SDataViewOwner* release() noexcept
    {
        detail::SDataViewOwner* const owner = m_owner;
        m_owner = nullptr;
        return owner;
    }

private:
    detail::SDataViewOwner* m_owner;
};

CDataBlockView::~CDataBlockView() noexcept { clear(); }

bool CDataBlockView::bind(const CBakedDataBlock& source, const SDataOperationLimits limits,
    SDataOperationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (m_owner)
    {
        diagnostic.reason = EDataOperationReason::destination_not_empty;
        return false;
    }
    memory::CMemoryContext* const parent = memory::get_ambient_memory_context();
    const std::size_t size = parent ? parent->condition_bytes(alignof(detail::SDataViewOwner), sizeof(detail::SDataViewOwner)) : 0u;
    if (!size || (size > limits.owned_bytes))
    {
        diagnostic.reason = EDataOperationReason::limit_exceeded;
        diagnostic.limit = EDataOperationLimit::owned_bytes;
        diagnostic.requested = size;
        diagnostic.allowed = limits.owned_bytes;
        return false;
    }
    void* const storage = parent->allocate(alignof(detail::SDataViewOwner), size);
    if (!storage)
    {
        diagnostic.reason = EDataOperationReason::allocation_failed;
        return false;
    }
    detail::SDataViewOwner* const owner = new (storage) detail::SDataViewOwner(limits);
    CDataViewOwnerGuard guard{ owner };
    detail::CDataOperation& operation = owner->operation;
    (void)operation.reserve_external(size);
    const detail::CDataOperationScope scope{ operation };
    if (!owner->metadata.emplace(operation) || !owner->metadata->load(source))
    {
        operation.diagnose(diagnostic);
        return false;
    }
    std::uint64_t generation = s_view_generation.load(std::memory_order_relaxed);
    do
    {
        if (generation == UINT64_MAX)
        {
            diagnostic.reason = EDataOperationReason::invalid_input;
            return false;
        }
    } while (!s_view_generation.compare_exchange_weak(generation, (generation + 1u), std::memory_order_relaxed));
    m_owner = guard.release();
    m_generation = generation;
    return true;
}

void CDataBlockView::clear() noexcept
{
    destroy_view_owner(m_owner);
    m_owner = nullptr;
    m_generation = 0u;
}

const CResolvedTypes* CDataBlockView::types() const noexcept { return m_owner ? m_owner->metadata->types.resolved() : nullptr; }
const CBakedDocument* CDataBlockView::document() const noexcept { return m_owner ? &m_owner->metadata->document : nullptr; }
CByteConstView CDataBlockView::file_bytes() const noexcept { return m_owner ? m_owner->metadata->file : CByteConstView{}; }

std::uint32_t CDataBlockView::entry_count(const EDataEntryKind kind) const noexcept
{
    if (!m_owner) return 0u;
    if (kind == EDataEntryKind::instance)
    {
        return static_cast<std::uint32_t>(m_owner->metadata->snapshots.size());
    }
    if (kind == EDataEntryKind::collection)
    {
        return static_cast<std::uint32_t>(m_owner->metadata->collections.size());
    }
    return 0u;
}

CDataEntryHandle CDataBlockView::entry_at(const EDataEntryKind kind, const std::uint32_t ordinal) const noexcept
{
    CDataEntryHandle result;
    if (ordinal >= entry_count(kind)) return result;
    result.m_owner = this;
    result.m_generation = m_generation;
    result.m_index = ordinal;
    result.m_kind = kind;
    return result;
}

bool CDataBlockView::contains(const CDataEntryHandle handle) const noexcept
{
    return m_owner && (handle.m_owner == this) && (handle.m_generation == m_generation) &&
        (handle.m_index < entry_count(handle.m_kind));
}

bool CDataBlockView::contains(const CDataValueView& value) const noexcept
{
    return m_owner && (value.m_owner == this) && (value.m_generation == m_generation);
}

CDataValueView CDataBlockView::make_value(const CTypesIndex type, const std::uint8_t* const bytes,
    const std::size_t size) const noexcept
{
    CDataValueView result;
    result.m_owner = this;
    result.m_generation = m_generation;
    result.m_type = type;
    result.m_bytes = bytes;
    result.m_size = size;
    return result;
}

CDataEntryHandle CDataBlockView::find_collection(const CStringView& type, const CStringView& name) const noexcept
{
    if (!m_owner) return {};
    const CTypesIndex element = types()->find_type(type);
    for (std::uint32_t index = 0u; index < m_owner->metadata->collections.size(); ++index)
    {
        const detail::SBakedBulkRecord& collection = m_owner->metadata->collections[index];
        if ((collection.type == element) && (m_owner->metadata->document.name(collection.entry) == name))
        {
            return entry_at(EDataEntryKind::collection, index);
        }
    }
    return {};
}

CDataEntryHandle CDataBlockView::find_instance(const CStringView& type, const CStringView& name,
    const CDataEntryHandle parent) const noexcept
{
    if (!m_owner || (parent && (!contains(parent) || (parent.m_kind != EDataEntryKind::instance))))
    {
        return {};
    }
    const CTypesIndex element = types()->find_type(type);
    const std::uint32_t ancestor = parent ? parent.m_index : UINT32_MAX;
    for (std::uint32_t index = 0u; index < m_owner->metadata->snapshots.size(); ++index)
    {
        const detail::SBakedInstanceRecord& instance = m_owner->metadata->snapshots[index];
        if ((instance.type == element) && (instance.parent == ancestor) && (m_owner->metadata->document.name(instance.entry) == name))
        {
            return entry_at(EDataEntryKind::instance, index);
        }
    }
    return {};
}

bool CDataBlockView::entry_identity(const CDataEntryHandle handle, EDataEntryKind& kind,
    std::uint32_t& ordinal) const noexcept
{
    if (!contains(handle)) return false;
    kind = handle.m_kind;
    ordinal = handle.m_index;
    return true;
}

bool CDataBlockView::entry(const CDataEntryHandle handle, SDataEntryInfo& result) const noexcept
{
    if (!contains(handle)) return false;
    SDataEntryInfo info;
    info.kind = handle.m_kind;
    if (handle.m_kind == EDataEntryKind::collection)
    {
        const detail::SBakedBulkRecord& collection = m_owner->metadata->collections[handle.m_index];
        info.type = collection.type;
        info.name = m_owner->metadata->document.name(collection.entry);
        info.count = collection.count;
        info.stride = collection.stride;
    }
    else
    {
        const detail::SBakedInstanceRecord& instance = m_owner->metadata->snapshots[handle.m_index];
        info.type = instance.type;
        info.name = m_owner->metadata->document.name(instance.entry);
        info.count = 1u;
        info.stride = instance.extent;
        if (instance.parent != UINT32_MAX) info.parent = entry_at(EDataEntryKind::instance, instance.parent);
    }
    SType layout;
    if (!types()->type(info.type, layout)) return false;
    info.type_name = types()->name(layout.name);
    if (!info.type_name.length()) info.type_name = primitive_name(layout.primitive);
    result = info;
    return true;
}

bool CDataBlockView::value(const CDataEntryHandle handle, const std::uint32_t record, CDataValueView& result) const noexcept
{
    if (!contains(handle)) return false;
    if (handle.m_kind == EDataEntryKind::collection)
    {
        const detail::SBakedBulkRecord& collection = m_owner->metadata->collections[handle.m_index];
        if (record >= collection.count) return false;
        const std::uint8_t* const bytes = collection.stride ? m_owner->metadata->bulk_bytes.data() + collection.offset +
            (static_cast<std::size_t>(collection.stride) * record) : nullptr;
        result = make_value(collection.type, bytes, collection.stride);
    }
    else
    {
        if (record) return false;
        const detail::SBakedInstanceRecord& instance = m_owner->metadata->snapshots[handle.m_index];
        result = make_value(instance.type, instance.extent ? m_owner->metadata->instance_bytes.data() + instance.offset : nullptr, instance.extent);
    }
    return true;
}

bool CDataBlockView::member(const CDataValueView& value, const CStringView& name, CDataValueView& result) const noexcept
{
    if (!contains(value)) return false;
    SMember member;
    if (!types()->member(types()->find_member(value.m_type, name), member) || (member.offset > value.m_size) ||
        (member.size > (value.m_size - member.offset)))
    {
        return false;
    }
    result = make_value(member.type, member.size ? value.m_bytes + member.offset : nullptr, static_cast<std::size_t>(member.size));
    return true;
}

bool CDataBlockView::element(const CDataValueView& value, const std::uint32_t index, CDataValueView& result) const noexcept
{
    if (!contains(value)) return false;
    SType layout, element;
    if (!types()->type(value.m_type, layout) || (layout.category != ECategory::array) || (index >= layout.count) ||
        !types()->type(layout.element_or_storage, element))
    {
        return false;
    }
    result = make_value(layout.element_or_storage, element.size ? value.m_bytes + (layout.stride * index) : nullptr,
        static_cast<std::size_t>(element.size));
    return true;
}

bool CDataBlockView::bytes(const CDataValueView& value, CByteConstView& result) const noexcept
{
    if (!contains(value)) return false;
    result = value.m_size ? CByteConstView{ value.m_bytes, value.m_size, 1u } : CByteConstView{};
    return true;
}

bool CDataBlockView::text(const CDataValueView& value, SDataTextView& result) const noexcept
{
    if (!contains(value)) return false;
    SType layout;
    return types()->type(value.m_type, layout) && (layout.category == ECategory::text) &&
        m_owner->metadata->text_id(layout, value.m_bytes, result);
}

bool CDataBlockView::span(const CDataValueView& value, SDataRangeView& result) const noexcept
{
    if (!contains(value)) return false;
    SType layout;
    if (!types()->type(value.m_type, layout) || (layout.category != ECategory::span))
    {
        return false;
    }
    SDataRangeView range;
    std::uint32_t collection{};
    if (!m_owner->metadata->span_id(layout, value.m_bytes, collection, range.first, range.count, range.absent))
    {
        return false;
    }
    if (range.count) range.collection = entry_at(EDataEntryKind::collection, collection);
    range.element_type = layout.element_or_storage;
    result = range;
    return true;
}

bool CDataBlockView::element(const SDataRangeView& range, const std::uint32_t index, CDataValueView& result) const noexcept
{
    if (range.absent || (index >= range.count) || !contains(range.collection) ||
        (range.collection.m_kind != EDataEntryKind::collection))
    {
        return false;
    }
    const detail::SBakedBulkRecord& collection = m_owner->metadata->collections[range.collection.m_index];
    return (range.element_type == collection.type) &&
        ((static_cast<std::uint64_t>(range.first) + range.count) <= collection.count) &&
        value(range.collection, (range.first + index), result);
}

bool CDataBlockView::copy_text_utf8(const CDataValueView& value, CByteBuffer& output) const noexcept
{
    SDataOperationDiagnostic diagnostic;
    return copy_text_utf8(value, SDataOperationLimits{}, output, diagnostic);
}

bool CDataBlockView::copy_text_utf8(const CDataValueView& value, const SDataOperationLimits limits,
    CByteBuffer& output, SDataOperationDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    detail::CDataOperation operation{ limits };
    operation.stage(EDataOperationStage::construction);
    if (!operation.valid() || output.capacity())
    {
        diagnostic.reason = output.capacity() ? EDataOperationReason::destination_not_empty : EDataOperationReason::invalid_input;
        return false;
    }
    SDataTextView text;
    if (!this->text(value, text) || text.absent)
    {
        diagnostic.reason = EDataOperationReason::invalid_input;
        return false;
    }
    CByteBuffer staged;
    const std::size_t size = text.stored_text.length();
    const std::size_t owned = size ? operation.parent_context()->condition_bytes(1u, size) : 0u;
    if (!operation.source(file_bytes().data(), file_bytes().size()) || !operation.output(size) ||
        !operation.bytes_work(size) || !operation.reserve_external(owned))
    {
        operation.diagnose(diagnostic);
        return false;
    }
    if (size && !staged.allocate(size, 1u))
    {
        operation.release_external(owned);
        (void)operation.fail(EDataOperationReason::allocation_failed);
        operation.diagnose(diagnostic);
        return false;
    }
    std::size_t written{};
    for (std::size_t index = 0u; index < size; ++index)
    {
        const std::uint8_t byte = static_cast<std::uint8_t>(text.stored_text.string()[index]);
        if (byte == 0xc0u)
        {
            staged.data()[written++] = 0u;
            ++index; // Validated stored text admits C0 only as the exact NUL pair.
        }
        else staged.data()[written++] = byte;
    }
    if (size && !staged.set_size(written))
    {
        operation.release_external(owned);
        diagnostic.reason = EDataOperationReason::invalid_input;
        return false;
    }
    output = std::move(staged);
    operation.release_external(owned);
    return true;
}

}   // namespace schema
