
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   transports.hpp
//  Author: Ritchie Brannan
//  Date:   26 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Convenience include for the platform agnostic threading transports.
//
//  Include individual headers when a narrower dependency is preferred.

#pragma once

#ifndef TRANSPORTS_HPP_INCLUDED
#define TRANSPORTS_HPP_INCLUDED

#include "TBulkTransport.hpp"
#include "TMpmcTransport.hpp"
#include "TOwningTransport.hpp"
#include "TQueueTransport.hpp"
#include "TRingTransport.hpp"

#endif  //  #ifndef TRANSPORTS_HPP_INCLUDED
