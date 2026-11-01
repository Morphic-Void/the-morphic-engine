
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    audio_thread.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Audio service lifecycle and generic test protocol.

#pragma once
#include "audio_run.hpp"
#include "threading/CThreadPackage.hpp"

namespace audio
{
platform::threading::FThreadEntry audio_thread_entry_point() noexcept;
std::uint32_t serve_audio(threading::CThreadResources& resources, IAudioOutput& output) noexcept;
}   //  namespace audio
