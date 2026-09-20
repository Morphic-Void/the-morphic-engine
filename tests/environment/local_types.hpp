//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  MorphicTests-local runtime types. These fixtures deliberately model the
//  properties required by Core tests without depending on Host-local types.

#pragma once

#ifndef MORPHIC_TEST_LOCAL_TYPES_HPP_INCLUDED
#define MORPHIC_TEST_LOCAL_TYPES_HPP_INCLUDED

#include <cstdint>

#include "assets/asset_repository.hpp"

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
