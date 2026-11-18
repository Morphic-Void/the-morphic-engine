
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_candidate.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Preserve record bytes privately and relocate declared references at publication.

#include "schema/data_candidate.hpp"
#include "schema/document_construction.hpp"
#include "schema/data_block_construction.hpp"
#include "schema/data_operation_internal.hpp"
#include "data_model/string_value_remap.hpp"

#include <cstring>

namespace schema::detail
{

bool CDataCandidate::initialise() noexcept
{
    return operation.nodes(1u) && (document.initialise() || operation.fail(EDataOperationReason::allocation_failed));
}

bool CDataCandidate::check_records(const std::uint32_t replacement, const std::uint32_t count) noexcept
{
    std::uint64_t total = count;
    for (std::size_t index = 0u; index < entries.size(); ++index)
    {
        if (!operation.work()) return false;
        if ((index != replacement) && entries[index].alive) total += entries[index].count;
    }
    return operation.records(total);
}

bool CDataCandidate::error(const EReason reason) noexcept
{
    return operation.fail((reason == EReason::allocation_failed) ? EDataOperationReason::allocation_failed :
        EDataOperationReason::invalid_input);
}

bool CDataCandidate::admitted_nodes(const CDocumentRead& source, const SOccurrence value, const unsigned depth) noexcept
{
    if (!value) return true;
    if (!operation.depth(depth) || !operation.nodes(1u) || !operation.bytes_work(source.name(value).length()))
    {
        return false;
    }
    if ((source.value_kind(value) == EDocumentValueKind::string) && !operation.bytes_work(source.string_value(value).length()))
    {
        return false;
    }
    for (SOccurrence child = source.first_child(value); child; child = source.next_sibling(child))
    {
        if (!admitted_nodes(source, child, (depth + 1u)))
        {
            return false;
        }
    }
    return true;
}

CNodeKey CDataCandidate::copy_node(const CDocumentRead& source, const SOccurrence value) noexcept
{
    if (!value || !admitted_nodes(source, value))
    {
        return {};
    }
    SDocumentBuildDiagnostic error;
    const CNodeKey result = copy_document_subtree(source, value, document, 0u, error);
    if (!result) (void)operation.fail(EDataOperationReason::allocation_failed);
    return result;
}

std::uint32_t CDataCandidate::find(const CTypesIndex type, const EDataEntryKind kind,
    const CStringView& name, const std::uint32_t parent) noexcept
{
    for (std::uint32_t index = 0u; index < entries.size(); ++index)
    {
        if (!operation.work()) return UINT32_MAX;
        const SDataCandidateEntry& entry = entries[index];
        if ((entry.alive || entry.forward) && (entry.type == type) && (entry.kind == kind) &&
            (entry.parent == parent) && (document.name(entry.metadata) == name))
        {
            return index;
        }
    }
    return UINT32_MAX;
}

bool CDataCandidate::append(const CTypesIndex type, const EDataEntryKind kind, const CStringView& name,
    const std::uint32_t parent, const std::uint32_t count, std::uint32_t& index, const bool forward) noexcept
{
    if (!name.length() || !type || (entries.size() >= INT32_MAX) || (count > memory::k_byte_size_ceiling))
    {
        return operation.fail(EDataOperationReason::invalid_input);
    }
    index = find(type, kind, name, parent);
    if (index != UINT32_MAX)
    {
        SDataCandidateEntry& existing = entries[index];
        if (!existing.forward || forward)
        {
            return operation.fail(EDataOperationReason::naming_conflict);
        }
        if (!check_records(index, count)) return false;
        existing.forward = false;
        existing.alive = true;
        existing.count = count;
        return true;
    }
    if (!operation.nodes(1u) || !check_records(UINT32_MAX, count) || !operation.bytes_work(name.length()))
    {
        return false;
    }
    const CNodeKey metadata = document.create_object(name);
    if (!metadata)
    {
        return operation.fail(EDataOperationReason::allocation_failed);
    }
    SDataCandidateEntry entry;
    entry.metadata = metadata;
    entry.type = type;
    entry.kind = kind;
    entry.parent = parent;
    entry.count = count;
    entry.forward = forward;
    entry.alive = !forward;
    index = static_cast<std::uint32_t>(entries.size());
    return entries.push_back(std::move(entry)) || operation.fail(EDataOperationReason::allocation_failed);
}

bool CDataCandidate::copy_bytes(SDataCandidateEntry& entry, const CByteConstView source) noexcept
{
    if (!operation.bytes_work(source.size())) return false;
    if (!source.size()) return true;
    if (!entry.bytes.allocate(source.size(), k_max_alignment) || !entry.bytes.set_size(source.size()))
    {
        return operation.fail(EDataOperationReason::allocation_failed);
    }
    std::memcpy(entry.bytes.data(), source.data(), source.size());
    return true;
}

struct SCandidateTextCopy
{
    CDataCandidate& source;
    CReferenceConstruction& destination;
    std::uint8_t* bytes;
    static bool slot(void* const context, const SType& type, const SDataReferenceSlot slot) noexcept
    {
        if (type.category != ECategory::text) return true;
        SCandidateTextCopy& copy = *static_cast<SCandidateTextCopy*>(context);
        std::uint8_t* const bytes = copy.bytes + slot.offset;
        std::uint32_t id = static_cast<std::uint32_t>(read_scalar_bits(bytes, 4u));
        if (!id || (id == UINT32_MAX))
        {
            return true;
        }
        CStringView text;
        if (!copy.source.strings.lookup(id, text) || !copy.destination.intern(text, id))
        {
            return false;
        }
        write_scalar_bits(bytes, 4u, id);
        return true;
    }
};

bool CDataCandidate::clone(CDataCandidate& destination) noexcept
{
    if (!destination.initialise() || !destination.entries.reserve(entries.size()))
    {
        return false;
    }
    destination.present_domains[0] = present_domains[0];
    destination.present_domains[1] = present_domains[1];
    for (std::size_t index = 0u; index < entries.size(); ++index)
    {
        if (!operation.work()) return false;
        const SDataCandidateEntry& old = entries[index];
        SDataCandidateEntry copied;
        copied.type = old.type;
        copied.kind = old.kind;
        copied.parent = old.parent;
        copied.count = old.count;
        copied.alive = old.alive;
        copied.forward = old.forward;
        copied.metadata = destination.copy_node(CDocumentRead{ document }, SOccurrence{ old.metadata });
        if (!copied.metadata) return false;
        copied.selection = destination.document.object_child(copied.metadata, CStringView{ "declaration" });
        if (copied.alive)
        {
            if (!destination.copy_bytes(copied, old.bytes.const_view()))
            {
                return false;
            }
            SType layout;
            if (!types.resolved()->type(copied.type, layout))
            {
                return operation.fail(EDataOperationReason::invalid_input);
            }
            for (std::uint32_t record = 0u; record < copied.count; ++record)
            {
                SCandidateTextCopy copy{ *this, destination.strings,
                    (layout.size ? copied.bytes.data() + (layout.size * record) : nullptr) };
                if (!visit_reference_slots(*types.resolved(), copied.type, 0u, operation, &copy, &SCandidateTextCopy::slot))
                {
                    return false;
                }
            }
        }
        if (!destination.entries.push_back(std::move(copied)))
        {
            return operation.fail(EDataOperationReason::allocation_failed);
        }
    }
    return true;
}

bool CDataCandidate::encode(void* const context, const SType& type, const SReferenceValue& value,
    std::uint8_t* const destination, EReason& reason) noexcept
{
    CDataCandidate& candidate = *static_cast<CDataCandidate*>(context);
    std::uint32_t id = value.absent ? UINT32_MAX : 0u;
    std::uint32_t first{}, count{};
    if (type.category == ECategory::text)
    {
        if (!value.absent && !candidate.strings.intern(value.text, id))
        {
            reason = EReason::allocation_failed;
            return false;
        }
    }
    else if (value.targeted)
    {
        std::uint32_t index = candidate.find(type.element_or_storage, EDataEntryKind::collection, value.collection);
        if (!value.count)
        {
            if ((index == UINT32_MAX) || !candidate.entries[index].alive || (value.first > candidate.entries[index].count))
            {
                reason = EReason::invalid_range;
                return false;
            }
        }
        else
        {
            if ((index == UINT32_MAX) && !candidate.append(type.element_or_storage, EDataEntryKind::collection,
                value.collection, UINT32_MAX, 0u, index, true))
            {
                reason = EReason::allocation_failed;
                return false;
            }
            id = index + 1u;
            first = value.first;
            count = value.count;
        }
    }
    write_scalar_bits(destination, 4u, id);
    if (type.category == ECategory::span)
    {
        write_scalar_bits((destination + 4u), 4u, first);
        write_scalar_bits((destination + 8u), 4u, count);
    }
    return true;
}

static bool attach(CLiveDocument& document, const CNodeKey parent, const CNodeKey child) noexcept
{
    return child && document.append_child(parent, child).succeeded();
}

bool CDataCandidate::publish(CBakedDataBlock& output) noexcept
{
    operation.stage(EDataOperationStage::baking);
    if (output.loaded_ready())
    {
        return operation.fail(EDataOperationReason::destination_not_empty);
    }
    CLiveDocument description;
    const CTypesDocumentQuery query = types.document_query();
    const CDocumentRead& read = SDocumentQueryAccess::read(query);
    const SOccurrence types_root = read.object_child(read.root(), CStringView{ "types" });
    SDocumentBuildDiagnostic copy_error;
    const SDocumentSection section{ &read, types_root };
    if (!admitted_nodes(read, types_root) || !copy_document_sections(&section, 1u, description, copy_error))
    {
        return operation.fail(EDataOperationReason::allocation_failed);
    }
    CReferenceConstruction retained{ description, operation };
    TPodVector<CNodeKey> nodes;
    TPodVector<std::uint32_t> offsets;
    if (!nodes.resize(entries.size()) || !offsets.resize(entries.size()))
    {
        return operation.fail(EDataOperationReason::allocation_failed);
    }
    std::size_t totals[2]{};
    bool domains[2]{ present_domains[0], present_domains[1] };
    for (unsigned domain = 0u; domain < 2u; ++domain)
    {
        if (!domains[domain]) continue;
        if (!operation.nodes(1u) || !attach(description, description.root(),
            description.create_object(CStringView{ domain ? "data" : "instances" })))
        {
            return operation.fail(EDataOperationReason::allocation_failed);
        }
    }
    for (std::uint32_t index = 0u; index < entries.size(); ++index)
    {
        if (!operation.work()) return false;
        const SDataCandidateEntry& entry = entries[index];
        if (!entry.alive) continue;
        operation.location(document.name(entry.metadata));
        const bool bulk = entry.kind == EDataEntryKind::collection;
        const unsigned domain = bulk ? 1u : 0u;
        domains[domain] = true;
        SType layout;
        if (!types.resolved()->type(entry.type, layout) || !entry.count ||
            ((layout.size * entry.count) != entry.bytes.size()))
        {
            return operation.fail(EDataOperationReason::invalid_input);
        }
        const std::uint64_t offset = (totals[domain] + (layout.alignment - 1u)) & ~(layout.alignment - 1u);
        const std::uint64_t end = offset + entry.bytes.size();
        if ((end > memory::k_byte_size_ceiling) || !operation.output(end))
        {
            return false;
        }
        totals[domain] = static_cast<std::size_t>(end);
        offsets[index] = static_cast<std::uint32_t>(offset);
        CNodeKey group;
        if (!bulk && (entry.parent != UINT32_MAX))
        {
            if ((entry.parent >= index) || !entries[entry.parent].alive ||
                (entries[entry.parent].type != entry.type) || (entries[entry.parent].kind != EDataEntryKind::instance))
            {
                return operation.fail(EDataOperationReason::invalid_reference);
            }
            group = description.object_child(nodes[entry.parent], CStringView{ "specialisation" });
            if (!group)
            {
                if (!operation.nodes(1u)) return false;
                group = description.create_object(CStringView{ "specialisation" });
                if (!attach(description, nodes[entry.parent], group))
                {
                    return operation.fail(EDataOperationReason::allocation_failed);
                }
            }
        }
        else
        {
            const CStringView section_name{ bulk ? "data" : "instances" };
            CNodeKey domain_root = description.object_child(description.root(), section_name);
            if (!domain_root)
            {
                if (!operation.nodes(1u)) return false;
                domain_root = description.create_object(section_name);
                if (!attach(description, description.root(), domain_root))
                {
                    return operation.fail(EDataOperationReason::allocation_failed);
                }
            }
            const CStringView type_name = (layout.category == ECategory::primitive) ? primitive_name(layout.primitive) : types.resolved()->name(layout.name);
            group = description.object_child(domain_root, type_name);
            if (!group)
            {
                if (!operation.nodes(1u)) return false;
                group = description.create_object(type_name);
                if (!attach(description, domain_root, group))
                {
                    return operation.fail(EDataOperationReason::allocation_failed);
                }
            }
        }
        if (!operation.nodes(bulk ? 7u : 5u)) return false;
        const CNodeKey created = description.create_object(document.name(entry.metadata));
        const CNodeKey locator = description.create_object(CStringView{ "locator" });
        if (!attach(description, group, created) || !attach(description, created, locator) ||
            !attach(description, locator, description.create_unsigned_integer(offset, CStringView{ "offset" })) ||
            !attach(description, locator, description.create_boolean(true, CStringView{ "valid" })) ||
            !attach(description, locator, description.create_unsigned_integer(entry.bytes.size(), CStringView{ "size" })))
        {
            return operation.fail(EDataOperationReason::allocation_failed);
        }
        nodes[index] = created;
        if (bulk)
        {
            if (!attach(description, locator, description.create_unsigned_integer(entry.count, CStringView{ "count" })) ||
                !attach(description, created, description.create_unsigned_integer((index + 1u), CStringView{ "collectionId" })))
            {
                return operation.fail(EDataOperationReason::allocation_failed);
            }
        }
        else if (entry.selection)
        {
            const CDocumentRead selections{ document };
            if (!admitted_nodes(selections, SOccurrence{ entry.selection }))
            {
                return false;
            }
            const CNodeKey selection = copy_document_subtree(selections, SOccurrence{ entry.selection }, description, 0u, copy_error);
            if (!attach(description, created, selection))
            {
                return operation.fail(EDataOperationReason::allocation_failed);
            }
        }
    }
    CByteBuffer payloads[2];
    for (unsigned domain = 0u; domain < 2u; ++domain)
    {
        if (totals[domain] && (!payloads[domain].allocate(totals[domain], k_max_alignment) || !payloads[domain].set_size(totals[domain])))
        {
            return operation.fail(EDataOperationReason::allocation_failed);
        }
        if (totals[domain]) std::memset(payloads[domain].data(), 0, totals[domain]);
    }
    for (std::uint32_t index = 0u; index < entries.size(); ++index)
    {
        const SDataCandidateEntry& entry = entries[index];
        if (!entry.alive) continue;
        if (!operation.bytes_work(entry.bytes.size()))
        {
            return false;
        }
        std::uint8_t* const bytes = entry.bytes.size() ?
            payloads[(entry.kind == EDataEntryKind::collection) ? 1u : 0u].data() + offsets[index] : nullptr;
        if (entry.bytes.size()) std::memcpy(bytes, entry.bytes.data(), entry.bytes.size());
        SType layout;
        (void)types.resolved()->type(entry.type, layout);
        for (std::uint32_t record = 0u; record < entry.count; ++record)
        {
            SCandidateTextCopy copy{ *this, retained, (layout.size ? bytes + (layout.size * record) : nullptr) };
            if (!visit_reference_slots(*types.resolved(), entry.type, 0u, operation, &copy, &SCandidateTextCopy::slot))
            {
                return false;
            }
        }
    }
    if (!retained.finish()) return false;
    document_translation::CStringValueRemap mapping;
    CByteBuffer storage;
    SDataBlockLayout layout;
    SDataBlockDiagnostic block_error;
    if (!operation.nodes(5u) || !prepare_common_data_block_storage(description, (domains[0] ? &totals[0] : nullptr),
        (domains[1] ? &totals[1] : nullptr), storage, layout, block_error, &mapping) || !operation.output(layout.file_size)) return false;
    for (std::uint32_t index = 0u; index < entries.size(); ++index)
    {
        const SDataCandidateEntry& entry = entries[index];
        if (!entry.alive) continue;
        CByteBuffer& payload = payloads[(entry.kind == EDataEntryKind::collection) ? 1u : 0u];
        SType type;
        (void)types.resolved()->type(entry.type, type);
        for (std::uint32_t record = 0u; record < entry.count; ++record)
        {
            std::uint8_t* const bytes = type.size ? payload.data() + offsets[index] + (type.size * record) : nullptr;
            if (!retained.relocate_text(*types.resolved(), entry.type, bytes, mapping))
            {
                return false;
            }
        }
    }
    for (unsigned domain = 0u; domain < 2u; ++domain)
    {
        if (!operation.bytes_work(totals[domain]))
        {
            return false;
        }
        if (totals[domain]) std::memcpy((storage.data() + (domain ? layout.bulk_offset : layout.instance_offset)), payloads[domain].data(), totals[domain]);
    }
    CBakedDocumentBlock block;
    CBakedDataBlock complete;
    if (!block.adopt_buffer(std::move(storage)) || !complete.adopt_local(std::move(block), block_error))
    {
        return operation.fail(EDataOperationReason::invalid_input);
    }
    SDataViewState checked{ operation };
    return checked.load(complete, false) && publish_data_block(complete, operation, output);
}

}   // namespace schema::detail
