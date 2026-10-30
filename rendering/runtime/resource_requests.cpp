
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    resource_requests.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    30 Oct 26
//
//  Coordinate Rendering-owned resource requests and generic Host acquisitions.

#include "runtime/resource_requests.hpp"
#include "threading/messages/message_helpers.hpp"

namespace rendering
{

CResourceRequests::SOperation* CResourceRequests::reserve(const std::int32_t correlation, const EResourceKind kind) noexcept
{
    if ((correlation < 0) || m_failed || (m_next == INT32_MAX)) return nullptr;
    const auto slot = m_operations.emplace();
    auto* operation = (slot >= 0) ? m_operations.get_object(slot) : nullptr;
    if (operation != nullptr)
    {
        operation->identity = m_next++;
        operation->correlation = correlation;
        operation->kind = kind;
    }
    return operation;
}

CResourceRequests::SOperation* CResourceRequests::find(const std::int32_t identity) noexcept
{
    for (auto slot = m_operations.first_live(); slot >= 0; slot = m_operations.next_live(slot))
        if (m_operations.get_object(slot)->identity == identity) return m_operations.get_object(slot);
    return nullptr;
}

bool CResourceRequests::submit(TextureEncodeRequest&& request, const std::int32_t correlation) noexcept
{
    auto* operation = reserve(correlation, EResourceKind::encoded_texture);
    if (operation == nullptr) return false;
    operation->encode = std::move(request);
    operation->phase = EPhase::running;
    return m_textures.submit(TextureEncodeWork{ &operation->encode }, operation->identity);
}

bool CResourceRequests::submit(const TextureDecodeRequest& request, const std::int32_t correlation) noexcept
{
    auto* operation = reserve(correlation, EResourceKind::decoded_texture);
    if (operation == nullptr) return false;
    operation->decode = request;
    acquire(*operation, request.source, EAssetAcquireView::bytes);
    return true;
}

bool CResourceRequests::submit(ShaderCompileRequest&& request, const std::int32_t correlation) noexcept
{
    auto* operation = reserve(correlation, EResourceKind::shader_group);
    if (operation == nullptr) return false;
    operation->compile = std::move(request);
    acquire(*operation, operation->compile.bundle, EAssetAcquireView::bundle);
    return true;
}

void CResourceRequests::acquire(SOperation& operation, const CAssetId source, const EAssetAcquireView view) noexcept
{
    const auto message = threading::make_pod_message(operation.identity, AssetAcquireRequest{ source, view });
    if ((m_assets.post == nullptr) || !m_assets.post(m_assets.context, message))
    {
        operation.result.status = EAssetStatus::delivery_failed;
        operation.phase = EPhase::ready;
    }
}

bool CResourceRequests::complete(const threading::CErasedPodMsg& message) noexcept
{
    auto* operation = find(message.query_async_slot());
    if (operation == nullptr) return false;
    if (operation->phase == EPhase::acquiring)
    {
        AssetAcquireResult result;
        if (!message.copy_payload_to(result)) return false;
        if (result.status != EAssetStatus::success)
        {
            if (result.acquisition) return false;
            operation->result.status = result.status;
            operation->phase = EPhase::ready;
            return true;
        }
        if (!result.acquisition) return false;
        operation->acquired = result;
        operation->phase = EPhase::running;
        if (operation->kind == EResourceKind::decoded_texture)
        {
            if (!result.byte_view().is_ready())
            {
                operation->result.status = EAssetStatus::invalid_request;
                finish(*operation);
                return true;
            }
            return m_textures.submit(TextureDecodeWork{ &operation->decode, &operation->acquired.views.bytes }, operation->identity);
        }
        if (!result.bundle_view().is_ready())
        {
            operation->result.status = EAssetStatus::invalid_request;
            finish(*operation);
            return true;
        }
        return m_shaders.submit(ShaderCompileWork{ &operation->compile.input, result.bundle_view() }, operation->identity);
    }
    if (operation->phase == EPhase::releasing)
    {
        AssetReleaseResult result;
        if (!message.copy_payload_to(result) || (result.status != EAssetStatus::success) ||
            (result.acquisition != operation->acquired.acquisition)) return false;
        operation->acquired = {};
        operation->phase = EPhase::ready;
        return true;
    }
    return false;
}

void CResourceRequests::finish(SOperation& operation) noexcept
{
    if (operation.acquired.acquisition)
    {
        operation.phase = EPhase::releasing;
        const auto message = threading::make_pod_message(operation.identity, AssetReleaseRequest{ operation.acquired.acquisition });
        if (!m_assets.post(m_assets.context, message)) m_failed = true;
    }
    else operation.phase = EPhase::ready;
}

void CResourceRequests::texture_complete(void* self, const std::int32_t identity, STextureResult&& result) noexcept
{
    auto& service = *static_cast<CResourceRequests*>(self);
    auto* operation = service.find(identity);
    if (operation == nullptr) { service.m_failed = true; return; }
    switch (result.status)
    {
        case ETextureStatus::success: operation->result.status = EAssetStatus::success; break;
        case ETextureStatus::allocation_failed: operation->result.status = EAssetStatus::allocation_failed; break;
        case ETextureStatus::invalid_input:
        case ETextureStatus::unsupported_format: operation->result.status = EAssetStatus::invalid_request; break;
        default: operation->result.status = EAssetStatus::conditioning_failed; break;
    }
    if (operation->result.status == EAssetStatus::success)
    {
        operation->result.kind = operation->kind;
        operation->result.encoded = std::move(result.encoded);
        operation->result.decoded = std::move(result.decoded);
    }
    service.finish(*operation);
}

void CResourceRequests::shader_complete(void* self, const std::int32_t identity, ShaderCompileResult&& result) noexcept
{
    auto& service = *static_cast<CResourceRequests*>(self);
    auto* operation = service.find(identity);
    if (operation == nullptr) { service.m_failed = true; return; }
    operation->result.status = (result.status == shader::EStatus::success) ? EAssetStatus::success :
        ((result.status == shader::EStatus::allocation_failed) ? EAssetStatus::allocation_failed : EAssetStatus::invalid_request);
    if (operation->result.status == EAssetStatus::success)
    {
        operation->result.kind = EResourceKind::shader_group;
        operation->result.shaders = std::move(result.group);
    }
    service.finish(*operation);
}

bool CResourceRequests::complete(const threading::SBatchResponse& response) noexcept
{
    return ((response.correlation & CShaderRequests::k_correlation_tag) != 0u) ? m_shaders.complete(response) : m_textures.complete(response);
}

bool CResourceRequests::receive(SResourceResult& result, std::int32_t& correlation) noexcept
{
    for (auto slot = m_operations.first_live(); slot >= 0; slot = m_operations.next_live(slot))
    {
        auto& operation = *m_operations.get_object(slot);
        if (operation.phase != EPhase::ready) continue;
        result = std::move(operation.result);
        correlation = operation.correlation;
        (void)m_operations.erase(slot);
        return true;
    }
    return false;
}

}   //  namespace rendering
