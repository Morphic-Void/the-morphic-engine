
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SchemaService_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    06 Nov 26
//
//  Deterministic publication failure and quiescence with production Host queues.

#include "framework/system/system_ids.hpp"
#include "host/runtime/asset_service.hpp"
#include "tests/support/direct_connection_fixture.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "tests/support/document_helpers.hpp"
#include "tests/support/input_schema.hpp"
#include "framework/threading/messages/message_helpers.hpp"
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

using Status = ESchemaProcessStatus;
using Output = schema::ESchemaOutput;
using Assembly = schema::ESchemaAssembly;
using Limit = schema::EDataOperationLimit;

struct SProcessFixture : SFixture
{
    using SFixture::SFixture;
    std::uint64_t next{ 1u };
    static constexpr std::int32_t slot = 97;

    static bool owned(SSchemaProcessInput& input, const char* const json)
    {
        input.document = {};
        input.kind = ESchemaInput::owned_document;
        input.asset = {};
        return tests::parse_document(json, input.document);
    }
    static void retained(SSchemaProcessInput& input, const CAssetId id)
    {
        input.document = {};
        input.kind = ESchemaInput::retained_document;
        input.asset = id;
    }
    template<class Configure>
    void submit(const Configure& configure)
    {
        auto message = threading::make_owner_message<SchemaProcessRequest>(slot);
        SchemaProcessRequest& request = *message.owner().payload<SchemaProcessRequest>();
        request.request_id.value = next++;
        request.input_count = 1u;
        request.output_kind = Output::schema_document;
        TEST_EXPECT(tests, owned(request.inputs.slots[0], R"({"types":{}})"));
        configure(request);
        TEST_EXPECT(tests, client.reserve_replies(2u));
        service.request_process(message, client, false, false);
        TEST_EXPECT(tests, !message.has_owner());
    }
    template<class T>
    T read()
    {
        T result;
        threading::CErasedPodMsg message;
        TEST_EXPECT(tests, (recipient_context.read(message) && message.copy_payload_to(result)));
        return result;
    }
    void accepted()
    {
        threading::CErasedPodMsg message;
        SchemaProcessAccepted result;
        TEST_EXPECT(tests, (recipient_context.read(message) && (message.query_async_slot() == slot) &&
            message.copy_payload_to(result) && (result.request_id.value == (next - 1u)) && (result.contract_version == 1u)));
    }
    SchemaProcessResult terminal(const Status expected)
    {
        threading::CErasedPodMsg message;
        SchemaProcessResult result;
        TEST_EXPECT(tests, (recipient_context.read(message) && (message.query_async_slot() == slot) &&
            message.copy_payload_to(result) && (result.status == expected) && (result.contract_version == 1u) && !result.reserved));
        if (expected != Status::success)
        {
            TEST_EXPECT(tests, ((result.output_kind == Output::none) && !result.failure.reserved && result.failure.reason));
        }
        return result;
    }
    threading::CErasedPodMsg step(const bool acknowledge = true)
    {
        threading::CErasedPodMsg message;
        SchemaProcessWork work;
        TEST_EXPECT(tests, (worker_context.read(message) && message.copy_payload_to(work)));
        if (work.reclaim) work.reclaim->deallocate();
        if (work.process) work.process->execute();
        if (acknowledge)
        {
            service.complete(threading::make_pod_message(message.query_async_slot(),
                SchemaProcessDone{ work.reclaim != nullptr }), worker);
        }
        return message;
    }
    template<class Configure>
    SchemaProcessResult run(const Configure& configure, const bool admitted, const Status expected)
    {
        submit(configure);
        if (admitted) accepted();
        (void)step();
        SchemaProcessResult result = terminal(expected);
        service.complete_disposals();
        if (admitted && (expected != Status::success)) (void)step();
        TEST_EXPECT(tests, !client.reserved_replies);
        return result;
    }
    void release(const CAssetAcquisitionId acquisition)
    {
        service.request_release({ acquisition }, 1, client);
        TEST_EXPECT(tests, (read<AssetReleaseResult>().status == EAssetStatus::success));
    }
    void dispose(const CAssetId id)
    {
        service.request_disposal({ id }, 1, client);
        service.complete_disposals();
        TEST_EXPECT(tests, (read<AssetDisposeResult>().status == EAssetStatus::success));
        service.complete_disposals();
        (void)step();
    }
};

void process_operations(TTestContext& ctx)
{
    SProcessFixture f{ ctx };
    const auto existing = [](SchemaProcessRequest&) {};
    const auto declaration = f.run(existing, true, Status::success).success;
    if (!declaration.descriptor) return;
    const auto first = declaration.descriptor->bytes;
    for (const Output output : { Output::schema_document, Output::runtime_data_block })
    {
        for (const Assembly assembly : { Assembly::existing, Assembly::compose_roles, Assembly::merge_two })
        {
            const auto result = f.run([&](SchemaProcessRequest& request)
            {
                request.output_kind = output;
                request.operation = assembly;
                SProcessFixture::retained(request.inputs.slots[0], declaration.asset);
                if (assembly == Assembly::compose_roles) request.types_source = 0;
                else if (assembly == Assembly::merge_two)
                {
                    request.input_count = 2u;
                    SProcessFixture::retained(request.inputs.slots[1], declaration.asset);
                }
            }, true, Status::success);
            TEST_EXPECT(ctx, ((result.output_kind == output) && result.success.descriptor &&
                (result.success.descriptor->version == 1u) && (result.success.descriptor->kind == output)));
            if (!result.success.descriptor) return;
            const CByteConstView bytes = result.success.descriptor->bytes;
            TEST_EXPECT(ctx, (bytes.is_ready() && (bytes.align() >= ((output == Output::schema_document) ? 32u : 128u))));
            if (output == Output::runtime_data_block)
            {
                (void)f.run([&](SchemaProcessRequest& request)
                {
                    SProcessFixture::retained(request.inputs.slots[0], result.success.asset);
                }, false, Status::invalid_source);
            }
            f.release(result.success.acquisition);
            f.dispose(result.success.asset);
        }
    }
    f.service.request_acquire({ declaration.asset, EAssetAcquireView::bytes }, 3, f.client);
    const auto second = f.read<AssetAcquireResult>();
    TEST_EXPECT(ctx, ((second.status == EAssetStatus::success) && (second.views.bytes.data() == first.data())));
    f.service.request_disposal({ declaration.asset }, 4, f.client);
    (void)f.run([&](SchemaProcessRequest& request)
    {
        SProcessFixture::retained(request.inputs.slots[0], declaration.asset);
    }, false, Status::invalid_source);
    TEST_EXPECT(ctx, (declaration.descriptor->bytes.data() == first.data()));
    const auto first_byte = first.data()[0];
    f.release(declaration.acquisition);
    f.service.complete_disposals();
    TEST_EXPECT(ctx, (second.views.bytes.data()[0] == first_byte));
    f.release(second.acquisition);
    f.service.complete_disposals();
    TEST_EXPECT(ctx, (f.read<AssetDisposeResult>().status == EAssetStatus::success));
    f.service.complete_disposals();
    (void)f.step();
    TEST_EXPECT(ctx, f.service.is_idle());
}

void process_rejection(TTestContext& ctx)
{
    SProcessFixture f{ ctx };
    auto invalid_slot = threading::make_owner_message<SchemaProcessRequest>(-1);
    invalid_slot.owner().payload<SchemaProcessRequest>()->request_id = { f.next++ };
    TEST_EXPECT(ctx, f.client.reserve_replies(2u));
    f.service.request_process(invalid_slot, f.client, false, false);
    (void)f.step();
    threading::CErasedPodMsg message;
    SchemaProcessResult rejected;
    TEST_EXPECT(ctx, (f.recipient_context.read(message) && (message.query_async_slot() == -1) &&
        message.copy_payload_to(rejected) && (rejected.status == Status::invalid_request) &&
        (rejected.failure.reason == static_cast<std::uint16_t>(ESchemaAdmissionReason::slot)) && !f.client.reserved_replies));
    for (unsigned i = 0u; i < 23u; ++i)
    {
        (void)f.run([&](SchemaProcessRequest& r)
        {
            switch (i)
            {
                case 0:
                {
                    r.contract_version = 2u;
                    break;
                }
                case 1:
                {
                    r.request_id.value = 0u;
                    break;
                }
                case 2:
                {
                    r.input_count = 0u;
                    break;
                }
                case 3:
                {
                    r.input_count = 4u;
                    break;
                }
                case 4:
                {
                    r.operation = static_cast<Assembly>(99);
                    break;
                }
                case 5:
                {
                    r.output_kind = Output::none;
                    break;
                }
                case 6:
                {
                    r.types_source = 0;
                    break;
                }
                case 7:
                {
                    r.operation = Assembly::compose_roles;
                    break;
                }
                case 8:
                {
                    r.operation = Assembly::compose_roles;
                    r.types_source = 1;
                    break;
                }
                case 9:
                {
                    r.operation = Assembly::compose_roles;
                    r.types_source = -2;
                    break;
                }
                case 10:
                {
                    r.operation = Assembly::compose_roles;
                    r.types_source = 0;
                    r.input_count = 2u;
                    break;
                }
                case 11:
                {
                    r.operation = Assembly::merge_two;
                    break;
                }
                case 12:
                {
                    r.inputs.slots[0].asset = CAssetId{ 1u };
                    break;
                }
                case 13:
                {
                    r.inputs.slots[0].kind = ESchemaInput::retained_document;
                    break;
                }
                case 14:
                {
                    r.inputs.slots[2].kind = ESchemaInput::owned_document;
                    break;
                }
                case 15:
                {
                    r.inputs.slots[1].asset = CAssetId{ 1u };
                    break;
                }
                case 16:
                {
                    r.deadline.duration_ns = 1u;
                    break;
                }
                case 17:
                {
                    r.deadline.generation = 23u;
                    break;
                }
                case 18:
                {
                    r.inputs.slots[0].kind = static_cast<ESchemaInput>(99);
                    break;
                }
                case 19:
                {
                    r.output_kind = static_cast<Output>(99);
                    break;
                }
                case 20:
                {
                    r.inputs.slots[0].document = {};
                    break;
                }
                case 21:
                {
                    r.inputs.slots[0].kind = ESchemaInput::none;
                    break;
                }
                case 22:
                {
                    TEST_EXPECT(ctx, SProcessFixture::owned(r.inputs.slots[2], R"({"types":{}})"));
                    r.inputs.slots[2].kind = ESchemaInput::none;
                    break;
                }
            }
        }, false, Status::invalid_request);
    }
    for (const Limit kind : { Limit::inspected_source_bytes, Limit::owned_bytes, Limit::result_bytes,
        Limit::work_credits, Limit::document_nodes, Limit::records, Limit::depth })
    {
        const auto result = f.run([&](SchemaProcessRequest& r)
        {
            switch (kind)
            {
                case Limit::inspected_source_bytes:
                {
                    ++r.limits.inspected_source_bytes;
                    break;
                }
                case Limit::owned_bytes:
                {
                    ++r.limits.owned_bytes;
                    break;
                }
                case Limit::result_bytes:
                {
                    ++r.limits.result_bytes;
                    break;
                }
                case Limit::work_credits:
                {
                    ++r.limits.work_credits;
                    break;
                }
                case Limit::document_nodes:
                {
                    ++r.limits.document_nodes;
                    break;
                }
                case Limit::records:
                {
                    ++r.limits.records;
                    break;
                }
                case Limit::depth:
                {
                    ++r.limits.depth;
                    break;
                }
                default:
                {
                    break;
                }
            }
        }, false, Status::invalid_request);
        TEST_EXPECT(ctx, ((result.failure.limit == kind) && (result.failure.requested == (result.failure.allowed + 1u))));
    }
    (void)f.run([&](SchemaProcessRequest& r) { r.request_id.value = f.next - 2u; }, false, Status::invalid_request);
    (void)f.run([&](SchemaProcessRequest& r) { SProcessFixture::retained(r.inputs.slots[0], f.asset); }, false, Status::invalid_source);
    (void)f.run([](SchemaProcessRequest& r) { SProcessFixture::retained(r.inputs.slots[0], CAssetId{ UINT64_MAX }); },
        false, Status::invalid_source);
    const auto missing = f.run([](SchemaProcessRequest& r)
    {
        r.operation = Assembly::compose_roles;
        r.data_source = 0;
    }, true, Status::invalid_schema);
    TEST_EXPECT(ctx, ((missing.failure.input == 0) && (missing.failure.role == static_cast<std::uint8_t>(schema::ESchemaSource::bulk_data))));
    (void)f.run([&](SchemaProcessRequest& r)
    {
        r.operation = Assembly::merge_two;
        r.input_count = 2u;
        TEST_EXPECT(ctx, SProcessFixture::owned(r.inputs.slots[0], R"({"instances":{"u32":{"x":{"declaration":1}}}})"));
        TEST_EXPECT(ctx, SProcessFixture::owned(r.inputs.slots[1], R"({"instances":{"u32":{"x":{"declaration":2}}}})"));
    }, true, Status::conflict);
    for (const bool compatible : { true, false })
    {
        const auto result = f.run([&](SchemaProcessRequest& request)
        {
            request.operation = Assembly::merge_two;
            request.input_count = 2u;
            TEST_EXPECT(ctx, SProcessFixture::owned(request.inputs.slots[0],
                R"({"types":{"structures":{"V":{"members":[{"x":{"type":"u32"}}]}}}})"));
            const char* const second = compatible ?
                R"({"types":{"structures":{"V":{"detail":{"size":4,"alignment":4},"members":[{"x":{"type":"u32","offset":0}}]}}}})" :
                R"({"types":{"structures":{"V":{"members":[{"x":{"type":"u64"}}]}}}})";
            TEST_EXPECT(ctx, SProcessFixture::owned(request.inputs.slots[1], second));
        }, true, (compatible ? Status::success : Status::conflict));
        if (compatible && (result.status == Status::success))
        {
            f.release(result.success.acquisition);
            f.dispose(result.success.asset);
        }
    }
    (void)f.run([&](SchemaProcessRequest& r)
    {
        r.output_kind = Output::runtime_data_block;
        TEST_EXPECT(ctx, SProcessFixture::owned(r.inputs.slots[0], R"({"instances":{}})"));
    }, true, Status::invalid_schema);
    for (const Limit kind : { Limit::inspected_source_bytes, Limit::owned_bytes, Limit::result_bytes,
        Limit::document_nodes, Limit::records, Limit::work_credits, Limit::depth })
    {
        const auto result = f.run([&](SchemaProcessRequest& r)
        {
            switch (kind)
            {
                case Limit::inspected_source_bytes:
                {
                    r.limits.inspected_source_bytes = 1u;
                    break;
                }
                case Limit::owned_bytes:
                {
                    r.limits.owned_bytes = 1u;
                    break;
                }
                case Limit::result_bytes:
                {
                    r.limits.result_bytes = 1u;
                    break;
                }
                case Limit::document_nodes:
                {
                    r.limits.document_nodes = 2u;
                    break;
                }
                case Limit::work_credits:
                {
                    r.limits.work_credits = 12u;
                    break;
                }
                case Limit::depth:
                {
                    r.limits.depth = 1u;
                    break;
                }
                case Limit::records:
                {
                    r.limits.records = 1u;
                    r.operation = Assembly::merge_two;
                    r.input_count = 2u;
                    for (std::uint32_t i = 0u; i < 2u; ++i)
                    {
                        TEST_EXPECT(ctx, SProcessFixture::owned(r.inputs.slots[i], R"({"data":{"u32":{"x":{"data":[1]}}}})"));
                    }
                    break;
                }
                default:
                {
                    break;
                }
            }
        }, true, Status::limit_exceeded);
        TEST_EXPECT(ctx, ((result.failure.limit == kind) && (result.failure.requested >= result.failure.allowed)));
    }
}

void process_transport_and_pins(TTestContext& ctx)
{
    SProcessFixture f{ ctx };
    auto& pipe = threading::SDirectConnectionTestAccess::resources(f.recipient).worker_to_host_owned_msgs;
    pipe.deallocate();
    TEST_EXPECT(ctx, pipe.initialise(2u));
    auto envelope = threading::make_owner_message<SchemaProcessRequest>(SProcessFixture::slot);
    auto* const request = envelope.owner().payload<SchemaProcessRequest>();
    if (!request) return;
    request->request_id.value = f.next++;
    request->input_count = 1u;
    request->output_kind = Output::schema_document;
    TEST_EXPECT(ctx, SProcessFixture::owned(request->inputs.slots[0], R"({"types":{},"instances":{}})"));
    while (pipe.writable_count())
    {
        auto filler = threading::make_owner_message<SchemaProcessRequest>(0);
        TEST_EXPECT(ctx, f.recipient_context.post(std::move(filler)));
    }
    TEST_EXPECT(ctx, (!f.recipient_context.post(std::move(envelope)) && envelope.has_owner()));
    threading::CErasedOwnerMsg read;
    while (f.recipient.read(read)) {}
    TEST_EXPECT(ctx, (f.recipient_context.post(std::move(envelope)) && !envelope.has_owner() && f.recipient.read(read)));
    TEST_EXPECT(ctx, f.client.reserve_replies(2u));
    f.service.request_process(read, f.client, false, false);
    f.accepted();
    (void)f.step();
    const auto declarations = f.terminal(Status::success).success;
    f.release(declarations.acquisition);
    auto internal = threading::make_owner_message<SchemaProcessRequest>(SProcessFixture::slot);
    SchemaProcessRequest& compose = *internal.owner().payload<SchemaProcessRequest>();
    compose.request_id = { 1u };
    compose.operation = Assembly::compose_roles;
    compose.output_kind = Output::schema_document;
    compose.input_count = 1u;
    compose.types_source = compose.instances_source = 0;
    SProcessFixture::retained(compose.inputs.slots[0], declarations.asset);
    host::SClientRecord consumer{ module_ids::executable, 55u };
    consumer.phase = host::EClientPhase::ready;
    TEST_EXPECT(ctx, f.service.post_internal_process(internal, consumer));
    TEST_EXPECT(ctx, (!internal.has_owner() && (consumer.message_count == 1u) &&
        consumer.messages[0].is_payload_a<SchemaProcessAccepted>()));
    f.service.request_disposal({ declarations.asset }, 7, f.client);
    f.service.complete_disposals();
    threading::CErasedPodMsg no_reply;
    TEST_EXPECT(ctx, !f.recipient_context.read(no_reply));
    (void)f.step();
    SchemaProcessResult result;
    TEST_EXPECT(ctx, ((consumer.message_count == 2u) && consumer.messages[1].copy_payload_to(result) &&
        (result.status == Status::success) && !consumer.reserved_replies));
    f.service.complete_disposals();
    TEST_EXPECT(ctx, (f.read<AssetDisposeResult>().status == EAssetStatus::success));
    (void)f.step();
    TEST_EXPECT(ctx, (result.success.descriptor && result.success.descriptor->bytes.is_ready()));
    f.service.request_release({ result.success.acquisition }, 8, f.client);
    TEST_EXPECT(ctx, (f.read<AssetReleaseResult>().status == EAssetStatus::invalid_request));
    f.service.request_release({ result.success.acquisition }, 9, consumer);
    f.dispose(result.success.asset);
}

void process_drain(TTestContext& ctx)
{
    SProcessFixture f{ ctx };
    const auto existing = [](SchemaProcessRequest&) {};
    const auto historical = f.run(existing, true, Status::success).success;
    f.submit(existing);
    f.accepted();
    // Dequeue and execute, then lose the worker completion. Actual production
    // shutdown supplies this proof only after all workers stop and queues drain.
    (void)f.step(false);
    f.worker_context.mark_exited();
    f.service.fail_pending();
    (void)f.terminal(Status::delivery_failed);
    TEST_EXPECT(ctx, (historical.descriptor && historical.descriptor->bytes.is_ready()));
    f.release(historical.acquisition);
    f.service.request_disposal({ historical.asset }, 1, f.client);
    f.service.complete_disposals();
    TEST_EXPECT(ctx, (f.read<AssetDisposeResult>().status == EAssetStatus::success));
    TEST_EXPECT(ctx, f.service.is_idle());
}

void process_worker_failure(TTestContext& ctx)
{
    tests::TAllocationFailureFixture allocator_state;
    memory::CMemoryAllocator allocator{ &allocator_state, tests::allocate_with_failure, tests::deallocate_test_memory, system_ids::host };
    memory::CMemoryContext memory{ allocator, system_ids::host };
    tests::TMemoryContextScope scope{ &memory };
    SProcessFixture f{ ctx };
    f.submit([](SchemaProcessRequest&) {});
    f.accepted();
    allocator_state.fail_on = allocator_state.attempts;
    (void)f.step();
    TEST_EXPECT(ctx, allocator_state.failed);
    (void)f.terminal(Status::allocation_failed);
    f.service.complete_disposals();
    (void)f.step();

    f.client.invocation_offered = true;
    f.submit([](SchemaProcessRequest&) {});
    f.fill(f.worker);
    allocator_state.failed = false;
    allocator_state.fail_on = allocator_state.attempts + 1u;
    f.client.invocation_offered = false;
    f.service.complete_disposals();
    f.accepted();
    TEST_EXPECT(ctx, (f.worker.pod_delivery_poisoned() && allocator_state.failed && f.service.failed()));
    threading::CErasedPodMsg message;
    unsigned offered{};
    while (f.worker_context.read(message))
    {
        SchemaProcessWork work;
        if (message.copy_payload_to(work))
        {
            ++offered;
            TEST_EXPECT(ctx, (work.process && work.process->envelope.is_ready()));
        }
    }
    TEST_EXPECT(ctx, ((offered == 1u) && !f.recipient_context.read(message) && !f.service.work_idle()));
    f.worker_context.mark_exited();
    f.service.fail_pending();
    (void)f.terminal(Status::delivery_failed);
    TEST_EXPECT(ctx, (f.service.is_idle() && !f.client.reserved_replies));
}

void process_departure(TTestContext& ctx)
{
    SProcessFixture f{ ctx };
    f.submit([](SchemaProcessRequest&) {});
    f.accepted();
    const auto late = f.step(false);
    f.recipient_context.mark_exited();
    host::SClientRecord replacement{ f.client.module, (f.client.identity.value + 1u) };
    f.service.release_client_acquisitions(f.client);
    TEST_EXPECT(ctx, (!f.service.client_idle(f.client) && f.service.client_idle(replacement)));
    f.service.complete(threading::make_pod_message(late.query_async_slot(), SchemaProcessDone{}), f.worker);
    f.service.complete_disposals();
    (void)f.step();
    threading::CErasedPodMsg message;
    TEST_EXPECT(ctx, (!f.recipient_context.read(message) && !replacement.message_count && f.service.is_idle()));
}

void process_shared_budget(TTestContext& ctx)
{
    schema::SSchemaAssemblyPlan plan{ Assembly::compose_roles, Output::runtime_data_block, 3u, { 0, 1, 2 } };
    CLiveDocument types, instances, data;
    TEST_EXPECT(ctx, tests::parse_document(R"({"types":{}})", types));
    TEST_EXPECT(ctx, tests::parse_document(R"({"instances":{"u32":{"x":{"declaration":1}}}})", instances));
    TEST_EXPECT(ctx, tests::parse_document(R"({"data":{"u32":{"x":{"data":[3,4]}}}})", data));
    CBakedDocumentBlock before;
    TEST_EXPECT(ctx, document_translation::bake(data, before));
    const schema::SSchemaProcessSource sources[3]{ { &types }, { &instances }, { &data } };
    schema::SSchemaProcessingDiagnostic diagnostic;
    schema::SDataOperationLimits limits;
    CBakedDocumentBlock full;
    TEST_EXPECT(ctx, schema::process_schema(plan, sources, limits, full, diagnostic));
    if (!full.is_ready()) return;
    // Find the exact shared credit boundary. Every failing probe must preserve
    // all sources and the empty destination, including late typed bake failures.
    std::uint64_t low{}, high = limits.work_credits;
    while ((low + 1u) < high)
    {
        const std::uint64_t trial = low + ((high - low) >> 1u);
        limits.work_credits = trial;
        CBakedDocumentBlock candidate;
        if (schema::process_schema(plan, sources, limits, candidate, diagnostic)) high = trial;
        else
        {
            TEST_EXPECT(ctx, (!candidate.buffer_capacity() && (diagnostic.operation.limit == Limit::work_credits)));
            low = trial;
        }
    }
    CBakedDocumentBlock candidate;
    limits.work_credits = low;
    TEST_EXPECT(ctx, !schema::process_schema(plan, sources, limits, candidate, diagnostic));
    TEST_EXPECT(ctx, ((diagnostic.encoding == Output::runtime_data_block) &&
        (diagnostic.operation.requested > diagnostic.operation.allowed) && !candidate.buffer_capacity()));
    limits.work_credits = high;
    TEST_EXPECT(ctx, schema::process_schema(plan, sources, limits, candidate, diagnostic));
    TEST_EXPECT(ctx, tests::same_baked_document(data, before));
}

void process_pressure(TTestContext& ctx)
{
    SProcessFixture f{ ctx };
    const auto existing = [](SchemaProcessRequest&) {};
    f.client.invocation_offered = true;
    f.submit(existing);
    threading::CErasedPodMsg message;
    TEST_EXPECT(ctx, (!f.worker_context.read(message) && !f.recipient_context.read(message)));
    f.client.invocation_offered = false;
    f.service.complete_disposals();
    f.accepted();
    // A caller with early cancellation intent sends only after this receipt.
    host::SClientRecord other{ module_ids::audio, 29u };
    f.service.cancel_process({ { 1u } }, other);
    f.service.cancel_process({ { 1u }, 2u }, f.client);
    for (unsigned i = 0u; i < 3u; ++i)
    {
        f.submit(existing);
        f.accepted();
    }
    f.submit(existing);
    TEST_EXPECT(ctx, !f.service.schema_ingress_available());
    f.service.cancel_process({ { 1u } }, f.client);
    TEST_EXPECT(ctx, !f.recipient_context.read(message));
    (void)f.step();
    (void)f.terminal(Status::cancelled);
    SSchemaProcessSuccess granted[2];
    (void)f.step();
    granted[0] = f.terminal(Status::success).success;
    f.service.cancel_process({ { 2u } }, f.client);
    (void)f.step();
    granted[1] = f.terminal(Status::success).success;
    // The worker built a valid result. Cancellation wins before publication.
    const auto late = f.step(false);
    f.service.cancel_process({ { 4u } }, f.client);
    TEST_EXPECT(ctx, !f.recipient_context.read(message));
    f.service.complete(threading::make_pod_message(late.query_async_slot(), SchemaProcessDone{}), f.worker);
    (void)f.terminal(Status::cancelled);
    (void)f.step();
    (void)f.terminal(Status::busy);
    TEST_EXPECT(ctx, (f.service.schema_ingress_available() && !f.client.reserved_replies));
    f.service.complete_disposals();
    (void)f.step();
    (void)f.step();
    for (const auto& success : granted)
    {
        TEST_EXPECT(ctx, (success.descriptor && success.descriptor->bytes.is_ready()));
        f.release(success.acquisition);
        f.dispose(success.asset);
    }
}

void process_deadlines(TTestContext& ctx)
{
    SProcessFixture f{ ctx };
    modules::CHostRestrictionPublication clock;
    modules::SHostRestrictionSnapshot state;
    state.generation = 1u;
    TEST_EXPECT(ctx, clock.publish(state));
    f.client.host_execution.restriction = clock.reader();
    modules::CHostEligibleBudget budget;
    TEST_EXPECT(ctx, (budget.start(clock.reader(), 0u) == modules::EBudgetStatus::expired));
    (void)f.run([&](SchemaProcessRequest& r) { r.deadline = budget.anchor(); }, false, Status::expired);
    for (const bool after_work : { false, true })
    {
        // Freeze eligible time at the sample during admission and dispatch.
        const platform::threading::SAwakeClock awake;
        TEST_EXPECT(ctx, (awake.read(state.active_start_ns).code == platform::threading::EWaitError::none));
        state.depth = 1u;
        TEST_EXPECT(ctx, clock.publish(state));
        TEST_EXPECT(ctx, (budget.start(clock.reader(), 1u) == modules::EBudgetStatus::restricted));
        f.submit([&](SchemaProcessRequest& r) { r.deadline = budget.anchor(); });
        f.accepted();
        if (after_work)
        {
            const auto complete = f.step(false);
            state.depth = 0u;
            TEST_EXPECT(ctx, clock.publish(state));
            f.service.complete(threading::make_pod_message(complete.query_async_slot(), SchemaProcessDone{}), f.worker);
        }
        else
        {
            state.depth = 0u;
            TEST_EXPECT(ctx, clock.publish(state));
            (void)f.step();
        }
        (void)f.terminal(Status::expired);
        f.service.complete_disposals();
        (void)f.step();
    }
}

void process_poison(TTestContext& ctx, const bool at_acceptance)
{
    tests::TAllocationFailureFixture allocation;
    memory::CMemoryAllocator allocator{ &allocation, tests::allocate_with_failure, tests::deallocate_test_memory, system_ids::host };
    memory::CMemoryContext memory{ allocator, system_ids::host };
    tests::TMemoryContextScope scope{ &memory };
    SProcessFixture f{ ctx };
    f.client.invocation_offered = true;
    f.submit([](SchemaProcessRequest&) {});
    f.client.invocation_offered = false;
    if (!at_acceptance)
    {
        f.service.complete_disposals();
        f.accepted();
        f.client.invocation_offered = true;
        (void)f.step();
        f.client.invocation_offered = false;
    }
    f.fill(f.recipient);
    allocation.fail_on = allocation.attempts + 1u;
    f.service.complete_disposals();
    TEST_EXPECT(ctx, (allocation.failed && f.recipient.pod_delivery_poisoned() && f.service.failed()));
    SchemaProcessResult exposed;
    unsigned accepted{}, results{};
    threading::CErasedPodMsg message;
    while (f.recipient_context.read(message))
    {
        if (message.is_payload_a<SchemaProcessAccepted>()) ++accepted;
        if (message.copy_payload_to(exposed)) ++results;
    }
    if (at_acceptance)
    {
        TEST_EXPECT(ctx, ((accepted == 1u) && !results && (f.client.reserved_replies == 1u)));
        (void)f.step();
    }
    else
    {
        TEST_EXPECT(ctx, (!accepted && (results == 1u) && (exposed.status == Status::success)));
        if (exposed.status != Status::success) return;
        f.service.release_client_acquisitions(f.client);
        f.service.complete_disposals();
        TEST_EXPECT(ctx, (exposed.success.descriptor && exposed.success.descriptor->bytes.is_ready()));
        TEST_EXPECT(ctx, !f.worker_context.read(message));
        f.recipient_context.mark_exited();
        f.service.release_client_acquisitions(f.client);
        TEST_EXPECT(ctx, !f.service.client_idle(f.client));
        f.client.terminal_before_drain = f.client.incoming_drained = true;
        f.service.release_client_acquisitions(f.client);
    }
    TEST_EXPECT(ctx, !f.client.reserved_replies);
    f.service.complete_disposals();
    (void)f.step();
    TEST_EXPECT(ctx, f.service.is_idle());
}

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

static void run_process_cases(tests::TTestContext& ctx)
{
    process_operations(ctx);
    process_rejection(ctx);
    process_transport_and_pins(ctx);
    process_drain(ctx);
    process_worker_failure(ctx);
    process_departure(ctx);
    process_shared_budget(ctx);
    process_pressure(ctx);
    process_deadlines(ctx);
    process_poison(ctx, true);
    process_poison(ctx, false);
}

int run_schema_process_focused()
{
    tests::TTestContext ctx;
    run_process_cases(ctx);
    return ctx.report("SchemaProcess");
}

void run_schema_service_cases(tests::TTestContext& ctx)
{
    run_process_cases(ctx);
    poison(ctx, 0u);
    poison(ctx, 1u);
    poison(ctx, 2u);
    vanished_recipient(ctx);
}
