
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_reconstruction.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Oct 26
//
//  Internal binary-to-document reconstruction for bulk data import into CSchema.

#pragma once

#ifndef SCHEMA_DATA_RECONSTRUCTION_HPP_INCLUDED
#define SCHEMA_DATA_RECONSTRUCTION_HPP_INCLUDED

#include "schema/baked_bulk_data.hpp"

namespace schema::detail
{

[[nodiscard]] bool reconstruct_bulk_records(CLiveDocument& document, const CResolvedTypes& schema,
    const CNodeKey entry, const CTypesIndex type, const std::uint8_t* const bytes,
    const std::uint32_t count, EBulkDataLoadReason& reason,
    const unsigned depth_limit = UINT32_MAX) noexcept;

}   // namespace schema::detail

#endif // SCHEMA_DATA_RECONSTRUCTION_HPP_INCLUDED
