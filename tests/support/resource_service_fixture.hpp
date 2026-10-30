
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    resource_service_fixture.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    30 Oct 26
//
//  Real generic Host asset service connected to Rendering-local resource calls.

#pragma once
#include "tests/support/rendering_service_fixture.hpp"
#include "host/runtime/asset_service.hpp"
#include "threading/messages/message_helpers.hpp"

namespace texture_batch_tests
{
struct SResourceFixture
{
    SFixture& rendering;
    host::CAssetService assets;
    TInstance<threading::CThreadPackage> client;
    TInstance<rendering::CResourceRequests> requests;
    std::vector<threading::CErasedPodMsg> messages;
    bool valid{ true };
    bool hold_release{ false };
    bool reject_acquire{ false };
    bool reject_release{ false };
    threading::CErasedPodMsg deferred_release;
    explicit SResourceFixture(SFixture& value) noexcept : rendering{ value } {}
    bool initialise(bool queued = true) noexcept
    {
        if (!rendering.start(queued) || !assets.initialise()) return false;
        const threading::ThreadConfig config{ thread_ids::rendering, module_ids::executable,
            platform::threading::EThreadPriority::Normal, &tests::echo_pod_messages };
        if (!client.emplace(config, rendering.conversion) || !client->startup()) return false;
        tests::TMemoryContextScope memory{ test_environment::rendering_memory_context() };
        return requests.emplace(*rendering.package->batch_client(), rendering::SAssetChannel{ this, &post }) && requests->initialise();
    }
    static bool post(void* value, const threading::CErasedPodMsg& message) noexcept
    {
        auto& self = *static_cast<SResourceFixture*>(value);
        tests::TMemoryContextScope memory{ test_environment::executable_memory_context() };
        AssetAcquireRequest acquire;
        AssetReleaseRequest release;
        if (message.copy_payload_to(acquire))
        {
            if (self.reject_acquire) return false;
            self.assets.request_acquire(acquire, message.query_async_slot(), *self.client);
        }
        else if (message.copy_payload_to(release))
        {
            if (self.reject_release) return false;
            if (self.hold_release) { self.deferred_release = message; return true; }
            self.assets.request_release(release, message.query_async_slot(), *self.client);
        }
        else return false;
        return !self.assets.failed();
    }
    void pump() noexcept
    {
        tests::TMemoryContextScope memory{ test_environment::rendering_memory_context() };
        threading::CErasedPodMsg message;
        while (client->read(message))
        {
            if (message.query_async_slot() >= rendering::CResourceRequests::k_first_slot) valid = requests->complete(message) && valid;
            else messages.push_back(message);
        }
        threading::SBatchResponse response;
        while (rendering.package->batch_client()->receive(response)) valid = requests->complete(response) && valid;
        assets.complete_disposals();
    }
    template<typename T> bool submit(T&& input, std::int32_t correlation) noexcept
    {
        tests::TMemoryContextScope memory{ test_environment::rendering_memory_context() };
        return requests->submit(std::forward<T>(input), correlation);
    }
    bool receive(rendering::SResourceResult& result, std::int32_t& correlation) noexcept
    { pump(); return requests->receive(result, correlation); }
    template<typename T> bool receive_host(T& result, std::int32_t& correlation) noexcept
    {
        pump();
        if (messages.empty()) return false;
        auto message = messages.front(); messages.erase(messages.begin());
        correlation = message.query_async_slot();
        return message.copy_payload_to(result);
    }
    CAssetId raw(tests::TTestContext& ctx, CByteBuffer&& buffer) noexcept
    {
        auto message = threading::make_owner_message<RawAssetTransfer>(10);
        auto* input = message.owner().payload<RawAssetTransfer>();
        if (!input) { TEST_EXPECT(ctx, false); return {}; }
        input->storage.value = std::move(buffer);
        assets.request(message, *client);
        AssetResult result;
        std::int32_t slot;
        TEST_EXPECT(ctx, tests::wait_until(rendering.conversion, [&]() noexcept { return receive_host(result, slot); }));
        TEST_EXPECT(ctx, result.status == EAssetStatus::success && slot == 10);
        return result.asset;
    }
    bool acquire_job() noexcept
    { return tests::wait_until(rendering.conversion, [&]() noexcept { pump(); return rendering.acquire(); }); }
    ~SResourceFixture() noexcept
    {
        if (requests && client && rendering.package)
        {
            rendering.package->request_exit();
            (void)tests::wait_until(rendering.conversion, [&]() noexcept {
                if (rendering.held.requester) (void)rendering.complete(threading::EBatchCompletion::discarded_requester_exiting);
                while (rendering.acquire()) (void)rendering.complete(threading::EBatchCompletion::discarded_requester_exiting);
                pump();
                rendering::SResourceResult result;
                std::int32_t slot;
                while (requests->receive(result, slot)) result = {};
                return !requests->pending() || (requests->failed() && !requests->jobs_pending());
            });
        }
        requests.reset();
        if (client) { (void)client->shutdown(); assets.detach_client(*client); }
        assets.deallocate();
        rendering.stop();
    }
};
}
