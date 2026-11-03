
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    storage_type_bindings.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  SYSTEM identities for common buffer and string storage.

#pragma once

#include "containers/ByteBuffers.hpp"
#include "containers/StringBuffers.hpp"
#include "system/system_type_registration.hpp"

MV_REGISTER_SYSTEM_TYPE(CByteBuffer, system_type_ids::byte_buffer);
MV_REGISTER_SYSTEM_TYPE(CByteRectBuffer, system_type_ids::byte_rect_buffer);
MV_REGISTER_SYSTEM_TYPE(CSimpleString, system_type_ids::simple_string);
MV_REGISTER_SYSTEM_TYPE(CStringBuffer, system_type_ids::string_buffer);
MV_REGISTER_SYSTEM_TYPE(CStableStrings, system_type_ids::stable_strings);
