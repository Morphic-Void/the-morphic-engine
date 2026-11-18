
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    reference_construction.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Private destination string interning and named collection binding.

#pragma once

#include "schema/reference_value.hpp"

namespace document_translation { class CStringValueRemap; }

namespace schema::detail
{

class CDataOperation;

class CReferenceConstruction
{
public:
    CReferenceConstruction(CLiveDocument& description, CDataOperation& operation) noexcept :
        m_document(description), m_operation(operation) {}
    [[nodiscard]] bool add_collection(const CTypesIndex type, const CNodeKey entry,
        const std::uint32_t id, const std::uint32_t count) noexcept;
    [[nodiscard]] bool finish() noexcept;
    [[nodiscard]] SReferenceEncoding encoding() noexcept { return { this, &encode }; }
    [[nodiscard]] bool relocate_text(const CResolvedTypes& types, const CTypesIndex type,
        std::uint8_t* const bytes, const document_translation::CStringValueRemap& mapping) noexcept;
    [[nodiscard]] bool intern(const CStringView& text, std::uint32_t& id) noexcept;
    [[nodiscard]] bool lookup(const std::uint32_t id, CStringView& text) noexcept;

private:
    struct SCollection { CTypesIndex type; CNodeKey entry; std::uint32_t id{}, count{}; };
    static bool encode(void* const context, const SType& type, const SReferenceValue& value,
        std::uint8_t* const destination, EReason& reason) noexcept;
    CLiveDocument& m_document;
    CDataOperation& m_operation;
    TPodVector<SCollection> m_collections;
    TPodVector<CNodeKey> m_strings, m_by_id;
};

}   // namespace schema::detail
