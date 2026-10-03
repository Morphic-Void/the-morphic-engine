
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_output.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Document content choice for schema data output.

//  Shared public output policy, normally included through the live data roles.

#pragma once

#ifndef SCHEMA_DATA_OUTPUT_HPP_INCLUDED
#define SCHEMA_DATA_OUTPUT_HPP_INCLUDED

#include <cstdint>

namespace schema
{

//==============================================================================
//  Output policy
//==============================================================================

//  Stripped output keeps navigation and locators, requiring a supplied payload.
//  Schema definitions and defaults are unaffected by this data-only choice.
enum class EDataOutputForm : std::uint8_t { embedded, external, stripped };

}   // namespace schema

#endif // SCHEMA_DATA_OUTPUT_HPP_INCLUDED
