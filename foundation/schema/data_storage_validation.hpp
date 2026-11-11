
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_storage_validation.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Oct 26
//
//  Internal locator parsing and range checks shared with storage admission.

#pragma once

#ifndef SCHEMA_DATA_STORAGE_VALIDATION_HPP_INCLUDED
#define SCHEMA_DATA_STORAGE_VALIDATION_HPP_INCLUDED

#include "schema/document_query.hpp"
#include "memory/memory_policies.hpp"

namespace schema::detail
{

struct SDataLocator
{
    std::uint64_t offset{}, count{}, size{};
    SOccurrence offset_node, valid_node, count_node, size_node;
    bool valid{};
};

template<class TReason>
[[nodiscard]] static bool read_data_locator(const CDocumentRead& document, const SOccurrence locator,
    const bool bulk, const std::uint64_t record_size, SDataLocator& result,
    TReason& reason, SOccurrence& occurrence) noexcept
{
    result = {};
    const auto fail = [&](const TReason why, const SOccurrence value) noexcept
    {
        reason = why;
        occurrence = value;
        return false;
    };
    for (SOccurrence property = document.first_child(locator); property; property = document.next_sibling(property))
    {
        const CStringView name = document.name(property);
        if (name == CStringView{ "offset" }) result.offset_node = property;
        else if (name == CStringView{ "valid" }) result.valid_node = property;
        else if (name == CStringView{ "count" }) result.count_node = property;
        else if (name == CStringView{ "size" }) result.size_node = property;
        else
        {
            return fail(TReason::unknown_property, property);
        }
    }
    if (!result.offset_node || !result.valid_node ||
        !document.unsigned_integer_value(result.offset_node, result.offset) || (result.offset > UINT32_MAX) ||
        !document.boolean_value(result.valid_node, result.valid))
    {
        return fail(TReason::invalid_locator, locator);
    }
    if (result.count_node && (!document.unsigned_integer_value(result.count_node, result.count) ||
        (bulk ? ((result.count == 0u) || (result.count > UINT32_MAX)) : (result.count != 1u))))
    {
        return fail(TReason::invalid_count, result.count_node);
    }
    if (result.size_node && (!document.unsigned_integer_value(result.size_node, result.size) ||
        (bulk ? (result.size > memory::k_byte_size_ceiling) : (result.size != record_size))))
    {
        return fail(TReason::invalid_range, result.size_node);
    }
    return true;
}

template<class TReason>
[[nodiscard]] static bool check_data_range(const std::uint64_t offset, const std::uint64_t extent,
    const std::uint64_t alignment, const std::uint64_t cursor,
    const std::size_t payload_size, TReason& reason) noexcept
{
    if ((offset < cursor) && ((extent != 0u) || (offset != 0u)))
    {
        reason = TReason::overlap;
        return false;
    }
    if ((offset > UINT32_MAX) || (offset > memory::k_byte_size_ceiling) ||
        ((offset & (alignment - 1u)) != 0u) || (extent > (memory::k_byte_size_ceiling - offset)) ||
        ((offset + extent) > payload_size))
    {
        reason = TReason::invalid_range;
        return false;
    }
    return true;
}

}   // namespace schema::detail

#endif // SCHEMA_DATA_STORAGE_VALIDATION_HPP_INCLUDED
