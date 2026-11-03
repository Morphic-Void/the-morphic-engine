
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    message_errors.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Message transport fallback.

#pragma once

#include "system/system_type_registration.hpp"

namespace threading
{

struct UnrecognisedMsg { system_type_id msg_id; };

}   //  namespace threading

MV_REGISTER_SYSTEM_TYPE(threading::UnrecognisedMsg, system_type_ids::unrecognised_msg);
