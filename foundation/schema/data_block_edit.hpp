
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_edit.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Private reference-aware edit sessions with explicit atomic publication.

#pragma once

#include "schema/data_block_view.hpp"
#include "schema/instance_edit_types.hpp"

namespace schema
{

namespace detail { struct SDataEditState; }

class CDataBlockEdit;

class CDataEditHandle
{
public:
    [[nodiscard]] explicit operator bool() const noexcept { return m_owner != nullptr; }

private:
    friend class CDataBlockEdit;
    const CDataBlockEdit* m_owner{};
    std::uint64_t m_generation{};
    std::uint32_t m_index{};
};

class CDataBlockEdit
{
public:
    CDataBlockEdit() noexcept = default;
    ~CDataBlockEdit() noexcept;
    CDataBlockEdit(const CDataBlockEdit&) = delete;
    CDataBlockEdit& operator=(const CDataBlockEdit&) = delete;

    [[nodiscard]] bool begin(const CDataBlockView& source, const SDataOperationLimits limits,
        SDataOperationDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool begin_empty(const CTypesDocumentQuery& types, const SDataOperationLimits limits,
        SDataOperationDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool is_ready() const noexcept { return m_state != nullptr; }
    [[nodiscard]] CDataEditHandle find(const CStringView& type, const EDataEntryKind kind, const CStringView& name,
        const CDataEditHandle parent = {}) noexcept;

    //  Logical inputs are consumed synchronously. A missing declaration means
    //  defaults for a base or inheritance for a specialisation. Collection input
    //  is a nonempty array of complete records. No writable descriptors escape.
    [[nodiscard]] CDataEditHandle create_instance(const CStringView& type, const CStringView& name,
        const CLiveDocument& input, const CNodeKey declaration, const CDataEditHandle parent,
        SDataOperationDiagnostic& diagnostic) noexcept;
    [[nodiscard]] CDataEditHandle create_collection(const CStringView& type, const CStringView& name,
        const CLiveDocument& input, const CNodeKey records, SDataOperationDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool replace_collection(const CDataEditHandle target, const CLiveDocument& input,
        const CNodeKey records, SDataOperationDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool write(const CDataEditHandle target, const std::uint32_t record,
        const SInstanceSelectionStep* const path, const std::size_t path_count,
        const CLiveDocument& input, const CNodeKey value, const EInstanceDescendantPolicy policy,
        SDataOperationDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool rename(const CDataEditHandle target, const CStringView& name,
        SDataOperationDiagnostic& diagnostic) noexcept;
    //  Erases only this entry. Remaining children/references must be repaired
    //  before commit; a replacement with the same name is a distinct identity.
    [[nodiscard]] bool erase(const CDataEditHandle target, SDataOperationDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool commit(CBakedDataBlock& output, SDataOperationDiagnostic& diagnostic) noexcept;
    void cancel() noexcept;

private:
    [[nodiscard]] bool start(const SDataOperationLimits limits, SDataOperationDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool contains(const CDataEditHandle handle) const noexcept;
    [[nodiscard]] CDataEditHandle handle(const std::uint32_t index) const noexcept;
    detail::SDataEditState* m_state{};
    std::uint64_t m_generation{};
};

}   // namespace schema
