
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_attachment.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Private storage-attachment preconditions.

#pragma once

#ifndef SCHEMA_DATA_BLOCK_ATTACHMENT_HPP_INCLUDED
#define SCHEMA_DATA_BLOCK_ATTACHMENT_HPP_INCLUDED

#include "schema/data_block_format.hpp"


namespace schema::detail
{

static inline bool data_block_precondition(SDataBlockDiagnostic& diagnostic, const EDataBlockReason reason) noexcept
{
    diagnostic = {};
    diagnostic.stage = EDataBlockStage::preconditions;
    diagnostic.reason = reason;
    return false;
}

}   // namespace schema::detail

#endif // SCHEMA_DATA_BLOCK_ATTACHMENT_HPP_INCLUDED
