
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   messages.hpp
//  Author: Ritchie Brannan
//  Date:   30 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Convenience include for the threading message structures.
//
//  Include individual headers when a narrower dependency is preferred.

#pragma once

#ifndef MESSAGES_HPP_INCLUDED
#define MESSAGES_HPP_INCLUDED

#include "framework/threading/messages/CErasedMessageTransports.hpp"
#include "framework/threading/messages/CErasedOwnerMsg.hpp"
#include "framework/threading/messages/CErasedPodMsg.hpp"
#include "framework/threading/messages/SErasedMsgHeader.hpp"
#include "framework/threading/messages/byte_view_payloads.hpp"

#endif  //  #ifndef MESSAGES_HPP_INCLUDED
