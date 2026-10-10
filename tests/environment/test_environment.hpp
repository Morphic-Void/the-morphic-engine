//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)

#pragma once

#ifndef MORPHIC_TEST_ENVIRONMENT_HPP_INCLUDED
#define MORPHIC_TEST_ENVIRONMENT_HPP_INCLUDED

#include "system/system_id_registry.hpp"

namespace memory
{
class CMemoryContext;
}

namespace test_environment
{

[[nodiscard]] bool install() noexcept;
[[nodiscard]] bool is_clean() noexcept;
[[nodiscard]] memory::CMemoryContext* executable_memory_context() noexcept;
[[nodiscard]] memory::CMemoryContext* executive_memory_context() noexcept;
[[nodiscard]] memory::CMemoryContext* rendering_memory_context() noexcept;
[[nodiscard]] const system_id_registry::SSystemRegistryView& system_registry_view() noexcept;

}   //  namespace test_environment

#endif  //  MORPHIC_TEST_ENVIRONMENT_HPP_INCLUDED
