
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_output.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Document content choice for schema data output.

#pragma once

#ifndef SCHEMA_DATA_OUTPUT_HPP_INCLUDED
#define SCHEMA_DATA_OUTPUT_HPP_INCLUDED

#include <cstdint>

namespace schema
{

enum class EDataOutputForm : std::uint8_t { embedded, external };

}   // namespace schema

#endif // SCHEMA_DATA_OUTPUT_HPP_INCLUDED
