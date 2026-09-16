
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    ErasedPod_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Aug 26

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>

#include "containers/TInstance.hpp"
#include "debug/service.hpp"
#include "tests/environment/local_type_ids.hpp"
#include "tests/environment/test_paths.hpp"
#include "system/erased_pod.hpp"
#include "system/system_context.hpp"
#include "system/transported_types.hpp"
#include "tests/test_suites/ErasedPod_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/file_helpers.hpp"
#include "threading/messages/CErasedMessageTransports.hpp"
#include "threading/messages/CErasedPodMsg.hpp"

namespace erased_pod_test_types
{
struct SLocalPod { std::uint32_t value; };
struct SUnregisteredSystemPod { std::uint32_t value; };
}

MV_REGISTER_LOCAL_TYPE(
    erased_pod_test_types::SLocalPod,
    local_type_ids::ops::encode_id(local_type_ids::ops::encode_index(1234u)));

// morphic-policy: suppress-next-line GID002 reason="negative test requires an out-of-catalogue system type binding"
MV_REGISTER_SYSTEM_TYPE(
    erased_pod_test_types::SUnregisteredSystemPod,
    system_type_ids::ops::encode_id(
        system_type_ids::ops::encode_index(system_type_ids::k_count)));

namespace
{

using TTestContext = tests::TTestContext;

struct CCanonicalValue
{
    CCanonicalValue() noexcept : value(0u) {}
    std::uint32_t value;
};

struct CNonTriviallyCopyable
{
    CNonTriviallyCopyable() noexcept = default;
    CNonTriviallyCopyable(const CNonTriviallyCopyable& other) noexcept : value(other.value) {}
    std::uint32_t value;
};

struct alignas(16) SAlignedPod
{
    std::uint64_t values[2];
};

struct alignas(32) SOverAlignedPod
{
    std::uint64_t values[4];
};

struct SOversizedPod
{
    std::uint64_t values[7];
};

void test_erased_pod_redefinition_and_access(TTestContext& ctx)
{
    using storage_type = TErasedPod<sizeof(UnrecognisedMsg), alignof(UnrecognisedMsg)>;

    static_assert(std::is_trivially_copyable_v<storage_type>);
    static_assert(std::is_standard_layout_v<storage_type>);
    static_assert(storage_type::k_payload_align == 16u);
    static_assert(alignof(storage_type) == 16u);
    static_assert(storage_type::is_compatible_with<UnrecognisedMsg>());
    static_assert(storage_type::is_compatible_with<FileSaveResult>());
    static_assert(!std::is_trivial_v<CCanonicalValue>);
    static_assert(std::is_trivially_copyable_v<CCanonicalValue>);
    static_assert(storage_type::is_compatible_with<CCanonicalValue>());
    static_assert(!storage_type::is_compatible_with<CNonTriviallyCopyable>());

    storage_type storage;
    TEST_EXPECT(ctx, storage.query_type_id() == type_ids::undefined);
    TEST_EXPECT(ctx, storage.query_tag() == 0u);
    TEST_EXPECT(ctx, storage.payload<UnrecognisedMsg>() == nullptr);

    storage.set_tag(41u);
    UnrecognisedMsg& unrecognised = storage.redefine<UnrecognisedMsg>();
    unrecognised.msg_id = system_type_ids::file_load_request;

    TEST_EXPECT(ctx, storage.query_type_id() == k_type_id_v<UnrecognisedMsg>);
    TEST_EXPECT(ctx, storage.query_tag() == 41u);
    TEST_EXPECT(ctx, storage.is_a<UnrecognisedMsg>());
    TEST_EXPECT(ctx, storage.payload<UnrecognisedMsg>() == &unrecognised);
    TEST_EXPECT(ctx, storage.payload<UnrecognisedMsg>()->msg_id == system_type_ids::file_load_request);

    const storage_type& const_storage = storage;
    TEST_EXPECT(ctx, const_storage.payload<UnrecognisedMsg>() == &unrecognised);

    FileSaveResult& result = storage.redefine<FileSaveResult>();
    TEST_EXPECT(ctx, !result.success);
    TEST_EXPECT(ctx, storage.query_tag() == 41u);
    result.success = true;

    TEST_EXPECT(ctx, storage.query_type_id() == k_type_id_v<FileSaveResult>);
    TEST_EXPECT(ctx, storage.payload<UnrecognisedMsg>() == nullptr);
    TEST_EXPECT(ctx, storage.payload<FileSaveResult>() == &result);
    TEST_EXPECT(ctx, storage.payload<FileSaveResult>()->success);
}

void test_local_erased_pod(TTestContext& ctx)
{
    using local_type = erased_pod_test_types::SLocalPod;
    TErasedPod<16u> storage;
    local_type& payload = storage.redefine<local_type>();
    payload.value = 73u;

    TEST_EXPECT(ctx, storage.query_type_id() == k_type_id_v<local_type>);
    TEST_EXPECT(ctx, storage.query_type_id().is_local());
    TEST_EXPECT(ctx, !storage.query_type_id().is_system());
    TEST_EXPECT(ctx, storage.is_a<local_type>());
    TEST_EXPECT(ctx, storage.payload<local_type>() != nullptr);
    TEST_EXPECT(ctx, storage.payload<local_type>()->value == 73u);
    TEST_EXPECT(ctx, storage.payload<FileSaveResult>() == nullptr);
}

void test_erased_pod_clears_complete_storage_extent(TTestContext& ctx)
{
    using storage_type = TErasedPod<16u, 16u>;
    static_assert(std::is_trivially_copyable_v<storage_type>);
    static_assert(sizeof(storage_type) == 32u);

    storage_type reused;
    reused.set_tag(73u);
    std::memset(
        reinterpret_cast<unsigned char*>(&reused) + 16u, 0xff,
        sizeof(reused) - 16u);
    FileSaveResult& redefined = reused.redefine<FileSaveResult>();

    storage_type fresh;
    fresh.set_tag(73u);
    FileSaveResult& initial = fresh.redefine<FileSaveResult>();

    TEST_EXPECT(ctx, !redefined.success);
    TEST_EXPECT(ctx, !initial.success);
    TEST_EXPECT(ctx, reused.query_tag() == 73u);
    TEST_EXPECT(ctx, std::memcmp(&reused, &fresh, sizeof(storage_type)) == 0);
}

void test_erased_pod_header_and_extended_alignment(TTestContext& ctx)
{
    using minimum_storage_type = TErasedPod<16u, 1u>;
    using extended_storage_type = TErasedPod<16u, 32u>;

    static_assert(minimum_storage_type::k_payload_align == 16u);
    static_assert(minimum_storage_type::is_compatible_with<SAlignedPod>());
    static_assert(alignof(minimum_storage_type) == 16u);
    static_assert(extended_storage_type::k_payload_align == 32u);
    static_assert(alignof(extended_storage_type) == 32u);
    static_assert(sizeof(extended_storage_type) == 64u);

    minimum_storage_type initial;
    const unsigned char* const initial_bytes = reinterpret_cast<const unsigned char*>(&initial);
    bool reserved_is_zero = true;
    for (std::size_t byte_index = 8u; byte_index < 16u; ++byte_index)
    {
        reserved_is_zero = reserved_is_zero && (initial_bytes[byte_index] == 0u);
    }
    TEST_EXPECT(ctx, reserved_is_zero);

    extended_storage_type reused;
    reused.set_tag(19u);
    std::memset(
        reinterpret_cast<unsigned char*>(&reused) + 16u, 0xff,
        sizeof(reused) - 16u);
    (void)reused.redefine<FileSaveResult>();

    extended_storage_type fresh;
    fresh.set_tag(19u);
    (void)fresh.redefine<FileSaveResult>();

    TEST_EXPECT(ctx, reused.query_tag() == 19u);
    TEST_EXPECT(ctx, std::memcmp(&reused, &fresh, sizeof(reused)) == 0);
}

void test_thread_message_copy_boundary(TTestContext& ctx)
{
    using threading::CErasedPodMsg;

    static_assert(std::is_same_v<
        decltype(CErasedPodMsg{}.query_message_type_id()), type_id>);
    static_assert(std::is_trivially_copyable_v<CErasedPodMsg>);
    static_assert(std::is_standard_layout_v<CErasedPodMsg>);
    static_assert(CErasedPodMsg::is_payload_compatible_with<FileSaveResult>());
    static_assert(CErasedPodMsg::is_payload_compatible_with<SAlignedPod>());
    static_assert(CErasedPodMsg::is_payload_compatible_with<CCanonicalValue>());
    static_assert(CErasedPodMsg::is_payload_compatible_with<FileSaveRequest>());
    static_assert(!CErasedPodMsg::is_payload_compatible_with<TgaLoadRequest>());
    static_assert(!CErasedPodMsg::is_payload_compatible_with<TgaSaveRequest>());
    static_assert(CErasedPodMsg::is_payload_compatible_with<TgaEncodeRequest>());
    static_assert(CErasedPodMsg::is_payload_compatible_with<TgaDecodeRequest>());
    static_assert(!CErasedPodMsg::is_payload_compatible_with<CNonTriviallyCopyable>());
    static_assert(!CErasedPodMsg::is_payload_compatible_with<SOverAlignedPod>());
    static_assert(!CErasedPodMsg::is_payload_compatible_with<SOversizedPod>());
    static_assert(sizeof(CErasedPodMsg) == 64u);
    static_assert(alignof(CErasedPodMsg) == 16u);

    CErasedPodMsg message;
    const unsigned char zero_message[sizeof(CErasedPodMsg)]{};
    TEST_EXPECT(ctx, std::memcmp(&message, zero_message, sizeof(message)) == 0);
    TEST_EXPECT(ctx, !message.has_message_type());
    TEST_EXPECT(ctx, message.query_message_type_id() == type_ids::undefined);
    TEST_EXPECT(ctx, message.query_async_slot() == 0);

    message.set_async_slot(41);
    TEST_EXPECT(ctx, message.query_async_slot() == 41);

    FileSaveResult source{ true };
    message.assign_payload(source);
    source.success = false;

    TEST_EXPECT(ctx, message.has_message_type());
    TEST_EXPECT(ctx, message.query_async_slot() == 41);
    TEST_EXPECT(ctx, message.is_payload_a<FileSaveResult>());
    TEST_EXPECT(ctx, message.query_message_type_id() == k_type_id_v<FileSaveResult>);

    FileSaveResult copied{ false };
    TEST_EXPECT(ctx, message.copy_payload_to(copied));
    TEST_EXPECT(ctx, copied.success);

    copied.success = false;
    TEST_EXPECT(ctx, message.copy_payload_to(copied));
    TEST_EXPECT(ctx, copied.success);

    UnrecognisedMsg mismatch{ system_type_ids::tga_save_request };
    TEST_EXPECT(ctx, !message.copy_payload_to(mismatch));
    TEST_EXPECT(ctx, mismatch.msg_id == system_type_ids::tga_save_request);
}

void test_local_thread_message_carrier(TTestContext& ctx)
{
    using local_type = erased_pod_test_types::SLocalPod;

    threading::CErasedPodMsg message;
    message.set_async_slot(42);
    message.assign_payload(local_type{ 91u });

    TEST_EXPECT(ctx, message.has_message_type());
    TEST_EXPECT(ctx, message.query_message_type_id() == k_type_id_v<local_type>);
    TEST_EXPECT(ctx, message.query_message_type_id().is_local());
    TEST_EXPECT(ctx, message.is_payload_a<local_type>());
    local_type copied{};
    TEST_EXPECT(ctx, message.copy_payload_to(copied));
    TEST_EXPECT(ctx, copied.value == 91u);
    TEST_EXPECT(ctx, message.query_async_slot() == 42);
}

void test_thread_message_clears_previous_representation(TTestContext& ctx)
{
    threading::CErasedPodMsg reused;
    const TgaEncodeRequest larger{};
    reused.assign_payload(larger);

    const UnrecognisedMsg small{ system_type_ids::file_save_result };
    reused.assign_payload(small);

    threading::CErasedPodMsg fresh;
    fresh.assign_payload(small);

    TEST_EXPECT(ctx, std::memcmp(&reused, &fresh, sizeof(reused)) == 0);
}

void test_concrete_erased_pod_transport_admission(TTestContext& ctx)
{
    const module_ids::id_type previous_module_id =
        system_context::set_ambient_module_id(module_ids::executable);

    threading::CErasedPodMsg source;
    source.set_async_slot(17);
    source.assign_payload(FileSaveResult{ true });

    threading::transports::CErasedPodMsgTransport same_component(
        module_ids::executable);
    TEST_EXPECT(ctx, same_component.initialise_growable(4u));
    TEST_EXPECT(ctx, same_component.destination_module_id() == module_ids::executable);
    TEST_EXPECT(ctx, same_component.post(source));

    threading::CErasedPodMsg received;
    TEST_EXPECT(ctx, same_component.read(received));
    FileSaveResult result{ false };
    TEST_EXPECT(ctx, received.copy_payload_to(result));
    TEST_EXPECT(ctx, result.success);
    TEST_EXPECT(ctx, received.query_async_slot() == 17);
    same_component.deallocate();

    threading::transports::CErasedPodMsgTransport cross_component(
        module_ids::executive);
    TEST_EXPECT(ctx, cross_component.initialise_fixed(4u));
    TEST_EXPECT(ctx, cross_component.post(source));
    TEST_EXPECT(ctx, cross_component.read(received));
    cross_component.deallocate();

    threading::CErasedPodMsg local_source;
    local_source.set_async_slot(18);
    local_source.assign_payload(test_environment::STestTgaFileSaveState{
        18, CAssetId{}, CAssetId{} });
    const threading::CErasedPodMsg local_snapshot = local_source;

    threading::transports::CErasedPodMsgTransport local_same_component(
        module_ids::executable);
    TEST_EXPECT(ctx, local_same_component.initialise_fixed(2u));
    TEST_EXPECT(ctx, local_same_component.post(local_source));
    TEST_EXPECT(ctx, local_same_component.read(received));
    test_environment::STestTgaFileSaveState local_result{};
    TEST_EXPECT(ctx, received.copy_payload_to(local_result));
    TEST_EXPECT(ctx, local_result.executive_slot == 18);
    TEST_EXPECT(ctx, received.query_async_slot() == 18);
    local_same_component.deallocate();

    threading::transports::CErasedPodMsgTransport local_cross_component(
        module_ids::executive);
    TEST_EXPECT(ctx, local_cross_component.initialise_fixed(2u));
    TEST_EXPECT(ctx, !local_cross_component.post(local_source));
    TEST_EXPECT(ctx, std::memcmp(
        &local_source, &local_snapshot, sizeof(local_source)) == 0);
    TEST_EXPECT(ctx, local_cross_component.refresh_readable_count() == 0u);
    local_cross_component.deallocate();

    threading::CErasedPodMsg unregistered_local;
    unregistered_local.set_async_slot(19);
    unregistered_local.assign_payload(erased_pod_test_types::SLocalPod{ 19u });
    const threading::CErasedPodMsg unregistered_local_snapshot =
        unregistered_local;
    threading::transports::CErasedPodMsgTransport local_registration_gate(
        module_ids::executable);
    TEST_EXPECT(ctx, local_registration_gate.initialise_fixed(2u));
    TEST_EXPECT(ctx, !local_registration_gate.post(unregistered_local));
    TEST_EXPECT(ctx, std::memcmp(
        &unregistered_local, &unregistered_local_snapshot,
        sizeof(unregistered_local)) == 0);
    TEST_EXPECT(ctx, local_registration_gate.refresh_readable_count() == 0u);
    local_registration_gate.deallocate();

    threading::CErasedPodMsg unregistered_system;
    unregistered_system.set_async_slot(20);
    unregistered_system.assign_payload(
        erased_pod_test_types::SUnregisteredSystemPod{ 20u });
    const threading::CErasedPodMsg unregistered_system_snapshot =
        unregistered_system;
    threading::transports::CErasedPodMsgTransport system_registration_gate(
        module_ids::executive);
    TEST_EXPECT(ctx, system_registration_gate.initialise_fixed(2u));
    TEST_EXPECT(ctx, !system_registration_gate.post(unregistered_system));
    TEST_EXPECT(ctx, std::memcmp(
        &unregistered_system, &unregistered_system_snapshot,
        sizeof(unregistered_system)) == 0);
    TEST_EXPECT(ctx, system_registration_gate.refresh_readable_count() == 0u);
    system_registration_gate.deallocate();

    threading::CErasedPodMsg untyped;
    untyped.set_async_slot(21);
    const threading::CErasedPodMsg untyped_snapshot = untyped;
    threading::transports::CErasedPodMsgTransport identity_gate(
        module_ids::executable);
    TEST_EXPECT(ctx, identity_gate.initialise_fixed(2u));
    TEST_EXPECT(ctx, !identity_gate.post(untyped));
    TEST_EXPECT(ctx, std::memcmp(
        &untyped, &untyped_snapshot, sizeof(untyped)) == 0);
    TEST_EXPECT(ctx, identity_gate.refresh_readable_count() == 0u);
    identity_gate.deallocate();

    threading::transports::CErasedPodMsgTransport missing_source(
        module_ids::executable);
    TEST_EXPECT(ctx, missing_source.initialise_fixed(2u));
    const threading::CErasedPodMsg source_snapshot = source;
    (void)system_context::set_ambient_module_id();
    TEST_EXPECT(ctx, !missing_source.post(source));
    TEST_EXPECT(ctx, std::memcmp(
        &source, &source_snapshot, sizeof(source)) == 0);
    TEST_EXPECT(ctx, missing_source.refresh_readable_count() == 0u);
    (void)system_context::set_ambient_module_id(module_ids::executable);
    missing_source.deallocate();

    threading::transports::CErasedPodMsgTransport invalid_destination;
    TEST_EXPECT(ctx, !invalid_destination.initialise_growable(4u));
    TEST_EXPECT(ctx, !invalid_destination.posting_is_valid());
    TEST_EXPECT(ctx, !invalid_destination.reading_is_valid());

    (void)system_context::set_ambient_module_id(previous_module_id);
}

void test_concrete_erased_pod_transport_diagnostics(TTestContext& ctx)
{
    const std::string event_path_storage =
        test_environment::test_log_path("erased_transport_admission_test");
    const std::string direct_path_storage =
        test_environment::test_log_path("erased_transport_admission_test_direct");
    const char* const event_path = event_path_storage.c_str();
    const char* const direct_path = direct_path_storage.c_str();

    TInstance<debug_system::CDebugServiceState> service_owner =
        TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, service_owner.is_ready());
    debug_system::CDebugServiceState* const service =
        service_owner.operator->();
    TEST_EXPECT(ctx, service->configure_log_paths(event_path, direct_path));
    TEST_EXPECT(ctx, service->open_logs());
    service->publish_configuration(0u);
    TEST_EXPECT(ctx, debug_system::install_service(service));
    TEST_EXPECT(ctx, service->start());

    const module_ids::id_type previous_module_id =
        system_context::set_ambient_module_id(module_ids::executable);

    threading::CErasedPodMsg local_message;
    local_message.assign_payload(
        test_environment::STestTgaFileSaveState{ 22, CAssetId{}, CAssetId{} });
    threading::transports::CErasedPodMsgTransport local_cross_component(
        module_ids::executive);
    TEST_EXPECT(ctx, local_cross_component.initialise_fixed(1u));
    const std::uint32_t local_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, !local_cross_component.post(local_message));
    TEST_EXPECT(ctx, service->allocate_incident_id() == local_before + 2u);
    TEST_EXPECT(ctx, local_cross_component.refresh_readable_count() == 0u);
    local_cross_component.deallocate();

    threading::CErasedPodMsg system_message;
    system_message.assign_payload(FileSaveResult{ true });
    threading::transports::CErasedPodMsgTransport capacity_transport(
        module_ids::executable);
    TEST_EXPECT(ctx, capacity_transport.initialise_fixed(1u));
    const std::uint32_t success_before = service->allocate_incident_id();
    constexpr std::uint32_t capacity =
        threading::transports::TQueue<threading::CErasedPodMsg>::k_min_capacity;
    for (std::uint32_t index = 0u; index < capacity; ++index)
    {
        TEST_EXPECT(ctx, capacity_transport.post(system_message));
    }
    TEST_EXPECT(ctx, service->allocate_incident_id() == success_before + 1u);
    const std::uint32_t capacity_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, !capacity_transport.post(system_message));
    TEST_EXPECT(ctx, service->allocate_incident_id() == capacity_before + 1u);
    TEST_EXPECT(ctx, capacity_transport.refresh_readable_count() == capacity);
    capacity_transport.deallocate();

    threading::transports::CErasedPodMsgTransport missing_source(
        module_ids::executable);
    TEST_EXPECT(ctx, missing_source.initialise_fixed(1u));
    (void)system_context::set_ambient_module_id();
    const std::uint32_t missing_before = service->allocate_incident_id();
    TEST_EXPECT(ctx, !missing_source.post(system_message));
    TEST_EXPECT(ctx, service->allocate_incident_id() == missing_before + 2u);
    TEST_EXPECT(ctx, missing_source.refresh_readable_count() == 0u);
    (void)system_context::set_ambient_module_id(module_ids::executable);
    missing_source.deallocate();

    TEST_EXPECT(ctx, service->stop());
    TEST_EXPECT(ctx, debug_system::uninstall_service(service));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[invalid-system:"));
    TEST_EXPECT(ctx, tests::file_contains(
        event_path, "Erased transport rejected POD message"));

    (void)system_context::set_ambient_module_id(previous_module_id);
}

}   //  namespace

int run_erased_pod_tests()
{
    TTestContext ctx;
    test_erased_pod_redefinition_and_access(ctx);
    test_local_erased_pod(ctx);
    test_erased_pod_clears_complete_storage_extent(ctx);
    test_erased_pod_header_and_extended_alignment(ctx);
    test_thread_message_copy_boundary(ctx);
    test_local_thread_message_carrier(ctx);
    test_thread_message_clears_previous_representation(ctx);
    test_concrete_erased_pod_transport_admission(ctx);
    test_concrete_erased_pod_transport_diagnostics(ctx);

    std::cout << "ErasedPod: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
