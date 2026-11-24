
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    executive_execution.hpp
//  Authors: Ritchie Brannan / OpenAI tools
//  Date:    7 Aug 26
//
//  Common execution descriptor for the module-owned Executive context.

#pragma once

#ifndef EXECUTIVE_EXECUTION_HPP_INCLUDED
#define EXECUTIVE_EXECUTION_HPP_INCLUDED

#include "framework/module/module_execution.hpp"

namespace executive
{

modules::SExecutionV1 executive_execution() noexcept;

}   //  namespace executive

#endif  //  #ifndef EXECUTIVE_EXECUTION_HPP_INCLUDED
