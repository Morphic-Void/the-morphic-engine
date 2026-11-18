
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_copy_internal.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Operation-local checked sources, collection closure and relocation mappings.

#pragma once

#include "schema/data_copy.hpp"
#include "schema/data_candidate.hpp"
#include "containers/TInstance.hpp"

namespace schema::detail
{

class CDataCopy
{
public:
    explicit CDataCopy(CDataOperation& operation) noexcept : m_operation(operation) {}
    [[nodiscard]] bool prepare(const CDataBlockView* const* const sources, const std::size_t count,
        const SDataCopyName* const names, const std::size_t name_count, CLiveTypes& types) noexcept;
    [[nodiscard]] bool selected(CDataCandidate& candidate, const SDataCopyRoot* const roots,
        const std::size_t count) noexcept;
    [[nodiscard]] bool all(CDataCandidate& candidate) noexcept;
    [[nodiscard]] bool import(CDataCandidate& candidate, const std::uint32_t source,
        const EDataEntryKind kind, const std::uint32_t index, std::uint32_t& destination,
        const unsigned depth = 0u) noexcept;

private:
    struct SMapping { std::uint32_t source, index, destination; EDataEntryKind kind; };
    [[nodiscard]] bool handle(const std::uint32_t source, const CDataEntryHandle entry,
        EDataEntryKind& kind, std::uint32_t& ordinal) noexcept;
    [[nodiscard]] CStringView name(const std::uint32_t source, const EDataEntryKind kind,
        const std::uint32_t index, const CStringView& original) noexcept;
    struct SRelocation
    {
        CDataCopy& copy;
        CDataCandidate& candidate;
        std::uint32_t source;
        std::uint8_t* bytes;
        unsigned depth;
        static bool slot(void* const context, const SType& type, const SDataReferenceSlot slot) noexcept;
    };
    CDataOperation& m_operation;
    const CDataBlockView* const* m_sources{};
    const SDataCopyName* m_names{};
    std::size_t m_count{}, m_name_count{};
    TVector<TInstance<SDataViewState>> m_states;
    TPodVector<std::uint32_t> m_canonical;
    TPodVector<SMapping> m_mappings;
};

}   // namespace schema::detail
