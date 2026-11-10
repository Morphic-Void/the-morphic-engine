
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_trace.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Nov 26
//
//  Acceptance-build origin carried through private resumable observations.

#pragma once
#if defined(MV_INPUT_ACCEPTANCE)
#include <cstdint>
namespace platform::acceptance
{
struct SObservationTrace
{
    std::uint64_t observation{}, offered{}, ingress{};
};
}
#endif
