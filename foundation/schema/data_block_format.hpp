
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_format.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Internal persistence envelope validation; not a public block loading API.

#pragma once

#ifndef SCHEMA_DATA_BLOCK_FORMAT_HPP_INCLUDED
#define SCHEMA_DATA_BLOCK_FORMAT_HPP_INCLUDED

#include "schema/data_block.hpp"

namespace schema::detail
{

constexpr std::size_t k_data_block_alignment = 128u;
constexpr std::uint64_t k_data_block_version = 2u;

enum class EDataDomain : std::uint8_t { instances = 0u, bulk_data };

struct SDataBlockRoot
{
    SOccurrence types, instances, bulk_data;
    SOccurrence instance_size, bulk_size;
    std::uint64_t instance_bytes{}, bulk_bytes{};
    bool stripped{};
};

struct SDataBlockLayout
{
    std::size_t instance_offset{}, bulk_offset{}, file_size{};
};

struct SDataBlockView
{
    CBakedDocument document;
    CByteConstView instances, bulk_data;
    bool has_instances{}, has_bulk_data{}, stripped{};
};

//  Checked arithmetic independent of storage, shared with assembly.
//  Resets outputs on entry; zero payload has no alignment tail or dummy range.
[[nodiscard]] bool read_data_block_root(const CDocumentRead& document, SDataBlockRoot& output,
    SDataBlockDiagnostic& diagnostic, const bool require_envelope = false) noexcept;
[[nodiscard]] bool data_block_layout(const std::uint64_t document_size, const std::uint64_t instance_size,
    const std::uint64_t bulk_size, SDataBlockLayout& layout, EDataBlockReason& reason) noexcept;

//  Caller supplies readable storage for the complete range and preserves it for
//  borrowed views. Checks the envelope only; typed entry semantics belong to the
//  existing roles. Resets output/diagnostic on entry and publishes only on success.
//  Baked validation reports only bool: invalid_document means validation failed,
//  without claiming to distinguish malformed content from its scratch failure.
[[nodiscard]] bool validate_data_block(const void* bytes, const std::size_t size,
    SDataBlockView& output, SDataBlockDiagnostic& diagnostic) noexcept;

}   // namespace schema::detail

#endif // SCHEMA_DATA_BLOCK_FORMAT_HPP_INCLUDED
