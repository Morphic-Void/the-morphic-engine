
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    unused_storage.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Oct 26
//
//  Explicit clearing of schema-unaddressable storage in writable buffers.

#pragma once

#ifndef SCHEMA_UNUSED_STORAGE_HPP_INCLUDED
#define SCHEMA_UNUSED_STORAGE_HPP_INCLUDED

#include "schema/resolved_schema.hpp"

namespace schema
{

enum class EUnusedBits : std::uint8_t { preserve = 0u, clear };

//  Standalone values must occupy whole strides and complete aggregate values.
//  No byte is changed on failure.
[[nodiscard]] bool clear_unused_storage(const CResolvedSchema& schema, const CSchemaIndex type, const CByteView values, const EUnusedBits bits = EUnusedBits::preserve) noexcept;

//  The supplied writable view defines the used payload range. Locator metadata
//  identifies occupied extents; every other byte in the view is cleared.
[[nodiscard]] bool clear_unused_storage(const CResolvedSchema& schema, const CInstanceDocumentQuery& document, const CByteView payload, const EUnusedBits bits = EUnusedBits::preserve) noexcept;
[[nodiscard]] bool clear_unused_storage(const CResolvedSchema& schema, const CBulkDocumentQuery& document, const CByteView payload, const EUnusedBits bits = EUnusedBits::preserve) noexcept;

}   // namespace schema

#endif // SCHEMA_UNUSED_STORAGE_HPP_INCLUDED
