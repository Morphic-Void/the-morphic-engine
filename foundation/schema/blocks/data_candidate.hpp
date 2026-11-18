
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_candidate.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Private stable-entry candidate shared by copy, repack and edit transactions.

#pragma once

#include "schema/blocks/data_reference_context.hpp"
#include "schema/blocks/reference_construction.hpp"
#include "schema/values/instance_selection.hpp"
#include "containers/TVector.hpp"

namespace schema::detail
{

struct SDataCandidateEntry
{
    CNodeKey metadata, selection;
    CTypesIndex type;
    EDataEntryKind kind{};
    std::uint32_t parent{ UINT32_MAX }, count{};
    bool alive{}, forward{};
    CByteBuffer bytes;
};

class CDataCandidate
{
public:
    CDataCandidate(CDataOperation& budget, const CLiveTypes& catalogue) noexcept :
        operation(budget), types(catalogue), strings(document, budget) {}
    [[nodiscard]] bool initialise() noexcept;
    [[nodiscard]] bool clone(CDataCandidate& destination) noexcept;
    [[nodiscard]] bool append(const CTypesIndex type, const EDataEntryKind kind, const CStringView& name,
        const std::uint32_t parent, const std::uint32_t count, std::uint32_t& index,
        const bool forward = false) noexcept;
    [[nodiscard]] std::uint32_t find(const CTypesIndex type, const EDataEntryKind kind,
        const CStringView& name, const std::uint32_t parent = UINT32_MAX) noexcept;
    [[nodiscard]] bool copy_bytes(SDataCandidateEntry& entry, const CByteConstView source) noexcept;
    [[nodiscard]] bool publish(CBakedDataBlock& output) noexcept;
    [[nodiscard]] bool check_records(const std::uint32_t replacement, const std::uint32_t count) noexcept;
    [[nodiscard]] SReferenceEncoding encoding() noexcept { return { this, &encode }; }
    [[nodiscard]] CNodeKey copy_node(const CDocumentRead& source, const SOccurrence value) noexcept;
    [[nodiscard]] bool admitted_nodes(const CDocumentRead& source, const SOccurrence value,
        const unsigned depth = 0u) noexcept;
    [[nodiscard]] bool error(const EReason reason) noexcept;
    static bool encode(void* const context, const SType& type, const SReferenceValue& value,
        std::uint8_t* const destination, EReason& reason) noexcept;

    CDataOperation& operation;
    const CLiveTypes& types;
    CLiveDocument document;
    CReferenceConstruction strings;
    TVector<SDataCandidateEntry> entries;
    bool present_domains[2]{};
};

}   // namespace schema::detail
