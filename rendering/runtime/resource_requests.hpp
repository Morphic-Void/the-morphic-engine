
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    resource_requests.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    30 Oct 26
//
//  Rendering owns typed requests and outcomes. Host sees only generic asset
//  acquisitions and releases. Results transfer to their Rendering caller.

#pragma once

#include "framework/threading/messages/CErasedPodMsg.hpp"
#include "framework/module/module_execution.hpp"

#include "framework/assets/asset_messages.hpp"
#include "runtime/texture_requests.hpp"
#include "runtime/shader_requests.hpp"

namespace rendering
{

enum class EResourceKind : std::uint8_t { none = 0u, encoded_texture, decoded_texture, shader_group };

struct SResourceResult
{
    EAssetStatus status{ EAssetStatus::allocation_failed };
    EResourceKind kind{ EResourceKind::none };
    image::texture::CEncodedTexture encoded;
    image::texture::CDecodedTexture decoded;
    shader::CCompileGroup shaders;
    [[nodiscard]] image::texture::CEncodedView encoded_texture_view() const noexcept { return encoded.view(); }
    [[nodiscard]] image::texture::CDecodedView decoded_texture_view() const noexcept { return decoded.view(); }
};

struct SAssetChannel
{
    void* context{ nullptr };
    modules::ESubmission(*post)(void* const, const threading::CErasedPodMsg&) noexcept{ nullptr };
};

class CResourceRequests final
{
public:
    CResourceRequests(threading::CBatchClient& batch, SAssetChannel assets) noexcept :
        m_assets{ assets }, m_textures{ batch, this, &texture_complete }, m_shaders{ batch, this, &shader_complete } {}
    [[nodiscard]] bool initialise() noexcept { return m_operations.initialise() && m_textures.initialise() && m_shaders.initialise(); }
    [[nodiscard]] bool submit(TextureEncodeRequest&& request, std::int32_t correlation) noexcept;
    [[nodiscard]] bool submit(const TextureDecodeRequest& request, std::int32_t correlation) noexcept;
    [[nodiscard]] bool submit(ShaderCompileRequest&& request, std::int32_t correlation) noexcept;
    [[nodiscard]] bool complete(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool complete(const threading::SBatchResponse& response) noexcept;
    [[nodiscard]] bool receive(SResourceResult& result, std::int32_t& correlation) noexcept;
    [[nodiscard]] bool pending() const noexcept { return !m_operations.is_empty(); }
    [[nodiscard]] bool jobs_pending() const noexcept { return m_textures.pending() || m_shaders.pending(); }
    [[nodiscard]] bool failed() const noexcept { return m_failed; }
    //  A bounded pass schedules continuation only while publication succeeds.
    //  Full output waits for the facade's capacity notification.
    [[nodiscard]] bool progress() noexcept;

    //  These positive slots cannot collide with test/asset-publication slots.
    static constexpr std::int32_t k_first_slot = 0x10000000;

private:
    enum class EPhase : std::uint8_t { acquire_pending, acquiring, running, release_pending, releasing, ready };
    struct SOperation
    {
        TextureEncodeRequest encode;
        TextureDecodeRequest decode;
        ShaderCompileRequest compile;
        AssetAcquireResult acquired;
        SResourceResult result;
        std::int32_t identity{ -1 };
        std::int32_t correlation{ -1 };
        EResourceKind kind{ EResourceKind::none };
        EPhase phase{ EPhase::acquiring };
        CAssetId source;
        EAssetAcquireView view{};
    };
    [[nodiscard]] SOperation* reserve(std::int32_t correlation, EResourceKind kind) noexcept;
    [[nodiscard]] SOperation* find(std::int32_t identity) noexcept;
    void acquire(SOperation& operation, CAssetId source, EAssetAcquireView view) noexcept;
    void finish(SOperation& operation) noexcept;
    [[nodiscard]] bool publish(SOperation& operation) noexcept;
    static void texture_complete(void* self, std::int32_t identity, STextureResult&& result) noexcept;
    static void shader_complete(void* self, std::int32_t identity, ShaderCompileResult&& result) noexcept;
    SAssetChannel m_assets;
    TUnorderedCollection<SOperation> m_operations;
    std::int32_t m_next{ k_first_slot };
    std::int32_t m_output_cursor{ -1 };
    bool m_failed{ false };
    CTextureRequests m_textures;
    CShaderRequests m_shaders;
};

}   //  namespace rendering
