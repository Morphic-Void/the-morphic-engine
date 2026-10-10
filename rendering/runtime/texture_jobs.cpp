
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    texture_jobs.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Oct 26
//
//  Typed codec operations and batch callback entry points.

#include "rendering/runtime/texture_jobs.hpp"

namespace rendering
{

void CTextureEncodeJob::run() noexcept
{
    m_status = basis_codec::encode_ktx2(m_input, m_options, m_output);
}

void MV_STD_ABI_CALL CTextureEncodeJob::execute(void* const object) noexcept
{
    static_cast<CTextureEncodeJob*>(object)->run();
}

void CTextureDecodeJob::run() noexcept
{
    m_status = basis_codec::transcode_ktx2(m_input, m_target, m_level, m_output);
}

void MV_STD_ABI_CALL CTextureDecodeJob::execute(void* const object) noexcept
{
    static_cast<CTextureDecodeJob*>(object)->run();
}

}   //  namespace rendering
