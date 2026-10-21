
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    local_type_ids.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Aug 26

#pragma once

#ifndef MORPHIC_TEST_LOCAL_TYPE_IDS_HPP_INCLUDED
#define MORPHIC_TEST_LOCAL_TYPE_IDS_HPP_INCLUDED

#include "tests/environment/local_types.hpp"
#include "tests/environment/local_type_ids.def"
#include "tests/environment/erased_owner_payloads.def"
#include "system/local_type_ids.hpp.inl"

#undef MV_IMAGE_ERASED_OWNER_PAYLOADS
#undef MV_HOST_ERASED_OWNER_PAYLOADS
#undef MV_HOST_PROTOCOL_TYPES

#endif  //  MORPHIC_TEST_LOCAL_TYPE_IDS_HPP_INCLUDED
