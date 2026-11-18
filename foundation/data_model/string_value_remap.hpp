
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    string_value_remap.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Owned live-to-baked string index mapping for one successful document bake.

#pragma once

#ifndef STRING_VALUE_REMAP_HPP_INCLUDED
#define STRING_VALUE_REMAP_HPP_INCLUDED

#include "containers/TPodVector.hpp"

#include <cstdint>
#include <utility>

namespace document_translation
{

//  An operation-owned map for exactly one successful bake of an unchanged source.
//  Detached/unretained nonempty strings have no mapping. IDs are context-local;
//  callers must discard the map when either document's string domain changes.
class CStringValueRemap
{
public:
    CStringValueRemap() noexcept = default;
    CStringValueRemap(CStringValueRemap&&) noexcept = default;
    CStringValueRemap& operator=(CStringValueRemap&&) noexcept = default;
    CStringValueRemap(const CStringValueRemap&) = delete;
    CStringValueRemap& operator=(const CStringValueRemap&) = delete;

    //  Take ownership of a completed live-index-to-baked-index table. Entry zero
    //  must be zero; other zero entries mark unretained strings. The caller must
    //  supply indices for the intended source and baked string domains.
    void initialise(TPodVector<std::uint32_t>&& indices) noexcept
    {
        m_ids = std::move(indices);
    }

    //  Checked bridge for fixed-width persisted slots; invalid and unretained
    //  positive indices reject without manufacturing a public string identity.
    [[nodiscard]] bool translate_index(const std::uint32_t source, std::uint32_t& destination) const noexcept
    {
        if ((source >= m_ids.size()) || ((source != 0u) && (m_ids[source] == 0u)))
        {
            return false;
        }
        destination = m_ids[source];
        return true;
    }

    void clear() noexcept { m_ids.deallocate(); }

private:
    TPodVector<std::uint32_t> m_ids;
};

}   // namespace document_translation

#endif // STRING_VALUE_REMAP_HPP_INCLUDED
