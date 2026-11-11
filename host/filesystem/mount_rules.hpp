
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    mount_rules.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Nov 26
//
//  Host-reserved mount recognition over generic logical path rules.

#pragma once

#include "foundation/filesystem/logical_path.hpp"

namespace host::mount_rules
{

//  Reserved Host mount; never a configured directory or virtual bundle root.
[[nodiscard]] inline bool is_native_mount(const CStringView root) noexcept
{
    return logical_path::case_equal(root, CStringView{ "native:" });
}

}   //  namespace host::mount_rules
