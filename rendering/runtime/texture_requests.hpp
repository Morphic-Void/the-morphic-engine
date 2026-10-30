
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    texture_requests.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    30 Oct 26
//
//  Rendering-local typed texture jobs. Callers retain borrowed input until the
//  completion callback; no texture contract crosses the Host transport.

#pragma once

#include "runtime/texture_jobs.hpp"
#include "texture/texture_requests.hpp"
#include "containers/TUnorderedCollection.hpp"
#include "containers/TInstance.hpp"
#include "threading/CBatchWork.hpp"

namespace rendering
{
struct STextureResult
{
    image::texture::CEncodedTexture encoded;
    image::texture::CDecodedTexture decoded;
    ETextureStatus status{ ETextureStatus::allocation_failed };
    bool encoding{ false };
};

class CTextureRequests final
{
public:
    using FComplete = void(*)(void*, std::int32_t, STextureResult&&) noexcept;
    CTextureRequests(threading::CBatchClient& batch, void* client, FComplete complete) noexcept :
        m_batch{ batch }, m_client{ client }, m_complete{ complete } {}
    [[nodiscard]] bool initialise() noexcept { return m_requests.initialise(); }
    [[nodiscard]] bool pending() const noexcept { return !m_requests.is_empty(); }
    [[nodiscard]] bool submit(const TextureEncodeWork& input, std::int32_t correlation) noexcept;
    [[nodiscard]] bool submit(const TextureDecodeWork& input, std::int32_t correlation) noexcept;
    [[nodiscard]] bool complete(const threading::SBatchResponse& response) noexcept;
private:
    struct SRequest
    {
        TInstance<CTextureEncodeJob> encode;
        TInstance<CTextureDecodeJob> decode;
        std::int32_t correlation{ -1 };
        std::uint32_t identity{ 0u };
        bool encoding{ false };
    };
    [[nodiscard]] std::int32_t reserve(std::int32_t correlation, bool encoding) noexcept;
    void dispatch(std::int32_t slot, threading::FBatchWork function, void* object) noexcept;
    void finish(std::int32_t slot, bool executed) noexcept;
    threading::CBatchClient& m_batch;
    void* m_client;
    FComplete m_complete;
    std::uint32_t m_next{ 1u };
    TUnorderedCollection<SRequest> m_requests;
};
}
