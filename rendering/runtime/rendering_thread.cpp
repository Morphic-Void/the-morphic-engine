
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
#include "rendering/runtime/texture_jobs.hpp"

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

//  The Host admits one texture request across clients. Rendering retains the
//  stable job and response owner until its batch completion has been consumed.
class CTextureRequests final
{
public:
    CTextureRequests(threading::CThreadContext& context, threading::CBatchClient& batch) noexcept :
        m_context{ context }, m_batch{ batch } {}

    [[nodiscard]] bool pending() const noexcept { return m_slot >= 0; }
    [[nodiscard]] bool submit(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool complete(const threading::SBatchResponse& response) noexcept;

private:
    [[nodiscard]] bool dispatch(const threading::FBatchWork function, void* const object) noexcept;
    [[nodiscard]] bool finish(const bool executed) noexcept;

    threading::CThreadContext& m_context;
    threading::CBatchClient& m_batch;
    TInstance<CTextureEncodeJob> m_encode;
    TInstance<CTextureDecodeJob> m_decode;
    threading::CErasedOwnerMsg m_response;
    std::int32_t m_slot{ -1 };
};

bool CTextureRequests::submit(const threading::CErasedPodMsg& message) noexcept
{
    if (pending() || (message.query_async_slot() < 0))
    {
        return false;
    }
    TextureEncodeWork encode;
    TextureDecodeWork decode;
    if (message.copy_payload_to(encode) && (encode.request != nullptr))
    {
        m_slot = message.query_async_slot();
        m_response.set_message_type<TextureEncodeResult>();
        CErasedOwner owner = CErasedOwner::create<TextureEncodeResult>();
        const bool allocated = (owner.payload<TextureEncodeResult>() != nullptr);
        m_response.set_owner(std::move(owner));
        const TextureEncodeRequest& request = *encode.request;
        const image::texture::CInputView input{ request.input.const_view(), request.format, request.transfer };
        if (!allocated || !m_encode.emplace(input, request.options))
        {
            return finish(false);
        }
        return dispatch((&CTextureEncodeJob::execute), m_encode.operator->());
    }
    if (message.copy_payload_to(decode) && (decode.request != nullptr) && (decode.input != nullptr))
    {
        m_slot = message.query_async_slot();
        m_response.set_message_type<TextureDecodeResult>();
        CErasedOwner owner = CErasedOwner::create<TextureDecodeResult>();
        const bool allocated = (owner.payload<TextureDecodeResult>() != nullptr);
        m_response.set_owner(std::move(owner));
        if (!allocated || !m_decode.emplace((*decode.input), decode.request->target, decode.request->level))
        {
            return finish(false);
        }
        return dispatch((&CTextureDecodeJob::execute), m_decode.operator->());
    }
    return false;
}

bool CTextureRequests::dispatch(const threading::FBatchWork function, void* const object) noexcept
{
    const threading::SBatchSubmission submitted = m_batch.submit(function, object, static_cast<std::uint32_t>(m_slot));
    if (submitted.status == threading::EBatchSubmission::queued)
    {
        MV_REPORT("Rendering: Texture batch queued, slot %d", m_slot);
        return true;
    }
    if (submitted.status == threading::EBatchSubmission::completed_inline)
    {
        MV_REPORT("Rendering: Texture batch completed inline, slot %d", m_slot);
        return finish(true);
    }
    MV_REPORT("Rendering: Texture batch rejected, slot %d, reason %u",
        m_slot, static_cast<unsigned int>(submitted.reason));
    return finish(false);
}

bool CTextureRequests::complete(const threading::SBatchResponse& response) noexcept
{
    if (!pending() || (response.correlation != static_cast<std::uint32_t>(m_slot)))
    {
        return false;
    }
    MV_REPORT("Rendering: Texture batch completion, slot %d, outcome %u",
        m_slot, static_cast<unsigned int>(response.completion));
    return finish(response.completion == threading::EBatchCompletion::executed);
}

bool CTextureRequests::finish(const bool executed) noexcept
{
    CErasedOwner owner = m_response.take_owner();
    if (m_encode)
    {
        if (TextureEncodeResult* const result = owner.payload<TextureEncodeResult>())
        {
            result->status = executed ? texture_status(m_encode->status()) : ETextureStatus::codec_failed;
            if (executed) result->texture = m_encode->take_output();
        }
        m_encode.reset();
    }
    if (m_decode)
    {
        if (TextureDecodeResult* const result = owner.payload<TextureDecodeResult>())
        {
            result->status = executed ? texture_status(m_decode->status()) : ETextureStatus::codec_failed;
            if (executed) result->texture = m_decode->take_output();
        }
        m_decode.reset();
    }
    m_response.set_async_slot(m_slot);
    m_response.set_owner(std::move(owner));
    m_slot = -1;
    return m_context.post(std::move(m_response));
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
    if ((resources.batch_client == nullptr) || !basis_codec::initialise())
    {
        context.mark_failed(1u);
        return 1u;
    }
    MV_REPORT("Rendering: Running");
    context.mark_running();

    CTextureRequests requests{ context, (*resources.batch_client) };
    bool failed = false;
    for (;;)
    {
        const std::uint32_t epoch = resources.wait_predicate.get_word();
        context.advance_heartbeat();
        if (context.exit_requested()) context.mark_exiting();
        bool progressed = false;
        threading::SBatchResponse response;
        while (resources.batch_client->receive(response))
        {
            progressed = true;
            failed = !requests.complete(response) || failed;
        }

        //  Drain accepted messages even after exit was requested. Submission
        //  then rejects them and returns a terminal failure to the Host.
        threading::CErasedPodMsg message;
        while (!failed && context.read(message))
        {
            progressed = true;
            failed = !requests.submit(message);
        }
        if ((context.exit_requested() || failed) && !requests.pending())
        {
            break;
        }
        if (!progressed)
        {
            if (context.exit_requested())
            {   //  Remain visibly Exiting while queued/running jobs drain.
                (void)resources.wait_predicate.wait_until_not_equal(resources.parking_ticket, epoch);
            }
            else
            {
                (void)context.wait_for_new_epoch(epoch);
            }
        }
    }

    context.mark_exiting();
    basis_codec::shutdown();
    if (failed)
    {
        MV_CRITICAL_EVENT("Rendering: Texture work response delivery failed");
        context.mark_failed(2u);
        return 2u;
    }
    MV_REPORT("Rendering: Exited");
    context.mark_exited();
    return 0u;
}

FRenderingThread rendering_thread_entry_point() noexcept
{
    return &thread_entry;
}

}   //  namespace rendering
