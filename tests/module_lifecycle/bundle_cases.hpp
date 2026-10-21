//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    bundle_cases.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    12 Oct 26
//
//  Bundle acceptance through real Host queues and Executive DLL transitions.

#pragma once

#include "threading/CThreadPackage.hpp"

namespace module_lifecycle_tests
{
bool bundle_case(threading::CThreadContext& context, threading::CThreadResources& resources, const char* selected) noexcept;
bool rendering_bundle_case(threading::CThreadContext& context, threading::CThreadResources& resources) noexcept;
}
