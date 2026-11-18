
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_export.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Bounded logical inspection/import documents with types and named collections.

#include "schema/data_copy.hpp"
#include "schema/data_operation_internal.hpp"
#include "schema/operation_meter.hpp"
#include "schema/data_reference_context.hpp"
#include "schema/document_construction.hpp"
#include "schema/instance_selection.hpp"
#include "data_model/document_translation.hpp"

#include <cstring>

namespace schema::detail
{

struct SDataExport
{
    SDataViewState& source;
    CLiveDocument document;

    [[nodiscard]] bool append(const CNodeKey parent, const CNodeKey child) noexcept
    {
        return (child && document.append_child(parent, child).succeeded()) ||
            source.operation.fail(EDataOperationReason::allocation_failed);
    }

    [[nodiscard]] CNodeKey object(const CNodeKey parent, const CStringView& name) noexcept
    {
        CNodeKey result = document.object_child(parent, name);
        if (!result)
        {
            if (!source.operation.nodes(1u)) return {};
            result = document.create_object(name);
            if (!append(parent, result)) return {};
        }
        return result;
    }

    static CNodeKey decode(void* const context, CLiveDocument& document, const CTypesIndex index,
        const std::uint8_t* const bytes, const CStringView& name, EReason& reason) noexcept
    {
        SDataExport& exporter = *static_cast<SDataExport*>(context);
        SType type;
        reason = EReason::invalid_input;
        if (!exporter.source.types.resolved()->type(index, type))
        {
            return {};
        }
        CNodeKey result;
        if (type.category == ECategory::text)
        {
            SDataTextView text;
            if (!exporter.source.text_id(type, bytes, text))
            {
                return {};
            }
            if (!text.absent)
            {
                if (!exporter.source.operation.bytes_work(text.stored_text.length()))
                {
                    return {};
                }
                result = document.create_string(text.stored_text, name);
                reason = result ? EReason::none : EReason::allocation_failed;
                return result;
            }
            if (!exporter.source.operation.nodes(1u))
            {
                return {};
            }
            result = document.create_object(name);
            if (!result || !document.append_child(result, document.create_boolean(true, CStringView{ "absent" })).succeeded())
            {
                return {};
            }
        }
        else
        {
            std::uint32_t collection{}, first{}, count{};
            bool absent{};
            if (!exporter.source.span_id(type, bytes, collection, first, count, absent) ||
                !exporter.source.operation.nodes(count ? 3u : 1u)) return {};
            result = document.create_object(name);
            if (!result) return {};
            if (!count)
            {
                if (!document.append_child(result, document.create_boolean(true, CStringView{ absent ? "absent" : "empty" })).succeeded())
                {
                    return {};
                }
            }
            else if (!document.append_child(result, document.create_string(exporter.source.document.name(exporter.source.collections[collection].entry), CStringView{ "collection" })).succeeded() ||
                !document.append_child(result, document.create_unsigned_integer(first, CStringView{ "first" })).succeeded() ||
                !document.append_child(result, document.create_unsigned_integer(count, CStringView{ "count" })).succeeded()) return {};
        }
        reason = EReason::none;
        return result;
    }

    static bool encode(void* const context, const SType& type, const SReferenceValue& value,
        std::uint8_t* const bytes, EReason& reason) noexcept
    {
        SDataExport& exporter = *static_cast<SDataExport*>(context);
        if (!value.targeted)
        {
            return SDataViewState::encode_default(&exporter.source, type, value, bytes, reason);
        }
        for (std::size_t index = 0u; index < exporter.source.collections.size(); ++index)
        {
            if (!exporter.source.operation.work())
            {
                reason = EReason::storage_limit;
                return false;
            }
            const SBakedBulkRecord& target = exporter.source.collections[index];
            if ((target.type != type.element_or_storage) || !(exporter.source.document.name(target.entry) == value.collection))
            {
                continue;
            }
            if ((static_cast<std::uint64_t>(value.first) + value.count) > target.count)
            {
                reason = EReason::invalid_range;
                return false;
            }
            std::uint64_t id{};
            if (value.count && (!exporter.source.document.unsigned_integer_value(
                exporter.source.document.object_child(target.entry, CStringView{ "collectionId" }), id) ||
                !id || (id > INT32_MAX)))
            {
                reason = EReason::invalid_input;
                return false;
            }
            write_scalar_bits(bytes, 4u, id);
            write_scalar_bits((bytes + 4u), 4u, (value.count ? value.first : 0u));
            write_scalar_bits((bytes + 8u), 4u, value.count);
            return true;
        }
        reason = EReason::invalid_input;
        return false;
    }

    [[nodiscard]] bool construct() noexcept
    {
        CDataOperation& operation = source.operation;
        const CDocumentRead read{ source.document };
        const SDocumentSection types{ &read, read.object_child(read.root(), CStringView{ "types" }) };
        SDocumentBuildDiagnostic copied;
        if (!operation.nodes(source.document.value_count()) || !copy_document_sections(&types, 1u, document, copied))
        {
            return false;
        }
        for (const char* const name : { "instances", "data" })
        {
            if (source.document.object_child(source.document.root(), CStringView{ name }) && !object(document.root(), CStringView{ name }))
            {
                return false;
            }
        }
        const SReferenceEncoding encoding{ this, &encode };
        const SReferenceDecoding decoding{ this, &decode, &encoding };
        TPodVector<CNodeKey> instances;
        if (!instances.resize(source.snapshots.size()))
        {
            return operation.fail(EDataOperationReason::allocation_failed);
        }
        for (std::uint32_t index = 0u; index < source.snapshots.size(); ++index)
        {
            const SBakedInstanceRecord& entry = source.snapshots[index];
            if (!operation.work()) return false;
            operation.location(source.document.name(entry.entry));
            SType layout;
            if (!source.types.resolved()->type(entry.type, layout))
            {
                return false;
            }
            CNodeKey group;
            if (entry.parent == UINT32_MAX)
            {
                const CNodeKey section = object(document.root(), CStringView{ "instances" });
                const CStringView type_name = (layout.category == ECategory::primitive) ? primitive_name(layout.primitive) : source.types.resolved()->name(layout.name);
                group = section ? object(section, type_name) : CNodeKey{};
            }
            else group = object(instances[entry.parent], CStringView{ "specialisation" });
            if (!group || !operation.nodes(1u))
            {
                return false;
            }
            const CNodeKey created = document.create_object(source.document.name(entry.entry));
            if (!append(group, created)) return false;
            instances[index] = created;
            SValueDiagnostic error;
            const CNodeKey declaration = export_selection(document, *source.types.resolved(), entry.type,
                { &read, SOccurrence{ entry.declaration } }, (entry.extent ? source.instance_bytes.data() + entry.offset : nullptr),
                CStringView{ "declaration" }, error, &decoding);
            if ((error.reason != EReason::none) || (declaration && !append(created, declaration)))
            {
                return operation.fail((error.reason == EReason::allocation_failed) ? EDataOperationReason::allocation_failed : EDataOperationReason::unrepresentable_value);
            }
        }
        for (std::uint32_t index = 0u; index < source.collections.size(); ++index)
        {
            const SBakedBulkRecord& entry = source.collections[index];
            if (!operation.work()) return false;
            const CNodeKey section = object(document.root(), CStringView{ "data" });
            SType layout;
            if (!section || !source.types.resolved()->type(entry.type, layout))
            {
                return false;
            }
            const CStringView type_name = (layout.category == ECategory::primitive) ? primitive_name(layout.primitive) : source.types.resolved()->name(layout.name);
            const CNodeKey group = object(section, type_name);
            if (!group || !operation.nodes(2u))
            {
                return false;
            }
            const CNodeKey created = document.create_object(source.document.name(entry.entry));
            const CNodeKey records = document.create_array(CStringView{ "data" });
            if (!append(group, created) || !append(created, records))
            {
                return false;
            }
            for (std::uint32_t ordinal = 0u; ordinal < entry.count; ++ordinal)
            {
                operation.location(source.document.name(entry.entry), ordinal);
                if (!operation.work()) return false;
                SValueDiagnostic error;
                const std::uint8_t* const bytes = entry.extent ? source.bulk_bytes.data() + entry.offset + (layout.size * ordinal) : nullptr;
                const CNodeKey value = export_selection(document, *source.types.resolved(), entry.type, SSelectionView::complete(),
                    bytes, {}, error, &decoding);
                if ((error.reason != EReason::none) || !value)
                {
                    return operation.fail((error.reason == EReason::allocation_failed) ? EDataOperationReason::allocation_failed : EDataOperationReason::unrepresentable_value);
                }
                if (!append(records, value)) return false;
            }
        }
        return data_healthy();
    }
};

static bool publish_export(CBakedDocumentBlock& source, CDataOperation& operation, CBakedDocumentBlock& output) noexcept
{
    operation.stage(EDataOperationStage::publication);
    const CByteConstView bytes = source.bytes();
    if (!operation.output(bytes.size()) || !operation.bytes_work(static_cast<std::uint64_t>(bytes.size()) << 1u) ||
        !operation.work(static_cast<std::uint64_t>(source.document().value_count()) << 2u))
    {
        return false;
    }
    const std::size_t size = operation.parent_context()->condition_bytes(128u, bytes.size());
    if (!operation.reserve_external(size)) return false;
    CByteBuffer storage;
    bool success{};
    {
        const CDataOperationScope parent{ operation.parent_context() };
        CByteBuffer independent;
        if (independent.allocate(bytes.size(), 128u) && independent.set_size(bytes.size()))
        {
            std::memcpy(independent.data(), bytes.data(), bytes.size());
            storage = std::move(independent);
            success = true;
        }
    }
    success = success && output.adopt_buffer(std::move(storage));
    operation.release_external(size);
    return success || operation.fail(EDataOperationReason::allocation_failed);
}

}   // namespace schema::detail

namespace schema
{

bool export_data(const CDataBlockView& source, const SDataOperationLimits limits,
    CBakedDocumentBlock& output, SDataOperationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (output.buffer_capacity())
    {
        diagnostic.reason = EDataOperationReason::destination_not_empty;
        return false;
    }
    detail::CDataOperation operation{ limits };
    const detail::CDataOperationScope scope{ operation };
    detail::SDataViewState checked{ operation };
    detail::SDataExport exporter{ checked };
    CBakedDocumentBlock staged;
    const bool success = source.is_ready() && checked.load_bytes(source.file_bytes()) && exporter.construct() &&
        detail::prepare_data_document_bake(exporter.document) &&
        document_translation::bake(exporter.document, staged) && detail::publish_export(staged, operation, output);
    if (!success)
    {
        operation.diagnose(diagnostic);
        if (diagnostic.reason == EDataOperationReason::none) diagnostic.reason = EDataOperationReason::invalid_input;
    }
    return success;
}

}   // namespace schema
