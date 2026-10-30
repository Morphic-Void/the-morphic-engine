
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    rendering_service_fixture.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    30 Oct 26
//
//  Controlled Rendering-local calls and real batch transports. Typed work never
//  uses the cross-component message transport. Real DLL flows are tested separately.

#pragma once
#include <vector>
#include "rendering/runtime/resource_requests.hpp"
#include "rendering/runtime/basis_codec.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/thread_helpers.hpp"
#include "tests/environment/test_environment.hpp"

namespace texture_batch_tests
{
// These typed contracts must never regain cross-component transport identities.
static_assert(k_type_id_binding_category_v<TextureEncodeRequest> == ETypeIdBindingCategory::none);
static_assert(k_type_id_binding_category_v<TextureDecodeRequest> == ETypeIdBindingCategory::none);
static_assert(k_type_id_binding_category_v<TextureEncodeWork> == ETypeIdBindingCategory::none);
static_assert(k_type_id_binding_category_v<TextureDecodeWork> == ETypeIdBindingCategory::none);
static_assert(k_type_id_binding_category_v<TextureEncodeResult> == ETypeIdBindingCategory::none);
static_assert(k_type_id_binding_category_v<TextureDecodeResult> == ETypeIdBindingCategory::none);
static_assert(k_type_id_binding_category_v<ShaderCompileRequest> == ETypeIdBindingCategory::none);
static_assert(k_type_id_binding_category_v<ShaderCompileWork> == ETypeIdBindingCategory::none);
static_assert(k_type_id_binding_category_v<ShaderCompileResult> == ETypeIdBindingCategory::none);

struct SLocalOwner
{
    TextureEncodeResult encode;
    TextureDecodeResult decode;
    ShaderCompileResult shader;
    int kind{ 0 };
    template<typename T> void set(T&& input) noexcept
    {
        if constexpr (std::is_same_v<T, TextureEncodeResult>) { encode = std::move(input); kind = 1; }
        else if constexpr (std::is_same_v<T, TextureDecodeResult>) { decode = std::move(input); kind = 2; }
        else { shader = std::move(input); kind = 3; }
    }
    template<typename T> T* payload() noexcept
    {
        if constexpr (std::is_same_v<T, TextureEncodeResult>) return kind == 1 ? &encode : nullptr;
        else if constexpr (std::is_same_v<T, TextureDecodeResult>) return kind == 2 ? &decode : nullptr;
        else return kind == 3 ? &shader : nullptr;
    }
    template<typename T> const T* payload() const noexcept { return const_cast<SLocalOwner*>(this)->payload<T>(); }

};
struct SLocalReply
{
    SLocalOwner storage;
    std::int32_t slot{ -1 };
    std::int32_t query_async_slot() const noexcept { return slot; }
    SLocalOwner& owner() noexcept { return storage; }
    SLocalOwner take_owner() noexcept { return std::move(storage); }
    template<typename T> bool is_message_a() const noexcept { return storage.payload<T>() != nullptr; }
};
struct SLocalWork
{
    TextureEncodeWork encode;
    TextureDecodeWork decode;
    ShaderCompileWork shader;
    int kind{ 0 };
    std::int32_t slot{ -1 };
    void set_async_slot(std::int32_t id) noexcept { slot = id; }
    template<typename T> void assign_payload(const T& input) noexcept
    {
        if constexpr (std::is_same_v<T, TextureEncodeWork>) { encode = input; kind = 1; }
        else if constexpr (std::is_same_v<T, TextureDecodeWork>) { decode = input; kind = 2; }
        else { shader = input; kind = 3; }
    }

};
struct SLocalPackage
{
    threading::CThreadControlState control;
    threading::CWaitPredicate wake;
    threading::CBatchClient batch;
    rendering::CTextureRequests textures;
    rendering::CShaderRequests shaders;
    std::vector<SLocalReply> replies;
    explicit SLocalPackage(threading::SBatchShared& shared) noexcept :
        batch{ shared, control, wake, module_ids::rendering, this },
        textures{ batch, this, &texture_done }, shaders{ batch, this, &shader_done }
    { control.mark_running(); }
    ~SLocalPackage() noexcept { wake.release_control(); }
    bool initialise() noexcept { return wake.acquire_control() && textures.initialise() && shaders.initialise(); }
    static void texture_done(void* self, std::int32_t slot, rendering::STextureResult&& result) noexcept
    {
        SLocalReply reply;
        reply.slot = slot;
        if (result.encoding) reply.storage.set(TextureEncodeResult{ std::move(result.encoded), result.status });
        else reply.storage.set(TextureDecodeResult{ std::move(result.decoded), result.status });
        static_cast<SLocalPackage*>(self)->replies.push_back(std::move(reply));
    }
    static void shader_done(void* self, std::int32_t slot, ShaderCompileResult&& result) noexcept
    {
        SLocalReply reply;
        reply.slot = slot;
        reply.storage.set(std::move(result));
        static_cast<SLocalPackage*>(self)->replies.push_back(std::move(reply));
    }
    bool post(const SLocalWork& work) noexcept
    {
        tests::TMemoryContextScope memory{ test_environment::rendering_memory_context() };
        if (work.kind == 1) return textures.submit(work.encode, work.slot);
        if (work.kind == 2) return textures.submit(work.decode, work.slot);
        return shaders.submit(work.shader, work.slot);
    }
    void pump() noexcept
    {
        tests::TMemoryContextScope memory{ test_environment::rendering_memory_context() };
        threading::SBatchResponse response;
        while (batch.receive(response))
        {
            const bool success = (response.correlation & rendering::CShaderRequests::k_correlation_tag) ?
                shaders.complete(response) : textures.complete(response);
            if (!success) control.mark_failed(1u);
        }
        if (control.exit_requested() && !textures.pending() && !shaders.pending()) control.mark_exited();
    }
    bool read(SLocalReply& reply) noexcept
    {
        pump();
        if (replies.empty()) return false;
        reply = std::move(replies.front());
        replies.erase(replies.begin());
        return true;
    }
    void request_exit() noexcept { control.request_exit(); control.mark_exiting(); }
    threading::EThreadRunState query_state() noexcept { pump(); return control.query_state(); }
    threading::CBatchClient* batch_client() noexcept { return &batch; }
};

struct SFixture
{
    platform::system::CPerfCountConversion conversion;
    threading::SBatchShared shared;
    TInstance<SLocalPackage> package;
    threading::SBatchWork held;
    struct SModule
    {
        bool prepare_batch_thread(thread_ids::id_type, memory::CMemoryContext* memory) noexcept
        { return memory != nullptr; }
    } module;
    bool initialise() noexcept { return conversion.init() && shared.wake.acquire_control() && rendering::basis_codec::initialise() && rendering::shader_compiler::initialise_compiler(); }
    ~SFixture() noexcept { stop(); rendering::basis_codec::shutdown(); shared.wake.release_control(); }
    bool start(bool queued) noexcept
    {
        stop();
        tests::TMemoryContextScope memory{ test_environment::rendering_memory_context() };
        shared.runner_count = queued ? 1u : 0u;
        return package.emplace(shared) && package->initialise();
    }
    bool acquire() noexcept
    {
        threading::transports::TAcquiredArenaSlot<threading::SBatchWork, threading::k_batch_channel_capacity> item(shared.work);
        if (!item) return false;
        held = *item;
        return item.recycle();
    }
    bool complete(threading::EBatchCompletion completion) noexcept
    {
        if (!held.requester) return false;
        const bool success = held.requester->complete({ held.correlation, completion });
        held = {};
        return success;
    }
    void stop() noexcept
    {
        if (!package) return;
        package->request_exit();
        if (held.requester) (void)complete(threading::EBatchCompletion::discarded_requester_exiting);
        while (acquire()) (void)complete(threading::EBatchCompletion::discarded_requester_exiting);
        package->pump();
        package.reset();
    }
    template<typename Work, typename Result>
    SLocalOwner operation(tests::TTestContext& ctx, const Work& work, std::int32_t slot, bool queued,
        threading::EBatchCompletion completion, bool exiting) noexcept
    {
        SLocalWork message;
        message.set_async_slot(slot);
        message.assign_payload(work);
        TEST_EXPECT(ctx, package->post(message));
        if (queued)
        {
            TEST_EXPECT(ctx, acquire());
            if (!held.requester) return {};
            TEST_EXPECT(ctx, held.memory_context == test_environment::rendering_memory_context());
            SLocalReply premature;
            TEST_EXPECT(ctx, !package->read(premature));
            if (exiting) package->request_exit();
            if (completion == threading::EBatchCompletion::executed)
            {
                tests::TMemoryContextScope memory{ held.memory_context };
                held.function(held.object);
            }
            TEST_EXPECT(ctx, complete(completion));
        }
        SLocalReply reply;
        TEST_EXPECT(ctx, package->read(reply));
        TEST_EXPECT(ctx, reply.slot == slot && reply.is_message_a<Result>());
        TEST_EXPECT(ctx, package->batch_client()->outstanding() == 0u);
        return reply.take_owner();
    }
};
struct SStopRendering { SFixture& fixture; ~SStopRendering() noexcept { fixture.stop(); } };
}
