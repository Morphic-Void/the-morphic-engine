
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_reference_context.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Bind complete storage, resolve declared references and reject invalid graphs.

#include "schema/data_reference_context.hpp"
#include "schema/data_operation_internal.hpp"
#include "schema/operation_meter.hpp"
#include "schema/data_block_format.hpp"
#include "schema/instance_selection.hpp"
#include "data_model/baked_document_format.hpp"

#include <cstring>
#include <utility>

namespace schema::detail
{

bool publish_data_block(const CBakedDataBlock& source, CDataOperation& operation, CBakedDataBlock& output) noexcept
{
    operation.stage(EDataOperationStage::publication);
    if (output.loaded_ready())
    {
        return operation.fail(EDataOperationReason::destination_not_empty);
    }
    const CByteConstView bytes = source.file_bytes();
    if (!operation.output(bytes.size()) || !operation.bytes_work(static_cast<std::uint64_t>(bytes.size()) * 3u) ||
        !operation.work(static_cast<std::uint64_t>(source.document().value_count()) << 2u))
    {
        return false;
    }
    const std::size_t owned = operation.parent_context()->condition_bytes(k_data_block_alignment, bytes.size());
    if (!operation.reserve_external(owned)) return false;
    CByteBuffer storage;
    bool allocated{};
    {
        const CDataOperationScope parent_scope{ operation.parent_context() };
        CByteBuffer independent;
        allocated = independent.allocate(bytes.size(), k_data_block_alignment) && independent.set_size(bytes.size());
        if (allocated)
        {
            std::memcpy(independent.data(), bytes.data(), bytes.size());
            storage = std::move(independent);
        }
    }
    CBakedDocumentBlock block;
    SDataBlockDiagnostic error;
    const bool published = allocated && block.adopt_buffer(std::move(storage)) && output.adopt_local(std::move(block), error);
    operation.release_external(owned);
    return published || operation.fail(allocated ? EDataOperationReason::invalid_input : EDataOperationReason::allocation_failed);
}

bool visit_reference_slots(const CResolvedTypes& types, const CTypesIndex type, const std::uint64_t offset,
    CDataOperation& operation, void* const context, const FReferenceSlot visit, const unsigned depth) noexcept
{
    if (!operation.work() || !operation.depth(depth))
    {
        return false;
    }
    SType layout;
    if (!types.type(type, layout) || (offset > memory::k_byte_size_ceiling) ||
        (layout.size > (memory::k_byte_size_ceiling - offset)))
    {
        return operation.fail(EDataOperationReason::invalid_input);
    }
    if (!layout.contains_references) return true;
    if ((layout.category == ECategory::text) || (layout.category == ECategory::span))
    {
        return visit(context, layout, { type, static_cast<std::uint32_t>(offset), depth });
    }
    for (std::uint32_t index = 0u; index < layout.count; ++index)
    {
        CTypesIndex child;
        std::uint64_t position{};
        CStringView component;
        if (layout.category == ECategory::structure)
        {
            SMember member;
            if (!types.member(types.member_at(type, index), member))
            {
                return operation.fail(EDataOperationReason::invalid_input);
            }
            child = member.type;
            position = member.offset;
            component = types.name(member.name);
        }
        else if (layout.category == ECategory::array)
        {
            child = layout.element_or_storage;
            position = layout.stride * index;
        }
        else return operation.fail(EDataOperationReason::invalid_input);
        const std::size_t path = operation.push_path(component, ((layout.category == ECategory::array) ? index : UINT32_MAX));
        if (!visit_reference_slots(types, child, (offset + position), operation, context, visit, (depth + 1u)))
        {
            return false;
        }
        operation.restore_path(path);
    }
    return true;
}

bool SDataViewState::load(const CBakedDataBlock& source, const bool inspect_source) noexcept
{
    if (!source.loaded_ready())
    {
        return operation.fail(EDataOperationReason::invalid_input);
    }
    return load_bytes(source.file_bytes(), inspect_source);
}

bool SDataViewState::load_bytes(const CByteConstView source, const bool inspect_source) noexcept
{
    operation.stage(EDataOperationStage::binding);
    if (!operation.valid() || !source.data())
    {
        return operation.fail(EDataOperationReason::invalid_input);
    }
    file = source;
    if ((inspect_source && !operation.source(file.data(), file.size())) || !operation.bytes_work(file.size()))
    {
        return false;
    }
    if (file.size() < sizeof(SBakedDocumentHeader))
    {
        return operation.fail(EDataOperationReason::invalid_input);
    }
    SBakedDocumentHeader header;
    std::memcpy(&header, file.data(), sizeof(header));
    //  Generic framing/string validation and the depth crawl execute complete
    //  passes without schema hooks. Charge those bounded scans before entry.
    if (!operation.nodes(header.value_count) || !operation.work(static_cast<std::uint64_t>(header.value_count) << 3u) ||
        !operation.bytes_work(static_cast<std::uint64_t>(file.size()) << 2u))
    {
        return false;
    }
    SDataBlockView storage;
    SDataBlockDiagnostic error;
    if (!validate_data_block(file.data(), file.size(), storage, error))
    {
        operation.block_diagnostic(error);
        return operation.fail(EDataOperationReason::invalid_input);
    }
    document = storage.document;
    instance_bytes = storage.instances;
    bulk_bytes = storage.bulk_data;
    if (!operation.depth(document.maximum_depth()))
    {
        return false;
    }
    SDiagnostic type_error;
    if (!types.set_document(document) || !types.resolve(type_error))
    {
        return operation.fail((type_error.reason == EReason::allocation_failed) ?
            EDataOperationReason::allocation_failed : EDataOperationReason::invalid_input);
    }

    const CBakedValueIndex retained = document.object_child(document.root(), CStringView{ "retainedText" });
    for (CBakedValueIndex value = document.first_child(retained); value; value = document.next_sibling(value))
    {
        if (!operation.work()) return false;
        if (!retained_ids.push_back(document.string_value_id(value).query_value()))
        {
            return operation.fail(EDataOperationReason::allocation_failed);
        }
    }

    SBakedInstancePlan instance_plan;
    SBakedBulkPlan bulk_plan;
    SBakedRecordPlanDiagnostic plan_error;
    const CResolvedTypes& resolved = *types.resolved();
    if (!plan_baked_instances(document, resolved, instance_bytes.size(), instance_plan, plan_error) ||
        !plan_baked_bulk_data(document, resolved, bulk_bytes.size(), bulk_plan, plan_error))
    {
        return operation.fail((plan_error.reason == EBakedRecordPlanReason::allocation_failed) ?
            EDataOperationReason::allocation_failed : EDataOperationReason::invalid_input);
    }
    snapshots = std::move(instance_plan.records);
    collections = std::move(bulk_plan.records);
    std::uint64_t record_count = snapshots.size();
    if (!operation.records(record_count)) return false;
    for (std::size_t index = 0u; index < collections.size(); ++index)
    {
        const SBakedBulkRecord& record = collections[index];
        operation.location(document.name(record.entry));
        record_count += record.count;
        if (!operation.work() || !operation.records(record_count))
        {
            return false;
        }
        std::uint64_t id{};
        if (!document.unsigned_integer_value(document.object_child(record.entry, CStringView{ "collectionId" }), id) ||
            !id || (id > INT32_MAX))
        {
            return operation.fail(EDataOperationReason::invalid_reference);
        }
        if (!collection_lookup.push_back({ static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(index) }))
        {
            return operation.fail(EDataOperationReason::allocation_failed);
        }
    }
    if (collection_lookup.size() > 1u)
    {
        if (!data_sort(collection_lookup.data(), (collection_lookup.data() + collection_lookup.size()),
            [](const SDataCollectionLookup left, const SDataCollectionLookup right) noexcept
            {
                return left.id < right.id;
            }))
        {
            return false;
        }
        for (std::size_t index = 1u; index < collection_lookup.size(); ++index)
        {
            if (!operation.work()) return false;
            if (collection_lookup[index - 1u].id == collection_lookup[index].id)
            {
                return operation.fail(EDataOperationReason::invalid_reference);
            }
        }
    }
    if (!references() || !instance_inheritance())
    {
        return false;
    }
    return data_healthy();
}

std::uint32_t SDataViewState::collection_index(const std::uint32_t id) const noexcept
{
    std::size_t begin{}, end = collection_lookup.size();
    while (begin < end)
    {
        if (!data_work()) return UINT32_MAX;
        const std::size_t middle = begin + ((end - begin) >> 1u);
        if (collection_lookup[middle].id < id) begin = middle + 1u;
        else end = middle;
    }
    return ((begin < collection_lookup.size()) && (collection_lookup[begin].id == id)) ?
        collection_lookup[begin].ordinal : UINT32_MAX;
}

bool SDataViewState::text_id(const SType& type, const std::uint8_t* const bytes, SDataTextView& output) const noexcept
{
    const std::uint32_t id = static_cast<std::uint32_t>(read_scalar_bits(bytes, 4u));
    if (id == UINT32_MAX)
    {
        if (!type.nullable) return false;
        output = { true, {} };
        return true;
    }
    if ((id > INT32_MAX) || (id > document.string_value_count()))
    {
        return false;
    }
    std::size_t begin{}, end = retained_ids.size();
    while (begin < end)
    {
        if (!data_work()) return false;
        const std::size_t middle = begin + ((end - begin) >> 1u);
        if (retained_ids[middle] < id) begin = middle + 1u;
        else end = middle;
    }
    if (id && ((begin == retained_ids.size()) || (retained_ids[begin] != id)))
    {
        return false;
    }
    output = { false, document.string_value(document.string_value_id_at_rank(id)) };
    return true;
}

bool SDataViewState::span_id(const SType& type, const std::uint8_t* const bytes,
    std::uint32_t& collection, std::uint32_t& first, std::uint32_t& count, bool& absent) const noexcept
{
    const std::uint32_t id = static_cast<std::uint32_t>(read_scalar_bits(bytes, 4u));
    first = static_cast<std::uint32_t>(read_scalar_bits((bytes + 4u), 4u));
    count = static_cast<std::uint32_t>(read_scalar_bits((bytes + 8u), 4u));
    absent = id == UINT32_MAX;
    collection = UINT32_MAX;
    if (!id || absent)
    {
        return (!absent || type.nullable) && !first && !count;
    }
    if ((id > INT32_MAX) || (first > INT32_MAX) || !count || (count > memory::k_byte_size_ceiling))
    {
        return false;
    }
    collection = collection_index(id);
    if (collection == UINT32_MAX) return false;
    const SBakedBulkRecord& target = collections[collection];
    return (target.type == type.element_or_storage) && ((static_cast<std::uint64_t>(first) + count) <= target.count);
}

struct SReferenceGraph
{
    SDataViewState& state;
    TPodVector<std::uint8_t> colours;
    const std::uint8_t* bytes{};
    unsigned depth{};

    [[nodiscard]] bool collection(const std::uint32_t index) noexcept
    {
        CDataOperation& operation = state.operation;
        if (!operation.work() || !operation.depth(depth))
        {
            return false;
        }
        if (colours[index] == 1u)
        {
            return operation.fail(EDataOperationReason::cycle);
        }
        if (colours[index] == 2u) return true;
        colours[index] = 1u;
        const SBakedBulkRecord& target = state.collections[index];
        const std::uint8_t* const saved = bytes;
        ++depth;
        for (std::uint32_t record = 0u; record < target.count; ++record)
        {
            operation.location(state.document.name(target.entry), record);
            bytes = target.extent ? state.bulk_bytes.data() + target.offset + (static_cast<std::size_t>(target.stride) * record) : nullptr;
            if (!visit_reference_slots(*state.types.resolved(), target.type, 0u, operation, this, &slot, depth))
            {
                return false;
            }
        }
        --depth;
        bytes = saved;
        colours[index] = 2u;
        return true;
    }

    static bool slot(void* const context, const SType& type, const SDataReferenceSlot slot) noexcept
    {
        SReferenceGraph& graph = *static_cast<SReferenceGraph*>(context);
        const std::uint8_t* const bytes = graph.bytes + slot.offset;
        if (type.category == ECategory::text)
        {
            SDataTextView text;
            return graph.state.text_id(type, bytes, text) || graph.state.operation.fail(EDataOperationReason::invalid_reference);
        }
        std::uint32_t collection{}, first{}, count{};
        bool absent{};
        if (!graph.state.span_id(type, bytes, collection, first, count, absent))
        {
            return graph.state.operation.fail(EDataOperationReason::invalid_reference);
        }
        if (!count) return true;
        const unsigned previous = graph.depth;
        graph.depth = slot.depth + 1u;
        const bool valid = graph.collection(collection);
        graph.depth = previous;
        return valid;
    }
};

bool SDataViewState::references() noexcept
{
    operation.stage(EDataOperationStage::validation);
    SReferenceGraph graph{ *this };
    if (!graph.colours.resize(collections.size()))
    {
        return operation.fail(EDataOperationReason::allocation_failed);
    }
    if (graph.colours.size()) std::memset(graph.colours.data(), 0, graph.colours.size());
    for (std::uint32_t index = 0u; index < collections.size(); ++index)
    {
        if (!graph.collection(index)) return false;
    }
    for (std::size_t index = 0u; index < snapshots.size(); ++index)
    {
        const SBakedInstanceRecord& snapshot = snapshots[index];
        operation.location(document.name(snapshot.entry));
        graph.bytes = snapshot.extent ? instance_bytes.data() + snapshot.offset : nullptr;
        if (!visit_reference_slots(*types.resolved(), snapshot.type, 0u, operation, &graph, &SReferenceGraph::slot))
        {
            return false;
        }
    }
    return true;
}

bool SDataViewState::encode_default(void* const context, const SType& type, const SReferenceValue& value,
    std::uint8_t* const destination, EReason& reason) noexcept
{
    SDataViewState& state = *static_cast<SDataViewState*>(context);
    std::uint32_t id = value.absent ? UINT32_MAX : 0u;
    if ((type.category == ECategory::text) && !value.absent && value.text.length())
    {
        std::uint32_t begin = 1u, end = state.document.string_value_count() + 1u;
        while (begin < end)
        {
            if (!state.operation.work())
            {
                reason = EReason::storage_limit;
                return false;
            }
            const std::uint32_t middle = begin + ((end - begin) >> 1u);
            const CStringView candidate = state.document.string_value(state.document.string_value_id_at_rank(middle));
            if (candidate.relationship(value.text) < 0) begin = middle + 1u;
            else end = middle;
        }
        if ((begin > state.document.string_value_count()) ||
            !(state.document.string_value(state.document.string_value_id_at_rank(begin)) == value.text))
        {
            reason = EReason::invalid_default;
            return false;
        }
        id = begin;
    }
    if (value.targeted)
    {
        reason = EReason::invalid_default;
        return false;
    }
    write_scalar_bits(destination, 4u, id);
    if (type.category == ECategory::span) std::memset((destination + 4u), 0, 8u);
    return true;
}

bool SDataViewState::instance_inheritance() noexcept
{
    const SReferenceEncoding references{ this, &encode_default };
    SValueDiagnostic error;
    const EValueCheck checked = check_selected_records(*types.resolved(), CDocumentRead{ document }, snapshots,
        instance_bytes.data(), [this](const std::size_t index) noexcept { return snapshots[index].type; }, error,
        &references, &operation.value_work());
    return (checked == EValueCheck::match) || operation.fail((error.reason == EReason::allocation_failed) ?
        EDataOperationReason::allocation_failed : EDataOperationReason::invalid_input);
}

}   // namespace schema::detail
