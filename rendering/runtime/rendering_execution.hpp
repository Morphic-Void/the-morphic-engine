
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    rendering_execution.hpp
//  Authors: Ritchie Brannan / OpenAI tools
//  Date:    21 Sep 26
//
//  Common execution descriptor for the module-owned Rendering context.

#pragma once

#ifndef RENDERING_EXECUTION_HPP_INCLUDED
#define RENDERING_EXECUTION_HPP_INCLUDED

#include "framework/module/module_execution.hpp"

namespace rendering
{

[[nodiscard]] modules::SExecutionV1 rendering_execution() noexcept;

}   //  namespace rendering

#endif  //  #ifndef RENDERING_EXECUTION_HPP_INCLUDED
