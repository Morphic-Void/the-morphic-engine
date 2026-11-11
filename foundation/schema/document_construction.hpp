
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_construction.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Oct 26
//
//  Internal sorted string preparation and admitted subtree construction.

#pragma once

#ifndef SCHEMA_DOCUMENT_CONSTRUCTION_HPP_INCLUDED
#define SCHEMA_DOCUMENT_CONSTRUCTION_HPP_INCLUDED

#include "schema/document_query.hpp"
#include "containers/TPodVector.hpp"

namespace schema::detail
{

enum class EDocumentBuildReason : std::uint8_t
{
    none = 0u,
    allocation_failed,
    storage_limit,
    construction_failed
};

enum class EDocumentBuildStage : std::uint8_t { preparation = 0u, copying };

struct SDocumentBuildDiagnostic
{
    EDocumentBuildReason reason{ EDocumentBuildReason::none };
    SOccurrence occurrence;
    EDocumentBuildStage stage{ EDocumentBuildStage::preparation };
};

struct SDocumentSection
{
    const CDocumentRead* source{};
    SOccurrence section;
};

//  Copy admitted sections in caller-supplied order. Absent sections are skipped;
//  required-section admission and publication remain the caller's responsibility.
[[nodiscard]] bool copy_document_sections(const SDocumentSection* sections, const std::size_t count,
    CLiveDocument& target, SDocumentBuildDiagnostic& diagnostic) noexcept;

class CDocumentStrings
{
public:
    [[nodiscard]] bool name(const CStringView& text) noexcept;
    [[nodiscard]] bool string(const CStringView& text) noexcept;
    [[nodiscard]] bool collect(const CDocumentRead& source, const SOccurrence value, const unsigned depth) noexcept;
    [[nodiscard]] bool initialise(CLiveDocument& target, const std::uint64_t structural_nodes) noexcept;
    [[nodiscard]] bool preload(CLiveDocument& target) noexcept;
    [[nodiscard]] const SDocumentBuildDiagnostic& diagnostic() const noexcept { return m_error; }

private:
    [[nodiscard]] bool fail(const EDocumentBuildReason reason, const SOccurrence value = {}) noexcept;
    [[nodiscard]] bool add(TPodVector<CStringView>& values, const CStringView& text) noexcept;
    [[nodiscard]] bool preload_domain(CLiveDocument& target, TPodVector<CStringView>& values, const bool names) noexcept;
    TPodVector<CStringView> m_names, m_strings;
    std::uint64_t m_node_count{};
    SDocumentBuildDiagnostic m_error;
};

//  Source invariants establish unique names and newly constructed disjoint trees.
[[nodiscard]] CNodeKey copy_document_subtree(const CDocumentRead& source, const SOccurrence value,
    CLiveDocument& target, const unsigned depth, SDocumentBuildDiagnostic& diagnostic) noexcept;

}   // namespace schema::detail

#endif // SCHEMA_DOCUMENT_CONSTRUCTION_HPP_INCLUDED
