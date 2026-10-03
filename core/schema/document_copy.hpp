
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_copy.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Internal value copying for schema creation and editing.

#pragma once

#ifndef SCHEMA_DOCUMENT_COPY_HPP_INCLUDED
#define SCHEMA_DOCUMENT_COPY_HPP_INCLUDED

#include "schema/document_query.hpp"
#include "containers/ByteBuffers.hpp"

namespace schema
{

//  Stabilise caller-supplied names before an operation can grow their document.
[[nodiscard]] bool stabilise_document_name(const CStringView& source, CByteBuffer& storage, CStringView& copied) noexcept;

//  Copy a detached subtree from either representation, including within the same
//  live document. Preserve metadata and flags; erase partial nodes on failure.
//  Depth is shared with the caller's enclosing traversal and is bounded at 256.
[[nodiscard]] CNodeKey copy_document_value(CLiveDocument& target, const detail::CDocumentRead& source,
    const detail::SOccurrence value, const CStringView& name, const unsigned depth = 0u) noexcept;

}   // namespace schema

#endif // SCHEMA_DOCUMENT_COPY_HPP_INCLUDED
