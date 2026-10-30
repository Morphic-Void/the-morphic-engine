
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    texture_requests.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    30 Oct 26
//
//  Dispatch stable texture jobs and collect their Rendering-local results.

#include "runtime/texture_requests.hpp"
#include "debug/macros.hpp"

namespace rendering
{
std::int32_t CTextureRequests::reserve(const std::int32_t correlation, const bool encoding) noexcept
{
    const auto slot = (m_next < 0x80000000u) ? m_requests.emplace() : -1;
    if (slot < 0)
    {
        STextureResult result;
        result.encoding = encoding;
        m_complete(m_client, correlation, std::move(result));
        return -1;
    }
    auto& pending = *m_requests.get_object(slot);
    pending.correlation = correlation;
    pending.encoding = encoding;
    pending.identity = m_next++;
    return slot;
}

bool CTextureRequests::submit(const TextureEncodeWork& input, const std::int32_t correlation) noexcept
{
    if ((input.request == nullptr) || (correlation < 0)) return false;
    const auto slot = reserve(correlation, true);
    if (slot < 0) return true;
    auto& pending = *m_requests.get_object(slot);
    const auto& request = *input.request;
    if (!pending.encode.emplace(image::texture::CInputView{ request.input.const_view(), request.format, request.transfer }, request.options))
        finish(slot, false);
    else dispatch(slot, &CTextureEncodeJob::execute, pending.encode.operator->());
    return true;
}

bool CTextureRequests::submit(const TextureDecodeWork& input, const std::int32_t correlation) noexcept
{
    if ((input.request == nullptr) || (input.input == nullptr) || (correlation < 0)) return false;
    const auto slot = reserve(correlation, false);
    if (slot < 0) return true;
    auto& pending = *m_requests.get_object(slot);
    if (!pending.decode.emplace(*input.input, input.request->target, input.request->level)) finish(slot, false);
    else dispatch(slot, &CTextureDecodeJob::execute, pending.decode.operator->());
    return true;
}

void CTextureRequests::dispatch(const std::int32_t slot, const threading::FBatchWork function, void* const object) noexcept
{
    const auto submission = m_batch.submit(function, object, m_requests.get_object(slot)->identity);
    if (submission.status == threading::EBatchSubmission::queued)
    {
        MV_LOG(detail, "Rendering: Texture batch queued, slot {}", slot);
        return;
    }
    if (submission.status == threading::EBatchSubmission::completed_inline)
    {
        MV_LOG(detail, "Rendering: Texture batch completed inline, slot {}", slot);
        finish(slot, true);
        return;
    }
    MV_LOG(error, "Rendering: Texture batch rejected, slot {}, reason {}", slot, static_cast<unsigned int>(submission.reason));
    finish(slot, false);
}

bool CTextureRequests::complete(const threading::SBatchResponse& response) noexcept
{
    for (auto slot = m_requests.first_live(); slot >= 0; slot = m_requests.next_live(slot))
    {
        if (m_requests.get_object(slot)->identity == response.correlation)
        {
            MV_LOG(detail, "Rendering: Texture batch completion, slot {}, outcome {}", slot, static_cast<unsigned int>(response.completion));
            finish(slot, response.completion == threading::EBatchCompletion::executed);
            return true;
        }
    }
    return false;
}

void CTextureRequests::finish(const std::int32_t slot, const bool executed) noexcept
{
    auto& pending = *m_requests.get_object(slot);
    const auto correlation = pending.correlation;
    STextureResult result;
    result.encoding = pending.encoding;
    if (pending.encode)
    {
        result.status = executed ? pending.encode->status() : ETextureStatus::codec_failed;
        if (executed) result.encoded = pending.encode->take_output();
    }
    if (pending.decode)
    {
        result.status = executed ? pending.decode->status() : ETextureStatus::codec_failed;
        if (executed) result.decoded = pending.decode->take_output();
    }
    if ((result.status == ETextureStatus::success) &&
        !(result.encoding ? result.encoded.view().is_ready() : result.decoded.view().is_ready())) result.status = ETextureStatus::codec_failed;
    (void)m_requests.erase(slot);
    m_complete(m_client, correlation, std::move(result));
}
}
