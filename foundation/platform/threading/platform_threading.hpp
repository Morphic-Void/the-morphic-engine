
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   platform_threading.hpp
//  Author: Ritchie Brannan
//  Date:   7 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Convenience include for the small low-level native/platform
//  threading primitive wrappers.
//
//  Include headers when a narrower dependency is preferred.

#pragma once

#ifndef PLATFORM_THREADING_HPP_INCLUDED
#define PLATFORM_THREADING_HPP_INCLUDED

#include "platform/threading/exclusive_lock.hpp"
#include "platform/threading/hw_thread_count.hpp"
#include "platform/threading/native_thread_id.hpp"
#include "platform/threading/processor_relax.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "platform/threading/thread_naming.hpp"
#include "platform/threading/thread_priority.hpp"
#include "platform/threading/wait_word.hpp"
#include "platform/threading/wait_word_semaphore.hpp"

#endif  //  #ifndef PLATFORM_THREADING_HPP_INCLUDED
