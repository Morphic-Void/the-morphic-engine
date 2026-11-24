
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_worker_thread.hpp
//  Authors: Ritchie Brannan / OpenAI tools
//  Date:    7 Aug 26
//
//  Entry point for a host-owned worker thread.

#pragma once

#ifndef HOST_WORKER_THREAD_HPP_INCLUDED
#define HOST_WORKER_THREAD_HPP_INCLUDED

#include "platform/threading/thread_lifetime.hpp"

namespace host
{

platform::threading::FThreadEntry host_worker_thread_entry_point() noexcept;

}   //  namespace host

#endif  //  #ifndef HOST_WORKER_THREAD_HPP_INCLUDED
