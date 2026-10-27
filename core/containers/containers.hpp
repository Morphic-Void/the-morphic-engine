
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   containers.hpp
//  Author: Ritchie Brannan
//  Date:   15 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Convenience include for the engine containers.
//
//  This header aggregates all the containers.
// 
//  Include individual headers when a narrower dependency is preferred.

#pragma once

#ifndef CONTAINERS_HPP_INCLUDED
#define CONTAINERS_HPP_INCLUDED

#include "containers/BumpBuffer.hpp"
#include "containers/ByteBuffers.hpp"
#include "containers/StringBuffers.hpp"
#include "containers/TInstance.hpp"
#include "containers/TPodFifo.hpp"
#include "containers/TPodVector.hpp"
#include "containers/TPodOrderedSlots.hpp"
#include "containers/TPodUnorderedSlots.hpp"
#include "containers/TOrderedCollection.hpp"
#include "containers/TUnorderedCollection.hpp"

#endif  //  #ifndef CONTAINERS_HPP_INCLUDED
