
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    rendering_thread.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    21 Sep 26
//
//  Rendering module thread and asynchronous texture conditioning.

#include "threading/messages/message_helpers.hpp"

#include "runtime/rendering_thread.hpp"
#include "runtime/basis_codec.hpp"
#include "runtime/texture_jobs.hpp"
#include "runtime/shader_requests.hpp"
#include "runtime/texture_flow_tests.hpp"

#include "containers/TUnorderedCollection.hpp"
#include "debug/macros.hpp"
#include "module/module_binding_context.hpp"
#include "image/texture_messages.hpp"
#include "threading/CThreadPackage.hpp"

#include <utility>

namespace rendering
{

//  Each record retains a stable job and its Host response until completion.
//  Batch correlations name rendering records independently of Host slots.
class CTextureRequests final
{
public:
    CTextureRequests(threading::CThreadContext& context, threading::CBatchClient& batch) noexcept :
        m_context{ context }, m_batch{ batch } {}

    [[nodiscard]] bool initialise() noexcept { return m_requests.initialise(); }
    [[nodiscard]] bool pending() const noexcept { return !m_requests.is_empty(); }
    [[nodiscard]] bool submit(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool complete(const threading::SBatchResponse& response) noexcept;

private:
    struct SRequest
    {
        TInstance<CTextureEncodeJob> encode;
        TInstance<CTextureDecodeJob> decode;
        threading::CErasedOwnerMsg response;
    };

    [[nodiscard]] bool dispatch(const std::int32_t slot, const threading::FBatchWork function, void* const object) noexcept;
    [[nodiscard]] bool finish(const std::int32_t slot, const bool executed) noexcept;

    threading::CThreadContext& m_context;
    threading::CBatchClient& m_batch;
    TUnorderedCollection<SRequest> m_requests;
};

bool CTextureRequests::submit(const threading::CErasedPodMsg& message) noexcept
{
    if (message.query_async_slot() < 0)
    {
        return false;
    }
    TextureEncodeWork encode;
    TextureDecodeWork decode;
    const bool encoding = message.copy_payload_to(encode) && (encode.request != nullptr);
    const bool decoding = message.copy_payload_to(decode) && (decode.request != nullptr) && (decode.input != nullptr);
    if (!encoding && !decoding)
    {
        return false;
    }

    threading::CErasedOwnerMsg response;
    response.set_async_slot(message.query_async_slot());
    if (encoding) response.set_message_type<TextureEncodeResult>();
    else response.set_message_type<TextureDecodeResult>();
    const std::int32_t slot = m_requests.emplace();
    if (slot < 0)
    {   //  An empty typed owner follows the Host's allocation-failure path.
        return m_context.post(std::move(response));
    }
    SRequest& pending = *m_requests.get_object(slot);
    pending.response = std::move(response);
    if (encoding)
    {
        pending.response = threading::make_owner_message<TextureEncodeResult>(message.query_async_slot());
        const bool allocated = pending.response.has_owner();
        const TextureEncodeRequest& request = *encode.request;
        const image::texture::CInputView input{ request.input.const_view(), request.format, request.transfer };
        if (!allocated || !pending.encode.emplace(input, request.options))
        {
            return finish(slot, false);
        }
        return dispatch(slot, (&CTextureEncodeJob::execute), pending.encode.operator->());
    }
    pending.response = threading::make_owner_message<TextureDecodeResult>(message.query_async_slot());
    const bool allocated = pending.response.has_owner();
    if (!allocated || !pending.decode.emplace((*decode.input), decode.request->target, decode.request->level))
    {
        return finish(slot, false);
    }
    return dispatch(slot, (&CTextureDecodeJob::execute), pending.decode.operator->());
}

bool CTextureRequests::dispatch(const std::int32_t slot, const threading::FBatchWork function, void* const object) noexcept
{
    const threading::SBatchSubmission submitted = m_batch.submit(function, object, static_cast<std::uint32_t>(slot));
    if (submitted.status == threading::EBatchSubmission::queued)
    {
        MV_LOG(detail, "Rendering: Texture batch queued, slot {}", slot);
        return true;
    }
    if (submitted.status == threading::EBatchSubmission::completed_inline)
    {
        MV_LOG(detail, "Rendering: Texture batch completed inline, slot {}", slot);
        return finish(slot, true);
    }
    MV_LOG(error, "Rendering: Texture batch rejected, slot {}, reason {}", slot, static_cast<unsigned int>(submitted.reason));
    return finish(slot, false);
}

bool CTextureRequests::complete(const threading::SBatchResponse& response) noexcept
{
    const std::int32_t slot = static_cast<std::int32_t>(response.correlation);
    if ((response.correlation > static_cast<std::uint32_t>(INT32_MAX)) || (m_requests.get_object(slot) == nullptr))
    {
        return false;
    }
    MV_LOG(detail, "Rendering: Texture batch completion, slot {}, outcome {}", slot, static_cast<unsigned int>(response.completion));
    return finish(slot, (response.completion == threading::EBatchCompletion::executed));
}

bool CTextureRequests::finish(const std::int32_t slot, const bool executed) noexcept
{
    SRequest& pending = *m_requests.get_object(slot);
    threading::CErasedOwnerMsg response = std::move(pending.response);
    if (pending.encode)
    {
        if (TextureEncodeResult* const result = response.owner().payload<TextureEncodeResult>())
        {
            result->status = executed ? pending.encode->status() : ETextureStatus::codec_failed;
            if (executed) result->texture = pending.encode->take_output();
            if ((result->status == ETextureStatus::success) && !result->texture.view().is_ready())
            {
                MV_LOG(error, "Rendering: encode reported success without usable output at slot {}", response.query_async_slot());
                result->status = ETextureStatus::codec_failed;
            }
        }
    }
    if (pending.decode)
    {
        if (TextureDecodeResult* const result = response.owner().payload<TextureDecodeResult>())
        {
            result->status = executed ? pending.decode->status() : ETextureStatus::codec_failed;
            if (executed) result->texture = pending.decode->take_output();
            if ((result->status == ETextureStatus::success) && !result->texture.view().is_ready())
            {
                MV_LOG(error, "Rendering: decode reported success without usable output at slot {}", response.query_async_slot());
                result->status = ETextureStatus::codec_failed;
            }
        }
    }
    (void)m_requests.erase(slot);
    return m_context.post(std::move(response));
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
    CTextureRequests requests{ context, (*resources.batch_client) };
    CShaderRequests shaders{ context, (*resources.batch_client) };
    CTextureFlowTests tests{ context, resources.config.worker_module_id };
    if (!requests.initialise() || !shaders.initialise())
    {
        basis_codec::shutdown();
        context.mark_failed(1u);
        return 1u;
    }
    MV_LOG(info, "Rendering: Running");
    if (!shader_compiler::initialise_compiler())
    {
        MV_LOG(info, "Rendering: DXC is unavailable; shader requests will retain failure reports");
    }
    context.mark_running();
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
            const bool completed = ((response.correlation & CShaderRequests::k_correlation_tag) != 0u) ?
                shaders.complete(response) : requests.complete(response);
            failed = !completed || failed;
        }

        //  Drain accepted messages even after exit was requested. Submission
        //  then rejects them and returns a terminal failure to the Host.
        threading::CErasedPodMsg message;
        while (!failed && context.read(message))
        {
            progressed = true;
            if (message.is_payload_a<RunTestsRequest>())
                failed = !tests.start(message);
            else if (message.is_payload_a<AssetResult>() || message.is_payload_a<AssetDisposeResult>())
                failed = !tests.complete(message);
            else
                failed = !(message.is_payload_a<ShaderCompileWork>() ? shaders.submit(message) : requests.submit(message));
        }
        if ((context.exit_requested() || failed) && !requests.pending() && !shaders.pending() && (failed || !tests.pending()))
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
        MV_CRITICAL_EVENT("Rendering: Work or test response delivery failed");
        context.mark_failed(2u);
        return 2u;
    }
    MV_LOG(info, "Rendering: Exited");
    context.mark_exited();
    return 0u;
}

FRenderingThread rendering_thread_entry_point() noexcept
{
    return &thread_entry;
}

}   //  namespace rendering
