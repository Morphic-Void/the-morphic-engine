
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    rendering_thread.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    21 Sep 26
//
//  Rendering module thread and serialized texture conditioning.

#include "rendering/runtime/rendering_thread.hpp"
#include "rendering/runtime/basis_codec.hpp"

#include "debug/macros.hpp"
#include "module/module_binding_context.hpp"
#include "system/transported_types.hpp"
#include "threading/CThreadPackage.hpp"

#include <utility>

namespace rendering
{

static ETextureStatus texture_status(const basis_codec::EStatus status) noexcept
{
    switch (status)
    {
        case basis_codec::EStatus::success: return ETextureStatus::success;
        case basis_codec::EStatus::invalid_input: return ETextureStatus::invalid_input;
        case basis_codec::EStatus::unsupported_format: return ETextureStatus::unsupported_format;
        case basis_codec::EStatus::allocation_failed: return ETextureStatus::allocation_failed;
        default: return ETextureStatus::codec_failed;
    }
}

static bool encode_texture(threading::CThreadContext& context, const threading::CErasedPodMsg& message) noexcept
{
    TextureEncodeWork work;
    if (!message.copy_payload_to(work) || (work.request == nullptr))
    {
        return false;
    }
    CErasedOwner owner = CErasedOwner::create<TextureEncodeResult>();
    if (TextureEncodeResult* const result = owner.payload<TextureEncodeResult>())
    {
        const TextureEncodeRequest& request = *work.request;
        const image::texture::CInputView input{ request.input.const_view(), request.format, request.transfer };
        result->status = texture_status(basis_codec::encode_ktx2(input, request.options, result->texture));
    }
    threading::CErasedOwnerMsg response;
    response.set_message_type<TextureEncodeResult>();
    response.set_async_slot(message.query_async_slot());
    response.set_owner(std::move(owner));
    return context.post(std::move(response));
}

static bool decode_texture(threading::CThreadContext& context, const threading::CErasedPodMsg& message) noexcept
{
    TextureDecodeWork work;
    if (!message.copy_payload_to(work) || (work.request == nullptr) || (work.input == nullptr))
    {
        return false;
    }
    CErasedOwner owner = CErasedOwner::create<TextureDecodeResult>();
    if (TextureDecodeResult* const result = owner.payload<TextureDecodeResult>())
    {
        result->status = texture_status(basis_codec::transcode_ktx2(
            (*work.input), work.request->target, work.request->level, result->texture));
    }
    threading::CErasedOwnerMsg response;
    response.set_message_type<TextureDecodeResult>();
    response.set_async_slot(message.query_async_slot());
    response.set_owner(std::move(owner));
    return context.post(std::move(response));
}

static std::uint32_t MV_STD_ABI_CALL thread_entry(void* const user_data) noexcept
{
    if (user_data == nullptr)
    {
        return ~0u;
    }

    threading::CThreadResources& resources = *static_cast<threading::CThreadResources*>(user_data);
    if (!modules::is_thread_context_ready(user_data))
    {
        resources.control_state.mark_failed(~0u);
        return ~0u;
    }

    threading::CThreadContext context{ resources };
    context.startup();
    if (!basis_codec::initialise())
    {
        context.mark_failed(1u);
        return 1u;
    }
    MV_REPORT("Rendering: Running");
    context.mark_running();

    std::uint32_t epoch{ 0u };
    while (!context.exit_requested())
    {
        context.advance_heartbeat();
        threading::CErasedPodMsg message;
        if (context.read(message))
        {
            const type_id identity = message.query_message_type_id();
            const bool delivered = (identity == k_type_id_v<TextureEncodeWork>) ?
                encode_texture(context, message) :
                ((identity == k_type_id_v<TextureDecodeWork>) ? decode_texture(context, message) : false);
            if (!delivered)
            {
                MV_CRITICAL_EVENT("Rendering: Texture work response delivery failed");
                basis_codec::shutdown();
                context.mark_failed(2u);
                return 2u;
            }
        }
        else
        {
            epoch = context.wait_for_new_epoch(epoch);
        }
    }

    context.mark_exiting();
    basis_codec::shutdown();
    MV_REPORT("Rendering: Exited");
    context.mark_exited();
    return 0u;
}

FRenderingThread rendering_thread_entry_point() noexcept
{
    return &thread_entry;
}

}   //  namespace rendering
