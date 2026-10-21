
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    local_types.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Aug 26
//
//  MorphicTests-local runtime types and shared Host protocol declarations.
//  This binary binds the protocols in its own catalogue, independently of Host.

#pragma once

#ifndef MORPHIC_TEST_LOCAL_TYPES_HPP_INCLUDED
#define MORPHIC_TEST_LOCAL_TYPES_HPP_INCLUDED

#include <cstdint>

#include "assets/asset_repository.hpp"

#include "host/runtime/host_payloads.hpp"
#include "image/image_messages.hpp"

namespace test_environment
{

class CTestRuntime;

struct STestAssetLoadState
{
    std::int32_t executive_slot;
    CAssetId request;
};

struct STestAssetSaveState
{
    std::int32_t executive_slot;
    CAssetId encoded_file;
    CAssetId request;
};

}   //  namespace test_environment

#endif  //  MORPHIC_TEST_LOCAL_TYPES_HPP_INCLUDED
