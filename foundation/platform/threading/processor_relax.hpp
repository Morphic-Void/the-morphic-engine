
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   processor_relax.hpp
//  Author: Ritchie Brannan
//  Date:   7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Issues a processor-local spin-wait hint.
//
//  This function is for short bounded spin loops before falling back to a
//  real blocking primitive. It may reduce power use, pipeline pressure, or
//  sibling-thread contention on some architectures.
//
//  This is not a scheduler yield, sleep, wait, or synchronization operation.
//  It does not make another thread run, does not block the calling thread,
//  and does not provide ordering or progress guarantees.

#pragma once

#ifndef PROCESSOR_RELAX_HPP_INCLUDED
#define PROCESSOR_RELAX_HPP_INCLUDED

namespace platform::threading
{

void processor_relax() noexcept;

}   //  namespace platform::threading

#endif  //  #ifndef PROCESSOR_RELAX_HPP_INCLUDED
