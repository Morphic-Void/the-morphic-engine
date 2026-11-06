
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SchemaService_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    06 Nov 26
//
//  Deterministic publication failure and quiescence with production Host queues.

#include "host/runtime/asset_service.hpp"
#include "tests/support/direct_connection_fixture.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/input_schema.hpp"
#include "threading/messages/message_helpers.hpp"
#include <cstring>

namespace
{
using tests::TTestContext;

struct SFixture
{
    TTestContext& tests;
    platform::system::CPerfCountConversion conversion;
    threading::CThreadPackage worker{ { thread_ids::bg_conditioning, module_ids::executable }, conversion };
    threading::CThreadPackage recipient{ { thread_ids::executive, module_ids::executive }, conversion };
    host::SClientRecord client{ module_ids::executive, 19u, &recipient };
    modules::SHostExecutionContext worker_execution;
    threading::CThreadContext worker_context{ threading::SDirectConnectionTestAccess::resources(worker) };
    threading::CThreadContext recipient_context{ threading::SDirectConnectionTestAccess::resources(recipient) };
    host::CAssetService service;
    CAssetId asset;

    explicit SFixture(TTestContext& tests) : tests{ tests }
    {
        TEST_EXPECT(tests, conversion.init());
        TEST_EXPECT(tests, threading::SDirectConnectionTestAccess::initialise(worker, worker_execution));
        TEST_EXPECT(tests, threading::SDirectConnectionTestAccess::initialise(recipient, client.host_execution));
        client.phase = host::EClientPhase::ready;
        TEST_EXPECT(tests, (service.initialise() && service.configure_workers(worker, nullptr)));
        const std::string source = tests::input_schema();
        CErasedOwner owner = CErasedOwner::create<RawAssetTransfer>();
        RawAssetTransfer* const raw = owner.payload<RawAssetTransfer>();
        TEST_EXPECT(tests, (raw && raw->storage.value.resize(source.size(), 16u)));
        if (!raw || !raw->storage.value.is_ready())
        {
            return;
        }
        std::memcpy(raw->storage.value.data(), source.data(), source.size());
        threading::CErasedOwnerMsg request;
        request.set_message_type<RawAssetTransfer>();
        request.set_async_slot(1);
        request.set_owner(std::move(owner));
        service.request(request, client);
        threading::CErasedPodMsg message;
        AssetResult result;
        TEST_EXPECT(tests, (recipient_context.read(message) && message.copy_payload_to(result) && (result.status == EAssetStatus::success)));
        asset = result.asset;
    }
    ~SFixture()
    {
        worker_context.mark_exited();
        recipient_context.mark_exited();
        service.fail_pending();
        service.release_client_acquisitions(client);
        service.complete_disposals();
        service.deallocate();
        TEST_EXPECT(tests, (worker.shutdown() && recipient.shutdown()));
    }
    void request()
    {
        TEST_EXPECT(tests, client.reserve_replies(1u));
        service.request_schema({ asset }, 2, client);
    }
    threading::CErasedPodMsg work()
    {
        threading::CErasedPodMsg message;
        TEST_EXPECT(tests, worker_context.read(message));
        SchemaConditionWork work;
        TEST_EXPECT(tests, message.copy_payload_to(work));
        return message;
    }
    void execute(const threading::CErasedPodMsg& message)
    {
        SchemaConditionWork work;
        TEST_EXPECT(tests, message.copy_payload_to(work));
        if (!work.state) return;
        if (work.retire) work.state->prepared.clear();
        else
        {
            TEST_EXPECT(tests, work.state->prepared.prepare(work.state->source, work.state->request.policy,
                work.state->request.limits, work.state->diagnostic));
        }
    }
    void complete(const threading::CErasedPodMsg& message)
    {
        SchemaConditionWork work;
        TEST_EXPECT(tests, message.copy_payload_to(work));
        service.complete(threading::make_pod_message(message.query_async_slot(), SchemaConditionResult{ work.retire }), worker);
    }
    SchemaPrepareResult result()
    {
        threading::CErasedPodMsg message;
        SchemaPrepareResult result;
        TEST_EXPECT(tests, (recipient_context.read(message) && message.copy_payload_to(result)));
        return result;
    }
    void reclaim(const CAssetAcquisitionId acquisition)
    {
        service.request_release({ acquisition }, 3, client);
        service.complete_disposals();
        const threading::CErasedPodMsg retirement = work();
        execute(retirement);
        complete(retirement);
        threading::CErasedPodMsg message;
        TEST_EXPECT(tests, recipient_context.read(message));
    }
    void fill(threading::CThreadPackage& package)
    {
        threading::transports::CErasedPodMsgTransport& pipe = threading::SDirectConnectionTestAccess::resources(package).host_to_worker_msgs;
        pipe.deallocate();
        TEST_EXPECT(tests, pipe.initialise_growable(32u, 128u));
        for (unsigned index = 0u; index < 32u; ++index)
        {
            TEST_EXPECT(tests, package.post(threading::make_pod_message(-1, AssetReleaseResult{})));
        }
    }
};

void poison(TTestContext& ctx, const unsigned phase)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, tests::allocate_with_failure, tests::deallocate_test_memory, system_ids::host };
    memory::CMemoryContext memory{ allocator, system_ids::host };
    tests::TMemoryContextScope scope{ &memory };
    SFixture fixture{ ctx };
    //  Prime operation storage before arming queue-only allocation failure.
    fixture.request();
    threading::CErasedPodMsg work = fixture.work();
    fixture.execute(work);
    fixture.complete(work);
    SchemaPrepareResult result = fixture.result();
    if (phase == 0u)
    {
        fixture.reclaim(result.acquisition);
        fixture.fill(fixture.worker);
        allocation.fail_on = allocation.attempts + 1u;
        fixture.request();
    }
    else if (phase == 1u)
    {
        fixture.reclaim(result.acquisition);
        fixture.request();
        work = fixture.work();
        fixture.execute(work);
        fixture.fill(fixture.recipient);
        allocation.fail_on = allocation.attempts + 1u;
        fixture.complete(work);
    }
    else
    {
        fixture.fill(fixture.worker);
        fixture.service.request_release({ result.acquisition }, 3, fixture.client);
        allocation.fail_on = allocation.attempts + 1u;
        fixture.service.complete_disposals();
    }
    TEST_EXPECT(ctx, (allocation.failed && fixture.service.failed()));
    TEST_EXPECT(ctx, ((phase == 1u) ? fixture.recipient.pod_delivery_poisoned() : fixture.worker.pod_delivery_poisoned()));
    if (phase == 1u)
    {
        unsigned exposed = 0u;
        threading::CErasedPodMsg message;
        while (fixture.recipient_context.read(message))
        {
            if (message.copy_payload_to(result)) ++exposed;
        }
        TEST_EXPECT(ctx, ((exposed == 1u) && !fixture.client.reserved_replies && result.acquisition));
        fixture.service.release_client_acquisitions(fixture.client);
        TEST_EXPECT(ctx, schema::CPreparedSchemaView{ result.descriptor }.is_ready());
        fixture.service.complete_disposals();
        TEST_EXPECT(ctx, !fixture.service.client_idle(fixture.client));
        TEST_EXPECT(ctx, !fixture.worker_context.read(message));
        TEST_EXPECT(ctx, schema::CPreparedSchemaView{ result.descriptor }.is_ready());
        TEST_EXPECT(ctx, !fixture.recipient_context.read(message));
        fixture.recipient_context.mark_exited();
        fixture.service.release_client_acquisitions(fixture.client);
        fixture.service.complete_disposals();
        work = fixture.work();
        fixture.execute(work);
        fixture.complete(work);
    }
    else
    {
        unsigned exposed = 0u;
        threading::CErasedPodMsg message;
        while (fixture.worker_context.read(message))
        {
            SchemaConditionWork request;
            if (message.copy_payload_to(request))
            {
                ++exposed;
                TEST_EXPECT(ctx, request.state);
                fixture.execute(message);
                //  Simulate no worker completion after the poisoned submission.
            }
        }
        TEST_EXPECT(ctx, (exposed == 1u));
        fixture.worker_context.mark_exited();
        fixture.service.fail_pending();
    }
    fixture.recipient_context.mark_exited();
    fixture.service.release_client_acquisitions(fixture.client);
    fixture.service.complete_disposals();
    TEST_EXPECT(ctx, fixture.service.is_idle());
}

void vanished_recipient(TTestContext& ctx)
{
    SFixture fixture{ ctx };
    fixture.request();
    const threading::CErasedPodMsg work = fixture.work();
    fixture.execute(work);
    fixture.recipient_context.mark_exited();
    fixture.complete(work);
    const threading::CErasedPodMsg retirement = fixture.work();
    fixture.execute(retirement);
    fixture.complete(retirement);
    TEST_EXPECT(ctx, (fixture.service.is_idle() && !fixture.client.reserved_replies));
}
}

void run_schema_service_cases(tests::TTestContext& ctx)
{
    poison(ctx, 0u);
    poison(ctx, 1u);
    poison(ctx, 2u);
    vanished_recipient(ctx);
}
