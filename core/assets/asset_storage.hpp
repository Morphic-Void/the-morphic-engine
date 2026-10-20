
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    asset_storage.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Oct 26
//
//  Byte storage shared by Host worker completions and retained raw assets.
//  Identity and owner eligibility belong to the component-local catalogue.

#pragma once

#ifndef ASSET_STORAGE_HPP_INCLUDED
#define ASSET_STORAGE_HPP_INCLUDED

#include "containers/ByteBuffers.hpp"

struct ByteStorage { CByteBuffer buffer; };

#endif  //  ASSET_STORAGE_HPP_INCLUDED
