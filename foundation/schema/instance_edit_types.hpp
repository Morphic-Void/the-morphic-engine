
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    instance_edit_types.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Shared selection paths and descendant policy for instance editing.

#pragma once

#ifndef SCHEMA_INSTANCE_EDIT_TYPES_HPP_INCLUDED
#define SCHEMA_INSTANCE_EDIT_TYPES_HPP_INCLUDED

#include "containers/StringBuffers.hpp"

#include <cstdint>

namespace schema
{

enum class EInstanceDescendantPolicy : std::uint8_t { preserve = 0u, propagate };

enum class EInstanceSelectionStepKind : std::uint8_t { member = 0u, element };

//  Member steps name structure members or bit fields; element steps use zero-based
//  fixed-array indices. An empty path addresses the whole instance. Binary field
//  input is the unshifted logical scalar, with valid range and sign extension.
struct SInstanceSelectionStep
{
    EInstanceSelectionStepKind kind{ EInstanceSelectionStepKind::member };
    CStringView member;
    std::uint32_t element{};
};

}   // namespace schema

#endif // SCHEMA_INSTANCE_EDIT_TYPES_HPP_INCLUDED
