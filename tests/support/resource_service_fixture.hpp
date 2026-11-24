
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
#include "framework/system/system_ids.hpp"
#include "tests/support/rendering_service_fixture.hpp"
#include "host/assets/asset_service.hpp"
#include "tests/support/endpoint_fixture.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include <cstdio>
#include <cstdlib>

namespace texture_batch_tests
{
struct SResourceFixture
{
    SFixture& rendering;
    host::CAssetService assets;
    TInstance<tests::SCommonEndpointFixture> client;
    static inline std::uint64_t next_identity{ 4000u };
    TInstance<rendering::CResourceRequests> requests;
    std::vector<threading::CErasedPodMsg> messages;
    bool valid{ true };
    bool hold_release{ false };
    bool reject_acquire{ false };
    bool reject_release{ false };
    bool busy_acquire{ false }, busy_release{ false };
    threading::CErasedPodMsg deferred_release;
    explicit SResourceFixture(SFixture& value) noexcept : rendering{ value } {}
    bool initialise(const bool queued = true) noexcept
    {
        if (!rendering.start(queued) || !assets.initialise())
        {
            return false;
        }
        if (!client.emplace(module_ids::executable, ++next_identity, 32u))
        {
            return false;
        }
        tests::TMemoryContextScope memory{ test_environment::rendering_memory_context() };
        return requests.emplace(*rendering.package->batch_client(), rendering::SAssetChannel{ this, &post }) && requests->initialise();
    }
    static modules::ESubmission post(void* const value, const threading::CErasedPodMsg& message) noexcept
    {
        SResourceFixture& self = *static_cast<SResourceFixture*>(value);
        tests::TMemoryContextScope memory{ test_environment::executable_memory_context() };
        AssetAcquireRequest acquire;
        AssetReleaseRequest release;
        if (message.copy_payload_to(acquire))
        {
            if (self.busy_acquire) return modules::ESubmission::busy;
            if (self.reject_acquire)
            {
                return modules::ESubmission::transport_failed;
            }
            host::CRetainedReplies::CDispatch dispatch{ *self.client->pending_replies, 1u };
            if (!dispatch) return modules::ESubmission::busy;
            if (!self.client->reserve_replies(1u))
            {
                return modules::ESubmission::transport_failed;
            }
            self.assets.request_acquire(acquire, message.query_async_slot(), *self.client);
        }
        else if (message.copy_payload_to(release))
        {
            if (self.busy_release) return modules::ESubmission::busy;
            if (self.reject_release)
            {
                return modules::ESubmission::transport_failed;
            }
            if (self.hold_release)
            {
                self.deferred_release = message;
                return modules::ESubmission::accepted;
            }
            host::CRetainedReplies::CDispatch dispatch{ *self.client->pending_replies, 1u };
            if (!dispatch) return modules::ESubmission::busy;
            if (!self.client->reserve_replies(1u))
            {
                return modules::ESubmission::transport_failed;
            }
            self.assets.request_release(release, message.query_async_slot(), *self.client);
        }
        else return modules::ESubmission::invalid;
        return self.assets.failed() ? modules::ESubmission::transport_failed : modules::ESubmission::accepted;
    }
    void pump() noexcept
    {
        tests::TMemoryContextScope memory{ test_environment::rendering_memory_context() };
        (void)requests->progress();
        progress_host();
        threading::CErasedPodMsg message;
        while (client->channel->read_preamble_pod(message))
        {
            if (message.query_async_slot() >= rendering::CResourceRequests::k_first_slot) valid = requests->complete(message) && valid;
            else messages.push_back(message);
        }
        threading::SBatchResponse response;
        while (rendering.package->batch_client()->receive(response)) valid = requests->complete(response) && valid;
        progress_host();
    }
    void progress_host() noexcept
    {
        tests::TMemoryContextScope memory{ test_environment::executable_memory_context() };
        assets.complete_disposals();
        client->flush_replies();
        for (unsigned count = 0u; (count < 8u) && client->output_ready(); ++count)
        {
            const bool operation = assets.output_ready(*client);
            if (operation) assets.retry_output(*client);
            const bool bundle = client->output_ready() && assets.bundle_output_ready(*client);
            if (bundle) assets.retry_bundle_output(*client);
            if (!operation && !bundle)
            {
                break;
            }
        }
        assets.complete_disposals();
    }
    void dispose(tests::TTestContext& ctx, const CAssetId asset, const std::int32_t correlation) noexcept
    {
        tests::TMemoryContextScope memory{ test_environment::executable_memory_context() };
        host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 1u };
        TEST_EXPECT(ctx, (dispatch && client->reserve_replies(1u)));
        assets.request_disposal({ asset }, correlation, *client);
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
        {
            tests::TMemoryContextScope memory{ test_environment::executable_memory_context() };
            host::CRetainedReplies::CDispatch dispatch{ *client->pending_replies, 1u };
            TEST_EXPECT(ctx, (dispatch && client->reserve_replies(1u)));
            assets.request(message, *client);
        }
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
            const bool settled = tests::wait_until(rendering.conversion, [&]() noexcept {
                if (rendering.held.requester) (void)rendering.complete(threading::EBatchCompletion::discarded_requester_exiting);
                while (rendering.acquire()) (void)rendering.complete(threading::EBatchCompletion::discarded_requester_exiting);
                pump();
                rendering::SResourceResult result;
                std::int32_t slot;
                while (requests->receive(result, slot)) result = {};
                return !requests->pending() || (requests->failed() && !requests->jobs_pending());
            });
            if (!settled)
            {
                std::fputs("Resource fixture cleanup timed out with live requests; refusing to release batch/asset storage.\n", stderr);
                std::abort();
            }
        }
        requests.reset();
        if (client)
        {
            // Controlled recipient has no execution or remaining ingress. The
            // actual Rendering batch owner above must settle first.
            client->phase = host::EClientPhase::stopped;
            client->terminal_before_drain = client->incoming_drained = true;
            client->flush_replies();
            assets.detach_client(*client);
        }
        assets.deallocate();
        rendering.stop();
    }
};
}
