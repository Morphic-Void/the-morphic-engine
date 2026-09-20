
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SystemTypeIdentity_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Aug 26

#include <iostream>
#include <type_traits>

#include "debug/event_arguments.hpp"
#include "tests/environment/local_type_ids.hpp"
#include "tests/environment/test_environment.hpp"
#include "module/module_binding.hpp"
#include "system/local_type_registry.hpp"
#include "system/erased_transport_admission.hpp"
#include "system/system_context.hpp"
#include "system/system_id_registry.hpp"
#include "system/transported_types.hpp"
#include "system/type_registration.hpp"
#include "tests/test_suites/SystemTypeIdentity_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace identity_tests
{
struct CUnregisteredType;
using TTestContext = tests::TTestContext;

void test_encoding(TTestContext& ctx)
{
    static_assert(system_type_ids::ops::k_encoded_payload_mask == 0x15555555u);
    static_assert(system_type_ids::ops::k_system_type_flag == 0x40000000u);
    static_assert(system_type_ids::ops::k_id_field_mask == 0x55555555u);
    static_assert(system_type_ids::ops::k_invalid_id_mask == 0xaaaaaaaau);
    static_assert(system_type_ids::ops::k_capacity == 0x7fffu);
    static_assert(system_type_ids::ops::k_max_ordinal == 0x7ffeu);
    static_assert(system_type_ids::ops::k_category_bit == 0x8000u);
    static_assert(system_type_ids::ops::k_system_index_min == 0x8000u);
    static_assert(system_type_ids::ops::k_system_index_max == 0xfffeu);
    static_assert(system_type_ids::ops::k_invalid_index == 0xffffu);
    static_assert(local_type_ids::ops::k_local_index_min == 0u);
    static_assert(local_type_ids::ops::k_local_index_max == 0x7ffeu);
    static_assert(local_type_ids::ops::k_capacity == system_type_ids::ops::k_capacity);
    static_assert(!std::is_same_v<system_type_id, local_type_id>);
    static_assert(!std::is_convertible_v<system_type_id, std::uint32_t>);
    static_assert(!std::is_convertible_v<local_type_id, std::uint32_t>);
    static_assert(!std::is_convertible_v<module_ids::id_type, std::uint64_t>);
    static_assert(system_type_id{}.raw_value() == 0u);
    static_assert(local_type_id{}.raw_value() == 0u);
    static_assert(mount_point_ids::id_type{}.raw_value() == 0u);
    static_assert(thread_ids::id_type{}.raw_value() == 0u);
    static_assert(module_ids::id_type{}.raw_value() == 0u);
    static_assert(system_ids::id_type{}.raw_value() == 0u);

    constexpr mount_point_ids::index_type last_mount_point_index =
        mount_point_ids::ops::make_index(mount_point_ids::ops::k_capacity - 1u);
    constexpr thread_ids::index_type last_thread_index =
        thread_ids::ops::make_index(thread_ids::ops::k_capacity - 1u);
    constexpr module_ids::index_type last_module_index =
        module_ids::ops::make_index(module_ids::ops::k_capacity - 1u);
    constexpr mount_point_ids::id_type last_mount_point =
        mount_point_ids::ops::make_id(last_mount_point_index);
    constexpr thread_ids::id_type last_thread = thread_ids::ops::make_id(last_thread_index);
    constexpr module_ids::id_type last_module =
        module_ids::ops::make_id(last_mount_point, last_module_index);
    static_assert(mount_point_ids::ops::is_valid_id(last_mount_point));
    static_assert(thread_ids::ops::is_valid_id(last_thread));
    static_assert(module_ids::ops::is_valid_id(last_module));
    static_assert(mount_point_ids::ops::decode_id(last_mount_point) == last_mount_point_index);
    static_assert(thread_ids::ops::decode_id(last_thread) == last_thread_index);
    static_assert(module_ids::ops::decode_id(last_module) == last_module_index);
    static_assert(mount_point_ids::ops::make_id(mount_point_ids::ops::make_index(0u)).raw_value() ==
        mount_point_ids::ops::field::k_id_field_mask);
    static_assert(thread_ids::ops::make_id(thread_ids::ops::make_index(0u)).raw_value() ==
        thread_ids::ops::field::k_id_field_mask);

    constexpr system_type_ids::index_type system_first_index = system_type_ids::ops::encode_index(0u);
    constexpr system_type_ids::index_type system_last_index = system_type_ids::ops::encode_index(system_type_ids::ops::k_max_ordinal);
    constexpr local_type_ids::index_type local_first_index = local_type_ids::ops::encode_index(0u);
    constexpr local_type_ids::index_type local_last_index = local_type_ids::ops::encode_index(local_type_ids::ops::k_max_ordinal);
    constexpr system_type_id system_first = system_type_ids::ops::encode_id(system_first_index);
    constexpr system_type_id system_last = system_type_ids::ops::encode_id(system_last_index);
    constexpr local_type_id local_first = local_type_ids::ops::encode_id(local_first_index);
    constexpr local_type_id local_last = local_type_ids::ops::encode_id(local_last_index);
    static_assert(system_first.raw_value() == 0x55555555u);
    static_assert(system_last.raw_value() == 0x40000001u);
    static_assert(local_first.raw_value() == 0x15555555u);
    static_assert(local_last.raw_value() == 0x00000001u);
    static_assert(system_type_ids::ops::decode_id(system_first) == 0x8000u);
    static_assert(system_type_ids::ops::decode_id(system_last) == 0xfffeu);
    static_assert(local_type_ids::ops::decode_id(local_first) == 0u);
    static_assert(local_type_ids::ops::decode_id(local_last) == 0x7ffeu);
    static_assert(system_type_ids::ops::is_valid_index(0xfffeu));
    static_assert(!system_type_ids::ops::is_valid_index(0xffffu));
    static_assert(!system_type_ids::ops::is_valid_index(0x7ffeu));
    static_assert(local_type_ids::ops::is_valid_index(0x7ffeu));
    static_assert(!local_type_ids::ops::is_valid_index(0x8000u));

    TEST_EXPECT(ctx, !system_type_ids::ops::is_valid_index(system_type_ids::ops::k_invalid_index));
    TEST_EXPECT(ctx, !local_type_ids::ops::is_valid_index(local_type_ids::ops::k_invalid_index));
    TEST_EXPECT(ctx, system_type_ids::ops::encode_id(system_type_ids::ops::k_invalid_index) == system_type_ids::undefined);
    TEST_EXPECT(ctx, local_type_ids::ops::encode_id(local_type_ids::ops::k_invalid_index) == local_type_ids::undefined);
    TEST_EXPECT(ctx, !system_type_ids::ops::is_valid_id(system_type_id{ local_first.raw_value() }));
    TEST_EXPECT(ctx, !local_type_ids::ops::is_valid_id(local_type_id{ system_first.raw_value() }));
    TEST_EXPECT(ctx, !system_type_ids::ops::is_valid_id(system_type_id{ system_type_ids::ops::k_system_type_flag }));
    TEST_EXPECT(ctx, !local_type_ids::ops::is_valid_id(local_type_id{}));
}

void test_category_bearing_identity(TTestContext& ctx)
{
    static_assert(sizeof(type_id) == 4u);
    static_assert(alignof(type_id) == 4u);
    static_assert(std::is_trivially_copyable_v<type_id>);
    static_assert(std::is_standard_layout_v<type_id>);
    static_assert(!std::is_same_v<type_id, system_type_id>);
    static_assert(!std::is_same_v<type_id, local_type_id>);
    static_assert(!std::is_same_v<system_type_id, local_type_id>);
    static_assert(std::is_constructible_v<type_id, system_type_id>);
    static_assert(std::is_constructible_v<type_id, local_type_id>);
    static_assert(!std::is_convertible_v<system_type_id, type_id>);
    static_assert(!std::is_convertible_v<local_type_id, type_id>);
    static_assert(!std::is_convertible_v<type_id, system_type_id>);
    static_assert(!std::is_convertible_v<type_id, local_type_id>);

    constexpr type_id undefined{};
    constexpr type_id system{ system_type_ids::byte_buffer };
    constexpr type_id local{ local_type_ids::test_runtime };
    constexpr type_id malformed_system{
        system_type_id{ local_type_ids::test_runtime.raw_value() } };
    constexpr type_id malformed_local{
        local_type_id{ system_type_ids::byte_buffer.raw_value() } };
    static_assert(undefined.category() == ETypeIdCategory::undefined);
    static_assert(system.category() == ETypeIdCategory::system);
    static_assert(local.category() == ETypeIdCategory::local);
    static_assert(!undefined.is_valid());
    static_assert(system.is_valid() && system.is_system() && !system.is_local());
    static_assert(local.is_valid() && local.is_local() && !local.is_system());
    static_assert(malformed_system == type_ids::undefined);
    static_assert(malformed_local == type_ids::undefined);

    system_type_id extracted_system{ 1u };
    local_type_id extracted_local{ 1u };
    TEST_EXPECT(ctx, system.try_system_type_id(extracted_system));
    TEST_EXPECT(ctx, extracted_system == system_type_ids::byte_buffer);
    TEST_EXPECT(ctx, !system.try_local_type_id(extracted_local));
    TEST_EXPECT(ctx, extracted_local == local_type_ids::undefined);
    TEST_EXPECT(ctx, local.try_local_type_id(extracted_local));
    TEST_EXPECT(ctx, extracted_local == local_type_ids::test_runtime);
    TEST_EXPECT(ctx, !local.try_system_type_id(extracted_system));
    TEST_EXPECT(ctx, extracted_system == system_type_ids::undefined);
}

void test_registration_categories(TTestContext& ctx)
{
    static_assert(k_type_id_binding_category_v<CByteBuffer> ==
        ETypeIdBindingCategory::system);
    static_assert(k_type_id_binding_category_v<test_environment::CTestRuntime> ==
        ETypeIdBindingCategory::local);
    static_assert(k_local_type_id_v<test_environment::CTestRuntime> == local_type_ids::test_runtime);
    static_assert(!k_can_register_type_id_v<CByteBuffer>);
    static_assert(!k_can_register_type_id_v<test_environment::CTestRuntime>);
    static_assert(k_can_register_type_id_v<CUnregisteredType>);
    static_assert(debug_system::is_supported_event_argument_v<local_type_id>);
    static_assert(debug_system::is_supported_event_argument_v<type_id>);
    static_assert(k_type_id_v<CByteBuffer> == type_id{ k_system_type_id_v<CByteBuffer> });
    static_assert(k_type_id_v<test_environment::CTestRuntime> == type_id{ k_local_type_id_v<test_environment::CTestRuntime> });
    static_assert(k_type_id_binding_category_v<test_environment::STestAssetLoadState> == ETypeIdBindingCategory::local);
    static_assert(k_type_id_binding_category_v<test_environment::STestAssetSaveState> == ETypeIdBindingCategory::local);
    static_assert(k_type_id_v<test_environment::STestAssetLoadState> == type_id{ local_type_ids::asset_load });
    static_assert(k_type_id_v<test_environment::STestAssetSaveState> == type_id{ local_type_ids::asset_save });
    TEST_EXPECT(ctx, system_type_ids::ops::is_valid_id(k_system_type_id_v<CByteBuffer>));
    TEST_EXPECT(ctx, local_type_ids::ops::is_valid_id(k_local_type_id_v<test_environment::CTestRuntime>));
}

void test_erased_transport_admission(TTestContext& ctx)
{
    const module_ids::id_type previous_module_id =
        system_context::set_ambient_module_id(module_ids::executable);

    const type_id registered_system{ system_type_ids::byte_buffer };
    const type_id registered_local{ local_type_ids::test_runtime };
    const type_id unregistered_system{
        system_type_ids::ops::encode_id(
            system_type_ids::ops::encode_index(system_type_ids::k_count)) };
    const type_id unregistered_local{
        local_type_ids::ops::encode_id(
            local_type_ids::ops::encode_index(local_type_ids::k_count)) };

    TEST_EXPECT(ctx, erased_transport_admission::classify(
        registered_system, module_ids::executable).rejection ==
        erased_transport_admission::ERejection::none);
    TEST_EXPECT(ctx, erased_transport_admission::classify(
        registered_system, module_ids::executive).rejection ==
        erased_transport_admission::ERejection::none);
    TEST_EXPECT(ctx, erased_transport_admission::classify(
        registered_local, module_ids::executable).rejection ==
        erased_transport_admission::ERejection::none);
    TEST_EXPECT(ctx, erased_transport_admission::classify(
        registered_local, module_ids::executive).rejection ==
        erased_transport_admission::ERejection::cross_component_local_identity);
    TEST_EXPECT(ctx, erased_transport_admission::classify(
        type_ids::undefined, module_ids::executable).rejection ==
        erased_transport_admission::ERejection::invalid_type_identity);
    TEST_EXPECT(ctx, erased_transport_admission::classify(
        unregistered_system, module_ids::executable).rejection ==
        erased_transport_admission::ERejection::unregistered_system_identity);
    TEST_EXPECT(ctx, erased_transport_admission::classify(
        unregistered_local, module_ids::executable).rejection ==
        erased_transport_admission::ERejection::unregistered_local_identity);
    TEST_EXPECT(ctx, erased_transport_admission::classify(
        registered_system, module_ids::id_type{}).rejection ==
        erased_transport_admission::ERejection::invalid_destination_module);

    TEST_EXPECT(ctx, erased_transport_admission::is_admissible(
        registered_system, module_ids::executable));
    TEST_EXPECT(ctx, erased_transport_admission::is_admissible(
        registered_system, module_ids::executive));
    TEST_EXPECT(ctx, erased_transport_admission::is_admissible(
        registered_local, module_ids::executable));
    TEST_EXPECT(ctx, !erased_transport_admission::is_admissible(
        registered_local, module_ids::executive));
    TEST_EXPECT(ctx, !erased_transport_admission::is_admissible(
        type_ids::undefined, module_ids::executable));
    TEST_EXPECT(ctx, !erased_transport_admission::is_admissible(
        unregistered_system, module_ids::executable));
    TEST_EXPECT(ctx, !erased_transport_admission::is_admissible(
        unregistered_local,
        module_ids::executable));
    TEST_EXPECT(ctx, !erased_transport_admission::is_admissible(
        registered_system, module_ids::id_type{}));

    (void)system_context::set_ambient_module_id();
    TEST_EXPECT(ctx, erased_transport_admission::classify(
        registered_system, module_ids::executable).rejection ==
        erased_transport_admission::ERejection::invalid_source_module);
    TEST_EXPECT(ctx, !erased_transport_admission::is_admissible(
        registered_system, module_ids::executable));
    (void)system_context::set_ambient_module_id(previous_module_id);
}

void test_advertised_identity_negotiation(TTestContext& ctx)
{
    constexpr modules::SAdvertisedIdentity host_identity{
        module_ids::executable, { 7u, 1u }, 2u, 4u };
    constexpr modules::SAdvertisedIdentity module_identity{
        module_ids::executive, { 9u, 3u }, 1u, 3u };
    constexpr modules::SAdvertisedIdentity incompatible_identity{
        module_ids::executive, { 9u, 3u }, 5u, 6u };
    constexpr modules::SAdvertisedIdentity invalid_identity{
        module_ids::executive, { 9u, 3u }, 4u, 3u };

    static_assert(modules::is_valid_advertised_identity(host_identity));
    static_assert(modules::is_valid_advertised_identity(module_identity));
    static_assert(!modules::is_valid_advertised_identity(invalid_identity));
    static_assert(modules::functional_ranges_overlap(host_identity, module_identity));
    static_assert(!modules::functional_ranges_overlap(host_identity, incompatible_identity));
    static_assert(modules::highest_common_functional_major(
        host_identity, module_identity) == 3u);

    TEST_EXPECT(ctx, modules::functional_ranges_overlap(module_identity, host_identity));
    TEST_EXPECT(ctx, modules::highest_common_functional_major(
        module_identity, host_identity) == 3u);
}

void test_local_names_and_lookup(TTestContext& ctx)
{
    static_assert(local_type_registry::is_valid_name_literal("123456789012345"));
    static_assert(!local_type_registry::is_valid_name_literal("1234567890123456"));
    constexpr local_type_registry::SLocalTypeName name = local_type_registry::make_name("short");
    static_assert(name.bytes[0] == 's' && name.bytes[5] == 0 && name.bytes[15] == 0);
    static_assert(name.bytes[6] == 0 && name.bytes[7] == 0 && name.bytes[8] == 0 &&
        name.bytes[9] == 0 && name.bytes[10] == 0 && name.bytes[11] == 0 &&
        name.bytes[12] == 0 && name.bytes[13] == 0 && name.bytes[14] == 0);

    TEST_EXPECT(ctx, local_type_registry::view_is_installed());
    const local_type_registry::SLocalTypeRegistration* const registration =
        local_type_registry::find_type(local_type_ids::test_runtime);
    TEST_EXPECT(ctx, registration != nullptr);
    if (registration != nullptr)
        TEST_EXPECT(ctx, std::strcmp(registration->short_name.bytes, "test_runtime") == 0);
    const local_type_registry::SLocalTypeRegistration* const state_registration =
        local_type_registry::find_type(local_type_ids::asset_load);
    TEST_EXPECT(ctx, state_registration != nullptr);
    if (state_registration != nullptr)
        TEST_EXPECT(ctx, std::strcmp(state_registration->short_name.bytes, "asset_load") == 0);
    TEST_EXPECT(ctx, local_type_registry::find_type(
        static_cast<const local_type_registry::SLocalTypeRegistryView*>(nullptr),
        local_type_ids::test_runtime) == nullptr);
    const local_type_registry::SLocalTypeRegistryView unavailable_view{ nullptr, 1u };
    TEST_EXPECT(ctx, local_type_registry::find_type(
        &unavailable_view, local_type_ids::test_runtime) == nullptr);
    TEST_EXPECT(ctx, local_type_registry::find_type(
        local_type_ids::ops::encode_id(local_type_ids::k_count)) == nullptr);
    TEST_EXPECT(ctx, !local_type_registry::install_view(local_type_registry::component_view()));

    local_type_registry::SLocalTypeRegistration corrupt{
        local_type_ids::ops::encode_id(0u), 0u, local_type_registry::make_name("valid") };
    corrupt.short_name.bytes[15] = 'x';
    const local_type_registry::SLocalTypeRegistryView corrupt_view{ &corrupt, 1u };
    TEST_EXPECT(ctx, !local_type_registry::validate_view(corrupt_view));
    TEST_EXPECT(ctx, local_type_registry::find_type(&corrupt_view, corrupt.id) == nullptr);
}

void test_system_authority(TTestContext& ctx)
{
    TEST_EXPECT(ctx, system_id_registry::view_is_installed());
    TEST_EXPECT(ctx, system_id_registry::validate_all());
    const system_id_registry::SSystemRegistryView* const installed_view =
        system_id_registry::installed_view();
    TEST_EXPECT(ctx, installed_view != nullptr);
    TEST_EXPECT(ctx, !system_id_registry::install_view(test_environment::system_registry_view()));
    TEST_EXPECT(ctx, system_id_registry::find_type(
        static_cast<const system_id_registry::SSystemRegistryView*>(nullptr),
        system_type_ids::byte_buffer) == nullptr);
    const system_id_registry::SSystemRegistryView unavailable_view{ nullptr, 1u };
    TEST_EXPECT(ctx, system_id_registry::find_type(
        &unavailable_view, system_type_ids::ops::encode_id(system_type_ids::ops::encode_index(0u))) == nullptr);

    constexpr char long_name[] = "a-system-name-is-not-limited-to-fifteen-bytes";
    const system_id_registry::STypeRegistration registration{
        system_type_ids::ops::encode_id(system_type_ids::ops::encode_index(0u)), system_type_ids::ops::encode_index(0u),
        long_name, sizeof(long_name) - 1u };
    const system_id_registry::SSystemRegistryView view{ &registration, 1u };
    TEST_EXPECT(ctx, system_id_registry::validate_view(view));
    TEST_EXPECT(ctx, system_id_registry::find_type(&view, registration.id) == &registration);

    system_id_registry::STypeRegistration corrupt = registration;
    corrupt.id = system_type_ids::ops::encode_id(system_type_ids::ops::encode_index(1u));
    const system_id_registry::SSystemRegistryView corrupt_view{ &corrupt, 1u };
    TEST_EXPECT(ctx, !system_id_registry::validate_view(corrupt_view));
    TEST_EXPECT(ctx, system_id_registry::find_type(&corrupt_view, corrupt.id) == nullptr);

    system_id_registry::SSystemRegistryView oversized_view = *installed_view;
    oversized_view.mount_point_count = static_cast<std::uint32_t>(mount_point_ids::ops::k_capacity + 1u);
    TEST_EXPECT(ctx, !system_id_registry::validate_view(oversized_view));
    oversized_view = *installed_view;
    oversized_view.thread_count = static_cast<std::uint32_t>(thread_ids::ops::k_capacity + 1u);
    TEST_EXPECT(ctx, !system_id_registry::validate_view(oversized_view));
    oversized_view = *installed_view;
    oversized_view.module_count = static_cast<std::uint32_t>(module_ids::ops::k_capacity + 1u);
    TEST_EXPECT(ctx, !system_id_registry::validate_view(oversized_view));
}
}   //  namespace identity_tests

int run_system_type_identity_tests()
{
    identity_tests::TTestContext ctx;
    identity_tests::test_encoding(ctx);
    identity_tests::test_category_bearing_identity(ctx);
    identity_tests::test_registration_categories(ctx);
    identity_tests::test_erased_transport_admission(ctx);
    identity_tests::test_advertised_identity_negotiation(ctx);
    identity_tests::test_local_names_and_lookup(ctx);
    identity_tests::test_system_authority(ctx);
    std::cout << "SystemTypeIdentity: " << ctx.passed << " passed, "
        << ctx.failed << " failed\n";
    return ctx.failed;
}
