
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_reconstruction.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Oct 26
//
//  Internal binary-to-document reconstruction shared by roles and preparation.

#pragma once

#ifndef SCHEMA_DATA_RECONSTRUCTION_HPP_INCLUDED
#define SCHEMA_DATA_RECONSTRUCTION_HPP_INCLUDED

#include "schema/baked_instances.hpp"
#include "schema/baked_bulk_data.hpp"

namespace schema::detail
{

//  Baseline is original defaults for a base, or its immediate parent snapshot.
//  An absent result with no error is a valid unchanged declaration.
[[nodiscard]] bool reconstruct_instance(CLiveDocument& document, const CResolvedSchema& schema,
    const CSchemaIndex type, const std::uint8_t* const bytes, const std::uint8_t* const baseline,
    const std::size_t size, const bool base, CNodeKey& result, EInstanceLoadReason& reason,
    const unsigned depth_limit = UINT32_MAX) noexcept;

[[nodiscard]] bool reconstruct_bulk_records(CLiveDocument& document, const CResolvedSchema& schema,
    const CNodeKey entry, const CSchemaIndex type, const std::uint8_t* const bytes,
    const std::uint32_t count, EBulkDataLoadReason& reason,
    const unsigned depth_limit = UINT32_MAX) noexcept;

}   // namespace schema::detail

#endif // SCHEMA_DATA_RECONSTRUCTION_HPP_INCLUDED
