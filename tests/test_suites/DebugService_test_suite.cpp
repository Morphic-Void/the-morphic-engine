
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    DebugService_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    28 Jul 26

#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>

#include "containers/TInstance.hpp"
#include "debug/log_path.hpp"
#include "debug/macros.hpp"
#include "debug/service.hpp"
#include "tests/environment/local_type_ids.hpp"
#include "tests/environment/test_paths.hpp"
#include "system/system_context.hpp"
#include "system/system_id_registry.hpp"
#include "tests/test_suites/DebugService_test_suite.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/file_helpers.hpp"

namespace debug_system
{

struct SDebugServiceTestAccess
{
    [[nodiscard]] static bool write_event(
        CDebugServiceState& service, const SEvent& event) noexcept
    {
        return service.write_event(event);
    }
};

}   //  namespace debug_system

namespace debug_service_tests
{

using TTestContext = tests::TTestContext;

void compile_public_macro_interface(const bool execute)
{
    if (!execute)
    {
        return;
    }

    const bool condition = true;
    MV_ASSERT(condition);
    MV_ASSERT_MSG(condition, "assert message {}", std::uint32_t{ 1u });
    MV_DEBUG_ASSERT(condition);
    MV_DEBUG_ASSERT_MSG(condition, "debug assert message");
    MV_DEBUG_ONLY((void)condition);
    MV_CRITICAL_ASSERT(condition);
    MV_CRITICAL_ASSERT_MSG(condition, "critical assert message");
    MV_FATAL_ASSERT(condition);
    MV_FATAL_ASSERT_MSG(condition, "fatal assert message");
    MV_INFO("info {}", std::uint32_t{ 2u });
    MV_DETAIL("detail");
    MV_TRACE("trace");
    MV_REPORT("report %u", 3u);
    MV_REPORT_IMMEDIATE("immediate report %u", 4u);
    MV_WARNING("warning");
    MV_ERROR("error");
    MV_CRITICAL_EVENT("critical event");
    MV_FATAL_EVENT("fatal event");
    constexpr char maximum_transport_format[] =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "567890123456789012345678901234567890123456789012345678901234567";
    static_assert(sizeof(maximum_transport_format) ==
        debug_system::k_event_format_capacity);
    MV_INFO(maximum_transport_format);
    MV_PANIC("panic");
}

debug_system::EEventArgumentType argument_type(
    const debug_system::SEventArguments& arguments,
    const std::size_t index)
{
    return arguments.parameter_types[index];
}

debug_system::EEventFormatResult format_event(
    char* const destination,
    const std::size_t destination_capacity,
    const char* const format,
    const debug_system::SEventArguments& arguments,
    std::size_t& out_size)
{
    return debug_system::format_event_text(
        destination,
        destination_capacity,
        format,
        std::strlen(format),
        arguments,
        out_size);
}

void test_exclusive_lock_try_acquire(TTestContext& ctx)
{
    platform::threading::CExclusiveLock lock;
    TEST_EXPECT(ctx, lock.is_valid());
    TEST_EXPECT(ctx, lock.try_acquire());
    lock.release();
    TEST_EXPECT(ctx, lock.try_acquire());
    lock.release();
}

void test_process_log_paths(TTestContext& ctx)
{
    TEST_EXPECT(ctx, !debug_system::is_valid_log_tag(nullptr));
    TEST_EXPECT(ctx, !debug_system::is_valid_log_tag(""));
    TEST_EXPECT(ctx, !debug_system::is_valid_log_tag("-leading"));
    TEST_EXPECT(ctx, !debug_system::is_valid_log_tag("bad/tag"));
    TEST_EXPECT(ctx, !debug_system::is_valid_log_tag("bad tag"));
    TEST_EXPECT(ctx, debug_system::is_valid_log_tag("a"));
    TEST_EXPECT(ctx, debug_system::is_valid_log_tag("parallel.same_1-a"));

    char maximum_tag[debug_system::k_log_tag_max_length + 1u]{};
    for (std::size_t index = 0u; index < debug_system::k_log_tag_max_length; ++index)
    {
        maximum_tag[index] = 'a';
    }
    TEST_EXPECT(ctx, debug_system::is_valid_log_tag(maximum_tag));

    char oversized_tag[debug_system::k_log_tag_max_length + 2u]{};
    for (std::size_t index = 0u; index <= debug_system::k_log_tag_max_length; ++index)
    {
        oversized_tag[index] = 'a';
    }
    TEST_EXPECT(ctx, !debug_system::is_valid_log_tag(oversized_tag));

    char path[128]{};
    TEST_EXPECT(ctx, debug_system::format_process_log_path(
        path, sizeof(path), "logs/example", nullptr, 42u));
    TEST_EXPECT(ctx, std::strcmp(path, "logs/example.p42.log") == 0);
    TEST_EXPECT(ctx, debug_system::format_process_log_path(
        path, sizeof(path), "logs/example", "parallel-a", 42u));
    TEST_EXPECT(ctx,
        std::strcmp(path, "logs/example.parallel-a.p42.log") == 0);
    TEST_EXPECT(ctx, debug_system::format_process_log_path(
        path, sizeof(path), "example", "parallel-a", 42u, "development/logical-roots/test-logs"));
    TEST_EXPECT(ctx,
        std::strcmp(path, "development/logical-roots/test-logs/example.parallel-a.p42.log") == 0);
    TEST_EXPECT(ctx, !debug_system::format_process_log_path(
        path, 32u, "example", nullptr, 42u, "development/logical-roots/test-logs"));
    TEST_EXPECT(ctx, path[0] == '\0');
    TEST_EXPECT(ctx, !debug_system::format_process_log_path(
        path, sizeof(path), "logs/example", "bad/tag", 42u));
    TEST_EXPECT(ctx, path[0] == '\0');
    TEST_EXPECT(ctx, !debug_system::format_process_log_path(
        path, sizeof(path), "logs/example", nullptr, 0u));
    TEST_EXPECT(ctx, path[0] == '\0');

    char short_path[8]{};
    TEST_EXPECT(ctx, !debug_system::format_process_log_path(
        short_path, sizeof(short_path), "logs/example", nullptr, 42u));
    TEST_EXPECT(ctx, short_path[0] == '\0');
}

void test_argument_encoding(TTestContext& ctx)
{
    static_assert(
        sizeof(debug_system::SEvent) ==
        debug_system::k_event_transport_element_size);
    static_assert(alignof(debug_system::SEvent) == 64u);
    static_assert(offsetof(debug_system::SEvent, representation) == 16u);
    static_assert(offsetof(debug_system::SEvent, filename_size) == 19u);
    static_assert(offsetof(debug_system::SEvent, metadata) == 20u);
    static_assert(offsetof(debug_system::SEvent, filename) == 32u);
    static_assert(offsetof(debug_system::SEvent, storage) == 64u);
    static_assert(offsetof(debug_system::SReportEventMetadata, report_length) == 0u);
    static_assert(offsetof(debug_system::SReportEventMetadata, reserved) == 2u);
    static_assert(offsetof(debug_system::SStructuredEventMetadata, content_type) == 2u);
    static_assert(offsetof(debug_system::SStructuredEventMetadata, parameter_count) == 3u);
    static_assert(offsetof(debug_system::SStructuredEventMetadata, parameter_types) == 4u);
    static_assert(offsetof(debug_system::SStructuredEventStorage, expression) == 0u);
    static_assert(offsetof(debug_system::SStructuredEventStorage, format) == 192u);
    static_assert(offsetof(debug_system::SStructuredEventStorage, parameters) == 320u);
    static_assert(debug_system::is_valid_event_metadata(
        debug_system::EEventLevel::info,
        debug_system::EEventType::event));
    static_assert(debug_system::is_valid_event_metadata(
        debug_system::EEventLevel::assert,
        debug_system::EEventType::condition));
    static_assert(debug_system::is_valid_event_metadata(
        debug_system::EEventLevel::critical,
        debug_system::EEventType::condition));
    static_assert(debug_system::is_valid_event_metadata(
        debug_system::EEventLevel::critical,
        debug_system::EEventType::event));
    static_assert(!debug_system::is_valid_event_metadata(
        debug_system::EEventLevel::info,
        debug_system::EEventType::condition));
    static_assert(!debug_system::is_valid_event_metadata(
        debug_system::EEventLevel::warning,
        debug_system::EEventType::condition));
    static_assert(
        std::is_trivially_copyable_v<debug_system::SEventArguments>);
    static_assert(
        std::is_trivially_copyable_v<debug_system::SEventParameterValue>);
    static_assert(
        std::is_trivially_copyable_v<debug_system::CInlineText16>);
    static_assert(debug_system::is_supported_event_argument_v<bool>);
    static_assert(debug_system::is_supported_event_argument_v<std::int32_t>);
    static_assert(debug_system::is_supported_event_argument_v<std::uint32_t>);
    static_assert(debug_system::is_supported_event_argument_v<std::int64_t>);
    static_assert(debug_system::is_supported_event_argument_v<std::uint64_t>);
    static_assert(debug_system::is_supported_event_argument_v<float>);
    static_assert(debug_system::is_supported_event_argument_v<double>);
    static_assert(
        debug_system::is_supported_event_argument_v<
            debug_system::CInlineText16>);
    static_assert(debug_system::is_supported_event_argument_v<system_type_id>);
    static_assert(debug_system::is_supported_event_argument_v<local_type_id>);
    static_assert(debug_system::is_supported_event_argument_v<type_id>);
    static_assert(
        debug_system::is_supported_event_argument_v<system_ids::id_type>);
    static_assert(
        debug_system::is_supported_event_argument_v<module_ids::id_type>);
    static_assert(
        debug_system::is_supported_event_argument_v<thread_ids::id_type>);
    static_assert(!debug_system::is_supported_event_argument_v<const char*>);

    const debug_system::SEventArguments empty =
        debug_system::encode_event_arguments();
    TEST_EXPECT(ctx, empty.parameter_count == 0u);
    TEST_EXPECT(ctx,
        argument_type(empty, 0u) ==
        debug_system::EEventArgumentType::unused);

    const debug_system::SEventArguments encoded =
        debug_system::encode_event_arguments(
            false,
            true,
            std::int32_t{ -12 },
            std::uint32_t{ 34u },
            std::int64_t{ -56 },
            std::uint64_t{ 78u },
            1.25f,
            2.5);

    TEST_EXPECT(ctx, encoded.parameter_count == 8u);
    TEST_EXPECT(ctx,
        argument_type(encoded, 0u) ==
        debug_system::EEventArgumentType::false_value);
    TEST_EXPECT(ctx,
        argument_type(encoded, 1u) ==
        debug_system::EEventArgumentType::true_value);
    TEST_EXPECT(ctx,
        argument_type(encoded, 2u) ==
        debug_system::EEventArgumentType::int32);
    TEST_EXPECT(ctx,
        argument_type(encoded, 3u) ==
        debug_system::EEventArgumentType::uint32);
    TEST_EXPECT(ctx,
        argument_type(encoded, 4u) ==
        debug_system::EEventArgumentType::int64);
    TEST_EXPECT(ctx,
        argument_type(encoded, 5u) ==
        debug_system::EEventArgumentType::uint64);
    TEST_EXPECT(ctx,
        argument_type(encoded, 6u) ==
        debug_system::EEventArgumentType::float32);
    TEST_EXPECT(ctx,
        argument_type(encoded, 7u) ==
        debug_system::EEventArgumentType::float64);
    TEST_EXPECT(ctx, encoded.parameters[0u].bytes[0u] == std::byte{});
    TEST_EXPECT(ctx, encoded.parameters[1u].bytes[0u] == std::byte{});
    TEST_EXPECT(ctx, encoded.parameters[2u].bytes[4u] == std::byte{});
    TEST_EXPECT(ctx, encoded.parameters[7u].bytes[8u] == std::byte{});

    const debug_system::SEventArguments full_payload =
        debug_system::encode_event_arguments(
            std::uint64_t{ 0u },
            std::uint64_t{ 1u },
            std::uint64_t{ 2u },
            std::uint64_t{ 3u },
            std::uint64_t{ 4u },
            std::uint64_t{ 5u },
            std::uint64_t{ 6u },
            std::uint64_t{ 7u });
    TEST_EXPECT(ctx, full_payload.parameter_count == 8u);
    TEST_EXPECT(ctx,
        argument_type(full_payload, 7u) ==
        debug_system::EEventArgumentType::uint64);

    const debug_system::SEventArguments inline_text =
        debug_system::encode_event_arguments(
            debug_system::CInlineText16{ "fifteen-chars!!" });
    TEST_EXPECT(ctx, inline_text.parameter_count == 1u);
    TEST_EXPECT(ctx,
        argument_type(inline_text, 0u) ==
        debug_system::EEventArgumentType::inline_text);

    const debug_system::SEventArguments system_id_arguments =
        debug_system::encode_event_arguments(system_type_ids::file_load_request);
    TEST_EXPECT(ctx, system_id_arguments.parameter_count == 1u);
    TEST_EXPECT(ctx,
        argument_type(system_id_arguments, 0u) ==
        debug_system::EEventArgumentType::system_type_id);

    const local_type_id registered_local = local_type_ids::test_runtime;
    const local_type_id unresolved_local = local_type_ids::ops::encode_id(
        local_type_ids::ops::encode_index(local_type_ids::k_count));
    const local_type_id invalid_local{ 0x00000002u };
    type_id malformed_generic;
    const std::uint32_t malformed_raw = 0x00000002u;
    std::memcpy(&malformed_generic, &malformed_raw, sizeof(malformed_generic));
    const debug_system::SEventArguments normalized_ids =
        debug_system::encode_event_arguments(
            registered_local,
            unresolved_local,
            invalid_local,
            type_id{ system_type_ids::file_load_request },
            type_id{ registered_local },
            malformed_generic);
    TEST_EXPECT(ctx, normalized_ids.parameter_count == 6u);
    TEST_EXPECT(ctx, argument_type(normalized_ids, 0u) ==
        debug_system::EEventArgumentType::local_type_name);
    TEST_EXPECT(ctx, argument_type(normalized_ids, 1u) ==
        debug_system::EEventArgumentType::local_type_id_failure);
    TEST_EXPECT(ctx, argument_type(normalized_ids, 2u) ==
        debug_system::EEventArgumentType::local_type_id_failure);
    TEST_EXPECT(ctx, argument_type(normalized_ids, 3u) ==
        debug_system::EEventArgumentType::system_type_id);
    TEST_EXPECT(ctx, argument_type(normalized_ids, 4u) ==
        debug_system::EEventArgumentType::local_type_name);
    TEST_EXPECT(ctx, argument_type(normalized_ids, 5u) ==
        debug_system::EEventArgumentType::local_type_id_failure);
    TEST_EXPECT(ctx, std::memcmp(
        normalized_ids.parameters[0u].bytes,
        local_type_registry::lookup_name(registered_local)->bytes,
        debug_system::k_event_argument_slot_size) == 0);
    TEST_EXPECT(ctx, normalized_ids.parameters[1u].bytes[4u] == std::byte{});
    TEST_EXPECT(ctx, normalized_ids.parameters[2u].bytes[4u] == std::byte{});

    const debug_system::SEventArguments runtime_ids =
        debug_system::encode_event_arguments(
            system_ids::host,
            module_ids::executable,
            thread_ids::host);
    TEST_EXPECT(ctx, runtime_ids.parameter_count == 3u);
    TEST_EXPECT(ctx, argument_type(runtime_ids, 0u) ==
        debug_system::EEventArgumentType::system_id);
    TEST_EXPECT(ctx, argument_type(runtime_ids, 1u) ==
        debug_system::EEventArgumentType::module_id);
    TEST_EXPECT(ctx, argument_type(runtime_ids, 2u) ==
        debug_system::EEventArgumentType::thread_id);
    TEST_EXPECT(ctx, runtime_ids.parameters[0u].bytes[8u] == std::byte{});
    TEST_EXPECT(ctx, runtime_ids.parameters[1u].bytes[8u] == std::byte{});
    TEST_EXPECT(ctx, runtime_ids.parameters[2u].bytes[8u] == std::byte{});
}

void test_argument_formatting(TTestContext& ctx)
{
    char output[512]{};
    std::size_t output_size = 0u;

    const debug_system::SEventArguments values =
        debug_system::encode_event_arguments(
            false,
            true,
            std::int32_t{ -12 },
            std::uint32_t{ 34u },
            std::int64_t{ -56 },
            std::uint64_t{ 78u },
            1.25f,
            2.5);
    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "{} {} {} {} {} {} {} {}",
            values,
            output_size) ==
        debug_system::EEventFormatResult::success);
    TEST_EXPECT(ctx,
        std::strcmp(
            output,
            "false true -12 34 -56 78 1.25 2.5") == 0);
    TEST_EXPECT(ctx, output_size == std::strlen(output));

    const debug_system::SEventArguments inline_text =
        debug_system::encode_event_arguments(
            debug_system::CInlineText16{ "small text" });
    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "{{inline: {}}}",
            inline_text,
            output_size) ==
        debug_system::EEventFormatResult::success);
    TEST_EXPECT(ctx, std::strcmp(output, "{inline: small text}") == 0);

    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "#{}, x{}, X{}",
            debug_system::encode_event_arguments(
                std::uint32_t{ 0xabcd1234u },
                std::int32_t{ -1 },
                std::uint64_t{ 0x1234abcdefULL }),
            output_size) ==
        debug_system::EEventFormatResult::success);
    TEST_EXPECT(ctx,
        std::strcmp(
            output,
            "#abcd1234, xffffffff, x1234ABCDEF") == 0);

    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "0x{} 0X{}",
            debug_system::encode_event_arguments(
                std::uint32_t{ 0x2au },
                std::int64_t{ -1 }),
            output_size) ==
        debug_system::EEventFormatResult::success);
    TEST_EXPECT(ctx,
        std::strcmp(
            output,
            "0x2a 0xFFFFFFFFFFFFFFFF") == 0);

    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "#{{ x{{ X{{ {{ }}",
            debug_system::encode_event_arguments(),
            output_size) ==
        debug_system::EEventFormatResult::success);
    TEST_EXPECT(ctx, std::strcmp(output, "#{ x{ X{ { }") == 0);

    char expected_type_output[160]{};
    const system_type_id unregistered_type_id =
        system_type_ids::ops::encode_id(system_type_ids::ops::encode_index(system_type_ids::k_count));
    const int expected_type_output_size = std::snprintf(
        expected_type_output,
        sizeof(expected_type_output),
        "file_load_request | unregistered-type:0x%08x | invalid-type:0x%08x",
        static_cast<unsigned int>(unregistered_type_id.raw_value()),
        0x00000002u);
    TEST_EXPECT(ctx, expected_type_output_size > 0);
    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "{} | {} | {}",
            debug_system::encode_event_arguments(
                system_type_ids::file_load_request,
                unregistered_type_id,
                system_type_id{ 0x00000002u }),
            output_size) ==
        debug_system::EEventFormatResult::success);
    TEST_EXPECT(ctx, std::strcmp(output, expected_type_output) == 0);

    const local_type_id unresolved_local = local_type_ids::ops::encode_id(
        local_type_ids::ops::encode_index(local_type_ids::k_count));
    char expected_local_output[192]{};
    const int expected_local_output_size = std::snprintf(
        expected_local_output,
        sizeof(expected_local_output),
        "test_runtime | unregistered-local-type:0x%08x | invalid-local-type:0x%08x",
        static_cast<unsigned int>(unresolved_local.raw_value()),
        0x00000002u);
    TEST_EXPECT(ctx, expected_local_output_size > 0);
    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "{} | {} | {}",
            debug_system::encode_event_arguments(
                local_type_ids::test_runtime,
                unresolved_local,
                local_type_id{ 0x00000002u }),
            output_size) ==
        debug_system::EEventFormatResult::success);
    TEST_EXPECT(ctx, std::strcmp(output, expected_local_output) == 0);

    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "{} | {} | {}",
            debug_system::encode_event_arguments(
                system_ids::host,
                module_ids::executable,
                thread_ids::host),
            output_size) ==
        debug_system::EEventFormatResult::success);
    TEST_EXPECT(ctx, std::strcmp(output, "executable:host | executable | host") == 0);

    const module_ids::id_type unregistered_module = module_ids::ops::make_id(
        mount_point_ids::render,
        module_ids::executive_index);
    const thread_ids::id_type unregistered_thread = thread_ids::ops::make_id(
        thread_ids::ops::make_index(thread_ids::k_count));
    const system_ids::id_type unregistered_system =
        system_ids::ops::make_system_id(unregistered_module, thread_ids::host);
    char expected_runtime_output[512]{};
    const int expected_runtime_output_size = std::snprintf(
        expected_runtime_output,
        sizeof(expected_runtime_output),
        "unregistered-system:0x%016llx | invalid-system:0x%016llx | "
        "unregistered-module:0x%016llx | invalid-module:0x%016llx | "
        "unregistered-thread:0x%016llx | invalid-thread:0x%016llx",
        static_cast<unsigned long long>(unregistered_system.raw_value()),
        2ULL,
        static_cast<unsigned long long>(unregistered_module.raw_value()),
        2ULL,
        static_cast<unsigned long long>(unregistered_thread.raw_value()),
        2ULL);
    TEST_EXPECT(ctx, expected_runtime_output_size > 0);
    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "{} | {} | {} | {} | {} | {}",
            debug_system::encode_event_arguments(
                unregistered_system,
                system_ids::id_type{ 2u },
                unregistered_module,
                module_ids::id_type{ 2u },
                unregistered_thread,
                thread_ids::id_type{ 2u }),
            output_size) ==
        debug_system::EEventFormatResult::success);
    TEST_EXPECT(ctx, std::strcmp(output, expected_runtime_output) == 0);

    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "{} {}",
            debug_system::encode_event_arguments(std::uint32_t{ 1u }),
            output_size) ==
        debug_system::EEventFormatResult::argument_mismatch);
    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "#{}",
            debug_system::encode_event_arguments(
                debug_system::CInlineText16{ "small text" }),
            output_size) ==
        debug_system::EEventFormatResult::unsupported_argument);
    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "#{}",
            debug_system::encode_event_arguments(system_ids::host),
            output_size) ==
        debug_system::EEventFormatResult::unsupported_argument);
    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "{broken",
            debug_system::encode_event_arguments(),
            output_size) ==
        debug_system::EEventFormatResult::malformed_format);

    constexpr char embedded_null[]{ 'a', 0, 'b', 0 };
    TEST_EXPECT(ctx,
        debug_system::format_event_text(
            output,
            sizeof(output),
            embedded_null,
            3u,
            debug_system::encode_event_arguments(),
            output_size) ==
        debug_system::EEventFormatResult::malformed_format);

    TEST_EXPECT(ctx,
        format_event(
            output,
            4u,
            "value {}",
            debug_system::encode_event_arguments(std::uint32_t{ 1u }),
            output_size) ==
        debug_system::EEventFormatResult::output_too_small);

    output_size = sizeof(output);
    TEST_EXPECT(ctx,
        format_event(
            output,
            4u,
            "tail",
            debug_system::encode_event_arguments(),
            output_size) ==
        debug_system::EEventFormatResult::output_too_small);
    TEST_EXPECT(ctx, output_size == 0u);

    debug_system::SEventArguments malformed =
        debug_system::encode_event_arguments(std::uint32_t{ 1u });
    malformed.parameters[0u].bytes[4u] = std::byte{ 1u };
    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "{}",
            malformed,
            output_size) ==
        debug_system::EEventFormatResult::invalid_descriptor);

    debug_system::SEventArguments malformed_system_id =
        debug_system::encode_event_arguments(system_ids::host);
    malformed_system_id.parameters[0u].bytes[8u] = std::byte{ 1u };
    TEST_EXPECT(ctx,
        format_event(output, sizeof(output), "{}", malformed_system_id, output_size) ==
        debug_system::EEventFormatResult::invalid_descriptor);

    debug_system::SEventArguments malformed_module_id =
        debug_system::encode_event_arguments(module_ids::executable);
    malformed_module_id.parameters[0u].bytes[8u] = std::byte{ 1u };
    TEST_EXPECT(ctx,
        format_event(output, sizeof(output), "{}", malformed_module_id, output_size) ==
        debug_system::EEventFormatResult::invalid_descriptor);

    debug_system::SEventArguments malformed_thread_id =
        debug_system::encode_event_arguments(thread_ids::host);
    malformed_thread_id.parameters[0u].bytes[8u] = std::byte{ 1u };
    TEST_EXPECT(ctx,
        format_event(output, sizeof(output), "{}", malformed_thread_id, output_size) ==
        debug_system::EEventFormatResult::invalid_descriptor);

    debug_system::SEventArguments malformed_local_name{};
    malformed_local_name.parameter_types[0u] =
        debug_system::EEventArgumentType::local_type_name;
    malformed_local_name.parameter_count = 1u;
    std::memset(malformed_local_name.parameters[0u].bytes, 'n',
        debug_system::k_event_argument_slot_size);
    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "{}",
            malformed_local_name,
            output_size) ==
        debug_system::EEventFormatResult::invalid_descriptor);

    debug_system::SEventArguments malformed_local_failure{};
    malformed_local_failure.parameter_types[0u] =
        debug_system::EEventArgumentType::local_type_id_failure;
    malformed_local_failure.parameter_count = 1u;
    malformed_local_failure.parameters[0u].bytes[4u] = std::byte{ 1u };
    TEST_EXPECT(ctx,
        format_event(
            output,
            sizeof(output),
            "{}",
            malformed_local_failure,
            output_size) ==
        debug_system::EEventFormatResult::invalid_descriptor);

    debug_system::SEventArguments corrupt_unused{};
    corrupt_unused.parameter_types[7u] = debug_system::EEventArgumentType::uint32;
    TEST_EXPECT(ctx,
        format_event(output, sizeof(output), "text", corrupt_unused, output_size) ==
        debug_system::EEventFormatResult::invalid_descriptor);
    corrupt_unused = {};
    corrupt_unused.parameters[7u].bytes[0u] = std::byte{ 1u };
    TEST_EXPECT(ctx,
        format_event(output, sizeof(output), "text", corrupt_unused, output_size) ==
        debug_system::EEventFormatResult::invalid_descriptor);

    debug_system::SEventArguments invalid_count{};
    invalid_count.parameter_count = 9u;
    TEST_EXPECT(ctx,
        format_event(output, sizeof(output), "text", invalid_count, output_size) ==
        debug_system::EEventFormatResult::invalid_descriptor);

    debug_system::SEventArguments unknown_type{};
    unknown_type.parameter_count = 1u;
    unknown_type.parameter_types[0u] =
        static_cast<debug_system::EEventArgumentType>(0xffu);
    TEST_EXPECT(ctx,
        format_event(output, sizeof(output), "{}", unknown_type, output_size) ==
        debug_system::EEventFormatResult::invalid_descriptor);

    debug_system::SEventArguments unterminated_text{};
    unterminated_text.parameter_count = 1u;
    unterminated_text.parameter_types[0u] =
        debug_system::EEventArgumentType::inline_text;
    std::memset(unterminated_text.parameters[0u].bytes, 'q',
        debug_system::k_event_argument_slot_size);
    TEST_EXPECT(ctx,
        format_event(output, sizeof(output), "{}", unterminated_text, output_size) ==
        debug_system::EEventFormatResult::invalid_descriptor);

    char boundary_format[64]{};
    std::memset(boundary_format, 'a', sizeof(boundary_format) - 1u);
    TEST_EXPECT(ctx,
        debug_system::format_event_text(
            output, sizeof(output), boundary_format,
            sizeof(boundary_format) - 1u,
            debug_system::encode_event_arguments(), output_size) ==
        debug_system::EEventFormatResult::success);
    TEST_EXPECT(ctx, output_size == 63u);
}

void test_system_id_name_registry(TTestContext& ctx)
{
    static_assert(system_type_ids::undefined.raw_value() == 0u);
    static_assert(!system_type_ids::ops::is_defined(system_type_ids::undefined));
    static_assert(!system_type_ids::ops::is_valid_id(system_type_ids::undefined));
    static_assert(system_type_ids::ops::is_defined(system_type_ids::byte_buffer));
    static_assert(system_type_ids::ops::is_valid_id(system_type_ids::byte_buffer));

    const system_id_registry::SSystemRegistryView* const registry =
        system_id_registry::installed_view();
    TEST_EXPECT(ctx, registry != nullptr);
    TEST_EXPECT(ctx, registry->type_count == system_type_ids::k_count);
    TEST_EXPECT(ctx, registry->mount_point_count == mount_point_ids::k_count);
    TEST_EXPECT(ctx, registry->thread_count == thread_ids::k_count);
    TEST_EXPECT(ctx, registry->module_count == module_ids::k_count);
    TEST_EXPECT(ctx, system_id_registry::validate_all());

    const system_id_registry::STypeRegistration* const type_registration =
        system_id_registry::find_type(system_type_ids::file_load_request);
    TEST_EXPECT(ctx, type_registration != nullptr);
    if (type_registration != nullptr)
    {
        TEST_EXPECT(ctx,
            type_registration->index == system_type_ids::file_load_request_index);
    }
    const char* const type_name =
        system_id_registry::lookup_type_name(system_type_ids::file_load_request);
    TEST_EXPECT(ctx, type_name != nullptr);
    if (type_name != nullptr)
    {
        TEST_EXPECT(ctx, std::strcmp(type_name, "file_load_request") == 0);
    }

    const system_id_registry::SMountPointRegistration* const mount_registration =
        system_id_registry::find_mount_point(mount_point_ids::render);
    TEST_EXPECT(ctx, mount_registration != nullptr);
    if (mount_registration != nullptr)
    {
        TEST_EXPECT(ctx,
            mount_registration->index == mount_point_ids::render_index);
    }
    const char* const mount_name =
        system_id_registry::lookup_mount_point_name(mount_point_ids::render);
    TEST_EXPECT(ctx, mount_name != nullptr);
    if (mount_name != nullptr)
    {
        TEST_EXPECT(ctx, std::strcmp(mount_name, "render") == 0);
    }

    const system_id_registry::SModuleRegistration* const module_registration =
        system_id_registry::find_module(module_ids::executive);
    TEST_EXPECT(ctx, module_registration != nullptr);
    if (module_registration != nullptr)
    {
        TEST_EXPECT(ctx,
            module_registration->index == module_ids::executive_index);
        TEST_EXPECT(ctx,
            module_registration->mount_point_id == mount_point_ids::executive);
    }
    const char* const module_name =
        system_id_registry::lookup_module_name(module_ids::executive);
    TEST_EXPECT(ctx, module_name != nullptr);
    if (module_name != nullptr)
    {
        TEST_EXPECT(ctx, std::strcmp(module_name, "executive") == 0);
    }

    const system_id_registry::SThreadRegistration* const thread_registration =
        system_id_registry::find_thread(thread_ids::bg_conditioning);
    TEST_EXPECT(ctx, thread_registration != nullptr);
    if (thread_registration != nullptr)
    {
        TEST_EXPECT(ctx,
            thread_registration->index == thread_ids::bg_conditioning_index);
    }
    const char* const thread_name =
        system_id_registry::lookup_thread_name(thread_ids::bg_conditioning);
    TEST_EXPECT(ctx, thread_name != nullptr);
    if (thread_name != nullptr)
    {
        TEST_EXPECT(ctx, std::strcmp(thread_name, "bg_conditioning") == 0);
    }

    char system_name[64]{};
    std::size_t system_name_size = 0u;
    TEST_EXPECT(ctx,
        system_id_registry::format_system_name(
            system_ids::executive,
            system_name,
            sizeof(system_name),
            system_name_size));
    TEST_EXPECT(ctx, std::strcmp(system_name, "executive:executive") == 0);
    TEST_EXPECT(ctx, system_name_size == std::strlen(system_name));

    char small_system_name[8]{};
    std::size_t small_system_name_size = 123u;
    TEST_EXPECT(ctx,
        !system_id_registry::format_system_name(
            system_ids::executive,
            small_system_name,
            sizeof(small_system_name),
            small_system_name_size));
    TEST_EXPECT(ctx, small_system_name[0] == 0);
    TEST_EXPECT(ctx, small_system_name_size == 0u);

    TEST_EXPECT(ctx, system_id_registry::lookup_type_name(system_type_ids::undefined) == nullptr);
    TEST_EXPECT(ctx, system_id_registry::lookup_mount_point_name({}) == nullptr);
    TEST_EXPECT(ctx, system_id_registry::lookup_module_name({}) == nullptr);
    TEST_EXPECT(ctx, system_id_registry::lookup_thread_name({}) == nullptr);

    const module_ids::id_type unregistered_module = module_ids::ops::make_id(
        mount_point_ids::render,
        module_ids::executive_index);
    const system_ids::id_type unregistered_system =
        system_ids::ops::make_system_id(unregistered_module, thread_ids::host);
    TEST_EXPECT(ctx, module_ids::ops::is_valid_id(unregistered_module));
    TEST_EXPECT(ctx, system_ids::ops::is_valid_id(unregistered_system));
    TEST_EXPECT(ctx,
        system_id_registry::find_module(unregistered_module) == nullptr);
    TEST_EXPECT(ctx,
        system_id_registry::lookup_module_name(unregistered_module) == nullptr);
    TEST_EXPECT(ctx,
        !system_id_registry::format_system_name(
            unregistered_system,
            system_name,
            sizeof(system_name),
            system_name_size));
    TEST_EXPECT(ctx, system_name[0] == 0);
    TEST_EXPECT(ctx, system_name_size == 0u);
}

void test_provisioning_and_shared_words(TTestContext& ctx)
{
    TInstance<debug_system::CDebugServiceState> owner =
        TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, owner.is_ready());

    debug_system::CDebugServiceState* const service = owner.operator->();
    TEST_EXPECT(ctx, debug_system::get_service() == nullptr);
    TEST_EXPECT(ctx, !debug_system::install_service(nullptr));
    TEST_EXPECT(ctx, debug_system::install_service(service));
    TEST_EXPECT(ctx, debug_system::get_service() == service);
    TEST_EXPECT(ctx, !debug_system::install_service(service));
    TEST_EXPECT(ctx, !debug_system::uninstall_service(nullptr));

    TEST_EXPECT(ctx, service->allocate_incident_id() == 1u);
    TEST_EXPECT(ctx, service->allocate_incident_id() == 2u);

    service->publish_configuration(0x55aa55aau);
    TEST_EXPECT(ctx, service->read_configuration() == 0x55aa55aau);

    int filtered_argument_evaluations = 0;
    service->publish_configuration(0u);
    MV_INFO("filtered {}", ++filtered_argument_evaluations);
    TEST_EXPECT(ctx, filtered_argument_evaluations == 0);
    TEST_EXPECT(ctx, !service->informational_event_enabled(debug_system::EEventLevel::info));
    service->publish_configuration(1u << debug_system::k_information_level_shift);
    TEST_EXPECT(ctx, service->informational_event_enabled(debug_system::EEventLevel::info));
    TEST_EXPECT(ctx, !service->informational_event_enabled(debug_system::EEventLevel::detail));
    service->publish_configuration(debug_system::k_information_level_mask);
    TEST_EXPECT(ctx, service->informational_event_enabled(debug_system::EEventLevel::trace));
    TEST_EXPECT(ctx, !service->critical_shutdown_enabled());
    service->publish_configuration(debug_system::k_information_level_mask | debug_system::k_critical_shutdown_enabled);
    TEST_EXPECT(ctx, service->critical_shutdown_enabled());

    debug_system::EBreakpointOverride breakpoint_override =
        debug_system::EBreakpointOverride::inherit;
    service->publish_configuration(debug_system::k_breakpoints_enabled);
    TEST_EXPECT(ctx, service->breakpoint_enabled(breakpoint_override, true));
    TEST_EXPECT(ctx, !service->breakpoint_enabled(breakpoint_override, false));
    breakpoint_override = debug_system::EBreakpointOverride::enabled;
    TEST_EXPECT(ctx, service->breakpoint_enabled(breakpoint_override, false));
    breakpoint_override = debug_system::EBreakpointOverride::disabled;
    TEST_EXPECT(ctx, !service->breakpoint_enabled(breakpoint_override, true));
    service->publish_configuration(0u);
    breakpoint_override = debug_system::EBreakpointOverride::enabled;
    TEST_EXPECT(ctx, !service->breakpoint_enabled(breakpoint_override, true));
    TEST_EXPECT(ctx, !service->breakpoint_pause_requested());
    debug_system::SBreakpointContext breakpoint_context;
    TEST_EXPECT(ctx,
        !service->read_breakpoint_context(breakpoint_context));

    TEST_EXPECT(ctx,
        service->read_shutdown_request() ==
        debug_system::EShutdownReason::none);
    const debug_system::SEventUsagePoint invalid_usage_point{};
    service->publish_configuration(debug_system::k_critical_shutdown_enabled);
    TEST_EXPECT(ctx,
        !(service->process_event<
            debug_system::EEventLevel::critical,
            debug_system::EEventType::event>(
            invalid_usage_point,
            breakpoint_override,
            true,
            debug_system::EShutdownReason::critical_incident,
            nullptr,
            0u,
            nullptr,
            0u)));
    TEST_EXPECT(ctx,
        service->read_shutdown_request() ==
        debug_system::EShutdownReason::critical_incident);
    TEST_EXPECT(ctx,
        !(service->process_event<
            debug_system::EEventLevel::fatal,
            debug_system::EEventType::event>(
            invalid_usage_point,
            breakpoint_override,
            true,
            debug_system::EShutdownReason::fatal_incident,
            nullptr,
            0u,
            nullptr,
            0u)));
    TEST_EXPECT(ctx,
        service->read_shutdown_request() ==
        debug_system::EShutdownReason::fatal_incident);
    service->request_shutdown(debug_system::EShutdownReason::fatal_incident);
    service->request_shutdown(
        debug_system::EShutdownReason::critical_incident);
    TEST_EXPECT(ctx,
        service->read_shutdown_request() ==
        debug_system::EShutdownReason::fatal_incident);
    service->request_shutdown(
        debug_system::EShutdownReason::panic_incident);
    TEST_EXPECT(ctx,
        service->read_shutdown_request() ==
        debug_system::EShutdownReason::panic_incident);

    TEST_EXPECT(ctx, debug_system::uninstall_service(service));
    TEST_EXPECT(ctx, debug_system::get_service() == nullptr);
}

void test_writer_and_direct_paths(TTestContext& ctx)
{
    const std::string event_path_storage =
        test_environment::test_log_path("debug_service_test");
    const std::string direct_path_storage =
        test_environment::test_log_path("debug_service_test_direct");
    const char* const event_path = event_path_storage.c_str();
    const char* const direct_path = direct_path_storage.c_str();
    constexpr char source_file[] =
        "discarded/source/prefix/which/is/intentionally/longer/than/"
        "the/bounded/source/storage/available/to/the/debug/event/"
        "tests/test_suites/DebugService_test_suite.cpp";
    constexpr std::uint32_t source_line = 321u;
    static_assert(sizeof(source_file) >
        debug_system::k_source_file_capacity);

    TInstance<debug_system::CDebugServiceState> owner =
        TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, owner.is_ready());

    debug_system::CDebugServiceState* const service = owner.operator->();
    TEST_EXPECT(ctx,
        service->configure_log_paths(event_path, direct_path));
    TEST_EXPECT(ctx,
        std::strcmp(service->event_log_path(), event_path) == 0);
    TEST_EXPECT(ctx,
        std::strcmp(service->direct_log_path(), direct_path) == 0);
    TEST_EXPECT(ctx, service->open_logs());
    TEST_EXPECT(ctx, debug_system::install_service(service));
    TEST_EXPECT(ctx, service->start());
    TEST_EXPECT(ctx,
        service->thread_state() ==
        debug_system::EServiceThreadState::running);

    MV_INFO("first transported event");
    TEST_EXPECT(ctx, debug_system::submit_text("second transported event"));
    TEST_EXPECT(ctx, debug_system::submit_text("literal {braces}"));
    debug_system::EBreakpointOverride breakpoint_override =
        debug_system::EBreakpointOverride::disabled;
    TEST_EXPECT(ctx,
        (debug_system::process_event<
            debug_system::EEventLevel::error,
            debug_system::EEventType::event>(
            debug_system::SEventUsagePoint{
                source_file, sizeof(source_file) - 1u, source_line },
            breakpoint_override,
            true,
            debug_system::EShutdownReason::none,
            "typed {} {} {} {} {} {} {} {}",
            std::int32_t{ -7 },
            system_type_ids::file_load_request,
            debug_system::CInlineText16{ "payload" },
            local_type_ids::test_runtime,
            type_id{ local_type_ids::asset_load },
            system_ids::host,
            module_ids::executable,
            thread_ids::host)));

    const system_type_id unregistered_type_id =
        system_type_ids::ops::encode_id(system_type_ids::ops::encode_index(system_type_ids::k_count));
    TEST_EXPECT(ctx,
        (debug_system::process_event<
            debug_system::EEventLevel::error,
            debug_system::EEventType::event>(
            debug_system::SEventUsagePoint{
                source_file, sizeof(source_file) - 1u, source_line + 1u },
            breakpoint_override,
            true,
            debug_system::EShutdownReason::none,
            "typed {} {} {} {}",
            unregistered_type_id,
            system_type_id{ 0x00000002u },
            local_type_ids::ops::encode_id(
                local_type_ids::ops::encode_index(local_type_ids::k_count)),
            local_type_id{ 0x00000002u })));

    char oversized[debug_system::k_event_format_capacity + 32u];
    std::memset(oversized, 'x', sizeof(oversized) - 1u);
    oversized[sizeof(oversized) - 1u] = 0;
    TEST_EXPECT(ctx, debug_system::submit_text(oversized));
    MV_REPORT("rich %s %d %.1f", "report", 42, 3.5);

    char report447[debug_system::k_event_report_capacity]{};
    std::memset(report447, 'r', sizeof(report447) - 1u);
    report447[0u] = 'A';
    report447[sizeof(report447) - 2u] = 'Z';
    TEST_EXPECT(ctx, debug_system::report(
        debug_system::SEventUsagePoint{
            source_file, sizeof(source_file) - 1u, source_line + 2u },
        "%s", report447));

    char report448[debug_system::k_event_report_capacity + 1u]{};
    std::memset(report448, 's', sizeof(report448) - 1u);
    report448[0u] = 'B';
    report448[sizeof(report448) - 2u] = 'Y';
    TEST_EXPECT(ctx, debug_system::report(
        debug_system::SEventUsagePoint{
            source_file, sizeof(source_file) - 1u, source_line + 3u },
        "%s", report448));

    MV_REPORT_IMMEDIATE("immediate %s %u", "report", 17u);

    char maximum_report[debug_system::k_format_buffer_capacity]{};
    std::memset(maximum_report, 'm', sizeof(maximum_report) - 1u);
    maximum_report[0u] = 'C';
    maximum_report[sizeof(maximum_report) - 2u] = 'X';
    TEST_EXPECT(ctx, debug_system::report(
        debug_system::SEventUsagePoint{
            source_file, sizeof(source_file) - 1u, source_line + 4u },
        "%s", maximum_report));

    char oversized_report[debug_system::k_format_buffer_capacity + 1u]{};
    std::memset(oversized_report, 'o', sizeof(oversized_report) - 1u);
    TEST_EXPECT(ctx, !debug_system::report(
        debug_system::SEventUsagePoint{
            source_file, sizeof(source_file) - 1u, source_line + 5u },
        "%s", oversized_report));

    const std::uint32_t panic_incident_id = service->allocate_incident_id();
    TEST_EXPECT(ctx, panic_incident_id == 13u);
    const debug_system::SEventUsagePoint panic_usage_point{
        source_file, sizeof(source_file) - 1u, source_line + 6u };
    TEST_EXPECT(ctx,
        service->try_write_panic_record(
            panic_usage_point,
            panic_incident_id,
            "panic substrate record",
            22u));

    const module_ids::id_type previous_module_id =
        system_context::get_ambient_module_id();
    const thread_ids::id_type previous_thread_id =
        system_context::get_ambient_thread_id();
    const module_ids::id_type unregistered_module = module_ids::ops::make_id(
        mount_point_ids::render,
        module_ids::executive_index);
    const system_ids::id_type unregistered_system =
        system_ids::ops::make_system_id(unregistered_module, thread_ids::host);
    system_context::set_ambient_module_id(unregistered_module);
    system_context::set_ambient_thread_id(thread_ids::host);
    TEST_EXPECT(ctx,
        debug_system::submit_text("unregistered system identity"));

    system_context::set_ambient_module_id();
    system_context::set_ambient_thread_id();
    const system_ids::id_type invalid_system =
        system_context::get_ambient_system_id();
    TEST_EXPECT(ctx, !system_ids::ops::is_valid_id(invalid_system));
    TEST_EXPECT(ctx, debug_system::submit_text("invalid system identity"));

    system_context::set_ambient_module_id(previous_module_id);
    system_context::set_ambient_thread_id(previous_thread_id);

#if MV_DEVELOPMENT_BUILD
    service->publish_configuration(debug_system::k_information_level_mask);
    MV_ASSERT(std::uint32_t{} == 1u);
#endif

    TEST_EXPECT(ctx, service->stop());
    TEST_EXPECT(ctx,
        service->thread_state() ==
        debug_system::EServiceThreadState::stopped);
    TEST_EXPECT(ctx, service->is_event_transport_closed());
    TEST_EXPECT(ctx, debug_system::uninstall_service(service));

    char system_marker[32]{};
    const int system_marker_size = std::snprintf(
        system_marker,
        sizeof(system_marker),
        "[%016llx]",
        static_cast<unsigned long long>(
            system_ids::host.raw_value()));
    TEST_EXPECT(ctx, system_marker_size == 18);
    TEST_EXPECT(ctx,
        tests::file_contains(event_path, "[executable:host]"));
    TEST_EXPECT(ctx,
        tests::file_contains(direct_path, "[executable:host]"));

    char unregistered_marker[64]{};
    const int unregistered_marker_size = std::snprintf(
        unregistered_marker,
        sizeof(unregistered_marker),
        "[unregistered-system:%016llx]",
        static_cast<unsigned long long>(unregistered_system.raw_value()));
    TEST_EXPECT(ctx, unregistered_marker_size > 0);

    char invalid_marker[64]{};
    const int invalid_marker_size = std::snprintf(
        invalid_marker,
        sizeof(invalid_marker),
        "[invalid-system:%016llx]",
        static_cast<unsigned long long>(invalid_system.raw_value()));
    TEST_EXPECT(ctx, invalid_marker_size > 0);

    constexpr const char* source_suffix =
        "DebugService_test_suite.cpp";
    char source_marker[192]{};
    const int source_marker_size = std::snprintf(
        source_marker,
        sizeof(source_marker),
        "[%s:%u] typed -7 file_load_request payload test_runtime asset_load "
        "executable:host executable host",
        source_suffix,
        source_line);
    TEST_EXPECT(ctx, source_marker_size > 0);

    const local_type_id unresolved_local = local_type_ids::ops::encode_id(
        local_type_ids::ops::encode_index(local_type_ids::k_count));
    char type_fallback_marker[256]{};
    const int type_fallback_marker_size = std::snprintf(
        type_fallback_marker,
        sizeof(type_fallback_marker),
        "[%s:%u] typed unregistered-type:0x%08x invalid-type:0x%08x "
        "unregistered-local-type:0x%08x invalid-local-type:0x%08x",
        source_suffix,
        source_line + 1u,
        static_cast<unsigned int>(unregistered_type_id.raw_value()),
        0x00000002u,
        static_cast<unsigned int>(unresolved_local.raw_value()),
        0x00000002u);
    TEST_EXPECT(ctx, type_fallback_marker_size > 0);

    TEST_EXPECT(ctx, tests::file_contains(event_path, "[0000000001]"));
    TEST_EXPECT(ctx, !tests::file_contains(event_path, system_marker));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[info:event]"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "first transported event"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[0000000002]"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "second transported event"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[0000000003]"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "literal {braces}"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[0000000004]"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[error:event]"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, source_marker));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[0000000005]"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[error:event]"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, type_fallback_marker));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, "[0000000006]"));
    TEST_EXPECT(ctx, !tests::file_contains(direct_path, system_marker));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[0000000007]"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "rich report 42 3.5"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[0000000008]"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, report447));
    TEST_EXPECT(ctx, !tests::file_contains(event_path, report448));
    TEST_EXPECT(ctx, !tests::file_contains(event_path, "immediate report 17"));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, "[0000000009]"));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, report448));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, "[0000000010]"));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, "immediate report 17"));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, "[0000000011]"));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, maximum_report));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, "[0000000012]"));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, "Invalid rich debug report"));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, "[0000000013]"));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, "[panic:event]"));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, "panic substrate record"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[0000000014]"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, unregistered_marker));
    TEST_EXPECT(ctx,
        tests::file_contains(event_path, "unregistered system identity"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[0000000015]"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, invalid_marker));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "invalid system identity"));
#if MV_DEVELOPMENT_BUILD
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[0000000016]"));
    TEST_EXPECT(ctx,
        tests::file_contains(event_path,
            "Assertion failed: std::uint32_t{} == 1u"));
#endif
}

void test_explicit_log_opening(TTestContext& ctx)
{
    const std::string event_path_storage =
        test_environment::test_log_path("debug_service_explicit_test");
    const std::string direct_path_storage =
        test_environment::test_log_path("debug_service_explicit_test_direct");
    const char* const event_path = event_path_storage.c_str();
    const char* const direct_path = direct_path_storage.c_str();

    TInstance<debug_system::CDebugServiceState> owner =
        TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, owner.is_ready());

    debug_system::CDebugServiceState* const service = owner.operator->();
    TEST_EXPECT(ctx,
        service->configure_log_paths(event_path, direct_path));
    TEST_EXPECT(ctx, !service->start());
    TEST_EXPECT(ctx, service->thread_state() == debug_system::EServiceThreadState::empty);
    TEST_EXPECT(ctx, service->open_logs());
    TEST_EXPECT(ctx, debug_system::install_service(service));
    TEST_EXPECT(ctx, service->start());

    char oversized[debug_system::k_event_format_capacity + 32u];
    std::memset(oversized, 'y', sizeof(oversized) - 1u);
    oversized[sizeof(oversized) - 1u] = 0;
    TEST_EXPECT(ctx, debug_system::submit_text(oversized));

    TEST_EXPECT(ctx, service->stop());
    TEST_EXPECT(ctx, debug_system::uninstall_service(service));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, "[0000000001] "));
}

void test_filename_and_capacity_boundaries(TTestContext& ctx)
{
    const std::string event_path_storage =
        test_environment::test_log_path("debug_service_filename_test");
    const std::string direct_path_storage =
        test_environment::test_log_path("debug_service_filename_test_direct");
    const char* const event_path = event_path_storage.c_str();
    const char* const direct_path = direct_path_storage.c_str();
    constexpr char filename31[] =
        "root\\mixed/path/1234567890123456789012345678901";
    constexpr char filename32[] =
        "root/mixed\\path/12345678901234567890123456789012";
    constexpr char utf8_filename[] =
        "root/path/AAAAA\xe2\x82\xac" "123456789012345678901234567";

    TInstance<debug_system::CDebugServiceState> owner =
        TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, owner.is_ready());
    debug_system::CDebugServiceState* const service = owner.operator->();
    TEST_EXPECT(ctx, service->configure_log_paths(event_path, direct_path));
    TEST_EXPECT(ctx, service->open_logs());
    TEST_EXPECT(ctx, service->start());

    debug_system::EBreakpointOverride breakpoint_override =
        debug_system::EBreakpointOverride::disabled;
    TEST_EXPECT(ctx,
        (service->process_event<
            debug_system::EEventLevel::error,
            debug_system::EEventType::event>(
            debug_system::SEventUsagePoint{
                filename31, sizeof(filename31) - 1u, 701u },
            breakpoint_override, true, debug_system::EShutdownReason::none,
            nullptr, 0u, "filename-31", sizeof("filename-31") - 1u)));
    TEST_EXPECT(ctx,
        (service->process_event<
            debug_system::EEventLevel::error,
            debug_system::EEventType::event>(
            debug_system::SEventUsagePoint{
                filename32, sizeof(filename32) - 1u, 702u },
            breakpoint_override, true, debug_system::EShutdownReason::none,
            nullptr, 0u, "filename-32", sizeof("filename-32") - 1u)));
    TEST_EXPECT(ctx,
        (service->process_event<
            debug_system::EEventLevel::error,
            debug_system::EEventType::event>(
            debug_system::SEventUsagePoint{
                utf8_filename, sizeof(utf8_filename) - 1u, 703u },
            breakpoint_override, true, debug_system::EShutdownReason::none,
            nullptr, 0u, "filename-utf8", sizeof("filename-utf8") - 1u)));
    constexpr char plain_filename[] = "plain.cpp";
    TEST_EXPECT(ctx,
        (service->process_event<
            debug_system::EEventLevel::error,
            debug_system::EEventType::event>(
            debug_system::SEventUsagePoint{
                plain_filename, sizeof(plain_filename) - 1u, 704u },
            breakpoint_override, true, debug_system::EShutdownReason::none,
            nullptr, 0u, "filename-plain", sizeof("filename-plain") - 1u)));
    constexpr char trailing_separator[] = "root/path/";
    TEST_EXPECT(ctx,
        (service->process_event<
            debug_system::EEventLevel::error,
            debug_system::EEventType::event>(
            debug_system::SEventUsagePoint{
                trailing_separator, sizeof(trailing_separator) - 1u, 705u },
            breakpoint_override, true, debug_system::EShutdownReason::none,
            nullptr, 0u, "trailing-separator", sizeof("trailing-separator") - 1u)));

    char maximum_expression[debug_system::k_event_expression_capacity]{};
    std::memset(maximum_expression, 'e', sizeof(maximum_expression) - 1u);
    TEST_EXPECT(ctx,
        (service->process_event<
            debug_system::EEventLevel::assert,
            debug_system::EEventType::condition>(
            debug_system::SEventUsagePoint{
                plain_filename, sizeof(plain_filename) - 1u, 706u },
            breakpoint_override, false, debug_system::EShutdownReason::none,
            maximum_expression, sizeof(maximum_expression) - 1u, "", 0u)));

    char oversized_expression[debug_system::k_event_expression_capacity + 1u]{};
    std::memset(oversized_expression, 'x', sizeof(oversized_expression) - 1u);
    TEST_EXPECT(ctx,
        !(service->process_event<
            debug_system::EEventLevel::assert,
            debug_system::EEventType::condition>(
            debug_system::SEventUsagePoint{
                plain_filename, sizeof(plain_filename) - 1u, 707u },
            breakpoint_override, false, debug_system::EShutdownReason::none,
            oversized_expression, sizeof(oversized_expression) - 1u, "", 0u)));

    char maximum_format[debug_system::k_event_format_capacity]{};
    std::memset(maximum_format, 'q', sizeof(maximum_format) - 1u);
    TEST_EXPECT(ctx,
        (service->submit_event<
            debug_system::EEventLevel::info,
            debug_system::EEventType::event>(
            debug_system::SEventUsagePoint{
                plain_filename, sizeof(plain_filename) - 1u, 708u },
            maximum_format, sizeof(maximum_format) - 1u)));

    char oversized_format[debug_system::k_event_format_capacity + 1u]{};
    std::memset(oversized_format, 'z', sizeof(oversized_format) - 1u);
    TEST_EXPECT(ctx,
        !(service->submit_event<
            debug_system::EEventLevel::info,
            debug_system::EEventType::event>(
            debug_system::SEventUsagePoint{
                filename31, sizeof(filename31) - 1u, 709u },
            oversized_format, sizeof(oversized_format) - 1u)));

    TEST_EXPECT(ctx,
        (service->process_event<
            debug_system::EEventLevel::assert,
            debug_system::EEventType::condition>(
            debug_system::SEventUsagePoint{
                plain_filename, sizeof(plain_filename) - 1u, 710u },
            breakpoint_override, false, debug_system::EShutdownReason::none,
            "Assertion failed: split | ",
            sizeof("Assertion failed: split | ") - 1u,
            "value {}", sizeof("value {}") - 1u, std::uint32_t{ 42u })));

    TEST_EXPECT(ctx, service->stop());
    TEST_EXPECT(ctx,
        tests::file_contains(event_path,
            "[1234567890123456789012345678901:701] filename-31"));
    TEST_EXPECT(ctx,
        tests::file_contains(event_path,
            "[...5678901234567890123456789012:702] filename-32"));
    TEST_EXPECT(ctx,
        tests::file_contains(event_path,
            "[...123456789012345678901234567:703] filename-utf8"));
    TEST_EXPECT(ctx,
        tests::file_contains(event_path, "[plain.cpp:704] filename-plain"));
    TEST_EXPECT(ctx,
        tests::file_contains(event_path, "[error:event] trailing-separator"));
    TEST_EXPECT(ctx,
        tests::file_contains(event_path, "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"));
    TEST_EXPECT(ctx,
        tests::file_contains(event_path, "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq"));
    TEST_EXPECT(ctx,
        tests::file_contains(event_path,
            "[plain.cpp:710] Assertion failed: split | value 42"));
}

void test_queued_and_direct_equivalence(TTestContext& ctx)
{
    const std::string event_path_storage =
        test_environment::test_log_path("debug_service_equivalence_test");
    const std::string direct_path_storage =
        test_environment::test_log_path("debug_service_equivalence_test_direct");
    const char* const event_path = event_path_storage.c_str();
    const char* const direct_path = direct_path_storage.c_str();
    constexpr char source_file[] = "tests/test_suites/DebugService_test_suite.cpp";
    const debug_system::SEventUsagePoint usage_point{
        source_file, sizeof(source_file) - 1u, 777u };

    TInstance<debug_system::CDebugServiceState> owner =
        TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, owner.is_ready());
    debug_system::CDebugServiceState* const service = owner.operator->();
    TEST_EXPECT(ctx, service->configure_log_paths(event_path, direct_path));
    TEST_EXPECT(ctx, service->open_logs());
    TEST_EXPECT(ctx, debug_system::install_service(service));

    for (std::uint32_t index = 0u;
        index < debug_system::CEventTransport::k_capacity; ++index)
    {
        TEST_EXPECT(ctx,
            (service->submit_event<
                debug_system::EEventLevel::info,
                debug_system::EEventType::event>(
                usage_point, "equivalence {}",
                sizeof("equivalence {}") - 1u, std::uint32_t{ 42u })));
    }

    TEST_EXPECT(ctx,
        (service->submit_event<
            debug_system::EEventLevel::info,
            debug_system::EEventType::event>(
                usage_point, "direct ids {} {} {} {} {} {}",
            sizeof("direct ids {} {} {} {} {} {}") - 1u,
            local_type_ids::test_runtime,
            local_type_ids::ops::encode_id(
                local_type_ids::ops::encode_index(local_type_ids::k_count)),
            type_id{ system_type_ids::byte_buffer },
            system_ids::host,
            module_ids::executable,
            thread_ids::host)));
    TEST_EXPECT(ctx, debug_system::report(
        debug_system::SEventUsagePoint{
            source_file, sizeof(source_file) - 1u, 778u },
        "full %s", "report fallback"));
    TEST_EXPECT(ctx, service->start());
    TEST_EXPECT(ctx, service->stop());
    constexpr const char* reconstructed =
        "[info:event] [DebugService_test_suite.cpp:777] equivalence 42";
    TEST_EXPECT(ctx, tests::file_contains(event_path, reconstructed));
    TEST_EXPECT(ctx, tests::file_contains(direct_path,
        "[DebugService_test_suite.cpp:777] direct ids test_runtime "
        "unregistered-local-type:"));
    TEST_EXPECT(ctx, tests::file_contains(direct_path, "byte_buffer"));
    TEST_EXPECT(ctx, tests::file_contains(direct_path,
        "byte_buffer executable:host executable host"));
    TEST_EXPECT(ctx, tests::file_contains(direct_path,
        "[DebugService_test_suite.cpp:778] full report fallback"));
    TEST_EXPECT(ctx, !debug_system::report(
        debug_system::SEventUsagePoint{
            source_file, sizeof(source_file) - 1u, 779u },
        "closed %s", "report fallback"));
    TEST_EXPECT(ctx, debug_system::uninstall_service(service));
    owner.reset();

    TEST_EXPECT(ctx, !tests::file_contains(direct_path,
        "[DebugService_test_suite.cpp:779] closed report fallback"));
}

void test_malformed_event_overlays(TTestContext& ctx)
{
    const std::string event_path_storage =
        test_environment::test_log_path("debug_service_malformed_test");
    const std::string direct_path_storage =
        test_environment::test_log_path("debug_service_malformed_test_direct");
    const char* const event_path = event_path_storage.c_str();
    const char* const direct_path = direct_path_storage.c_str();

    TInstance<debug_system::CDebugServiceState> owner =
        TInstance<debug_system::CDebugServiceState>::create();
    TEST_EXPECT(ctx, owner.is_ready());
    debug_system::CDebugServiceState* const service = owner.operator->();
    TEST_EXPECT(ctx, service->configure_log_paths(event_path, direct_path));
    TEST_EXPECT(ctx, service->open_logs());

    debug_system::SEvent valid{};
    valid.system_id = system_ids::host;
    valid.incident_id = 1u;
    valid.representation = debug_system::EEventRepresentation::report;
    valid.level = debug_system::EEventLevel::info;
    valid.type = debug_system::EEventType::event;
    valid.metadata.report.report_length = 3u;
    valid.storage.report[0u] = 0;
    std::memcpy(valid.storage.report, "abc", 4u);
    TEST_EXPECT(ctx,
        debug_system::SDebugServiceTestAccess::write_event(*service, valid));

    debug_system::SEvent malformed = valid;
    malformed.incident_id = 2u;
    malformed.representation =
        static_cast<debug_system::EEventRepresentation>(0xffu);
    TEST_EXPECT(ctx,
        debug_system::SDebugServiceTestAccess::write_event(*service, malformed));

    malformed = valid;
    malformed.incident_id = 3u;
    malformed.metadata.report.report_length =
        static_cast<std::uint16_t>(debug_system::k_event_report_capacity);
    TEST_EXPECT(ctx,
        debug_system::SDebugServiceTestAccess::write_event(*service, malformed));

    malformed = valid;
    malformed.incident_id = 4u;
    malformed.metadata.report.reserved[0u] = 1u;
    TEST_EXPECT(ctx,
        debug_system::SDebugServiceTestAccess::write_event(*service, malformed));

    malformed = valid;
    malformed.incident_id = 5u;
    malformed.storage.report[1u] = 0;
    TEST_EXPECT(ctx,
        debug_system::SDebugServiceTestAccess::write_event(*service, malformed));

    malformed = valid;
    malformed.incident_id = 6u;
    malformed.storage.report[3u] = 'x';
    TEST_EXPECT(ctx,
        debug_system::SDebugServiceTestAccess::write_event(*service, malformed));

    malformed = valid;
    malformed.incident_id = 7u;
    malformed.storage.report[4u] = 'x';
    TEST_EXPECT(ctx,
        debug_system::SDebugServiceTestAccess::write_event(*service, malformed));

    malformed = {};
    malformed.system_id = system_ids::host;
    malformed.incident_id = 8u;
    malformed.representation = debug_system::EEventRepresentation::structured;
    malformed.level = debug_system::EEventLevel::info;
    malformed.type = debug_system::EEventType::event;
    malformed.metadata.structured.format_size = 3u;
    malformed.metadata.structured.content_type =
        static_cast<debug_system::EStructuredContentType>(0xffu);
    std::memcpy(malformed.storage.structured.format, "abc", 4u);
    TEST_EXPECT(ctx,
        debug_system::SDebugServiceTestAccess::write_event(*service, malformed));

    TEST_EXPECT(ctx, service->start());
    TEST_EXPECT(ctx, service->stop());
    TEST_EXPECT(ctx, tests::file_contains(event_path, "[0000000001]"));
    TEST_EXPECT(ctx, tests::file_contains(event_path, "abc"));
    for (std::uint32_t incident = 2u; incident <= 8u; ++incident)
    {
        char marker[32]{};
        const int marker_size = std::snprintf(
            marker, sizeof(marker), "[%010u]", incident);
        TEST_EXPECT(ctx, marker_size == 12);
        TEST_EXPECT(ctx, tests::file_contains(direct_path, marker));
    }
    TEST_EXPECT(ctx,
        tests::file_contains(direct_path, "Invalid transported debug event"));
}

}   //  namespace debug_service_tests

int run_debug_service_tests()
{
    debug_service_tests::TTestContext ctx;
    debug_service_tests::compile_public_macro_interface(false);
    debug_service_tests::test_exclusive_lock_try_acquire(ctx);
    debug_service_tests::test_process_log_paths(ctx);
    debug_service_tests::test_argument_encoding(ctx);
    debug_service_tests::test_argument_formatting(ctx);
    debug_service_tests::test_system_id_name_registry(ctx);
    debug_service_tests::test_provisioning_and_shared_words(ctx);
    debug_service_tests::test_writer_and_direct_paths(ctx);
    debug_service_tests::test_explicit_log_opening(ctx);
    debug_service_tests::test_filename_and_capacity_boundaries(ctx);
    debug_service_tests::test_queued_and_direct_equivalence(ctx);
    debug_service_tests::test_malformed_event_overlays(ctx);

    std::cout << "DebugService: " << ctx.passed << " passed, "
        << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
