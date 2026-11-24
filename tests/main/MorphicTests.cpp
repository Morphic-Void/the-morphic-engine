
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    MorphicTests.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    21 Oct 26

#include <iostream>
#include <cstring>

#include "tests/environment/test_environment.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/main/run_tests.hpp"
#include "tests/test_suites/ShaderReflection_test_suite.hpp"
#include "tests/test_suites/TBulkTransport_test_suite.hpp"
#include "tests/test_suites/HostPump_test_suite.hpp"
#include "tests/test_suites/Audio_test_suite.hpp"
#include "tests/test_suites/TextureService_test_suite.hpp"
#include "tests/test_suites/TextureBatch_test_suite.hpp"
#include "tests/test_suites/BundleService_test_suite.hpp"
#include "tests/test_suites/ErasedOwner_test_suite.hpp"
#include "tests/test_suites/SchemaReference_test_cases.hpp"
#include "tests/test_suites/Schema_test_suite.hpp"
#include "tests/test_suites/SchemaModel_test_cases.hpp"
#include "tests/test_suites/SchemaTraversal_test_cases.hpp"

int run_schema_process_focused();
int run_module_preamble_focused();
int run_platform_execution_focused();
int run_audio_execution_focused();
int run_rendering_execution_focused();
int run_execution_failure_focused();
int run_ordered_execution_focused();
int run_host_services_focused();
int run_host_maintenance_focused();
int run_bundle_request_focused();
int run_common_bulk_focused();
int run_common_establishment_focused();
int run_common_pod_focused();
int run_common_native_join_focused();
int run_common_multi_focused();
int run_lifecycle_service_focused();
int run_lifecycle_executive_focused();
int run_provider_execution_focused(const bool refusals_only, const bool retirement_only = false, const bool notification_only = false);
int run_pump_executive_focused();
int run_destroy_refusal_focused();
int run_pump_credits_focused();
int run_pump_borrow_focused();
int run_pump_close_focused();
int run_pump_notification_focused();
int run_pump_modal_focused();
int run_maintenance_pressure_focused();
int run_pump_schema_focused();
int run_pump_platform_shell_focused();
int run_pump_report_focused();
int run_pump_report_injected_focused();
int run_input_source_focused();
int run_input_freeze_focused();
int run_platform_input_freeze_focused();
int run_input_borrow_focused();
int run_input_installation_focused();
int run_input_secondary_focused();
int run_input_unexpected_close_focused();
int run_pump_native_focused();
int run_host_fatal_probe(const unsigned mode);
int run_platform_window_focused();
int run_platform_report_focused();
int run_platform_input_focused();
int run_platform_input_shutdown_focused();
int run_platform_native_tests();
int run_platform_input_native(const bool interactive);
int run_platform_controller_native(const bool interactive);

int main(const int argc, char** const argv)
{
    if (should_print_usage(argc, argv))
    {
        print_usage();
        return 0;
    }
    const char* log_tag = nullptr;
    if (!parse_log_tag(argc, argv, log_tag))
    {
        std::cerr <<
            "Invalid --log-tag. Use --log-tag=<value> with 1-48 ASCII letters, digits, '.', '_' or '-'.\n";
        return 2;
    }
    const char* output_directory = nullptr;
    if (!parse_output_directory(argc, argv, output_directory))
    {
        std::cerr <<
            "Invalid --output-directory. Use --output-directory=<path>.\n";
        return 2;
    }
    if (!test_environment::initialise_paths(
        (argc > 0) ? argv[0] : nullptr, log_tag, output_directory))
    {
        std::cerr << "MorphicTests could not initialise repository paths and log output.\n";
        return 1;
    }
    std::cout << "MorphicTests logs: " << test_environment::log_path_pattern() << '\n';
    if (!test_environment::install())
    {
        std::cerr << "MorphicTests could not install its executable environment.\n";
        return 1;
    }

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--schema-process") == 0)
        {
            const int result = run_schema_process_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--schema-traversal") == 0)
        {
            const int result = run_schema_traversal_tests();
            if (!test_environment::is_clean()) return 1;
            return result;
        }
        if (std::strcmp(argv[i], "--schema") == 0)
        {
            const int model_result = run_schema_model_tests();
            const int result = model_result + run_schema_tests();
            if (!test_environment::is_clean()) return 1;
            return result;
        }
        if (std::strcmp(argv[i], "--schema-references") == 0)
        {
            const int result = run_schema_reference_tests();
            if (!test_environment::is_clean()) return 1;
            return result;
        }
        if (std::strcmp(argv[i], "--bulk-transport") == 0)
        {
            const int result = run_bulk_transport_tests();
            if (!test_environment::is_clean()) return 1;
            return result;
        }
        if (std::strcmp(argv[i], "--platform-report") == 0)
        {
            const int result = run_platform_report_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--host-fatal-start-clock") == 0)
        {
            return run_host_fatal_probe(3u);
        }
        if (std::strcmp(argv[i], "--host-fatal-read-clock") == 0)
        {
            return run_host_fatal_probe(4u);
        }
        if (std::strcmp(argv[i], "--host-fatal-deadline") == 0)
        {
            return run_host_fatal_probe(0u);
        }
        if (std::strcmp(argv[i], "--erased-owner") == 0)
        {
            return run_erased_owner_tests();
        }
        if (std::strcmp(argv[i], "--host-endpoints") == 0)
        {
            return run_host_pump_tests();
        }
        if (std::strcmp(argv[i], "--host-services") == 0)
        {
            const int result = run_host_services_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--bundle-service") == 0)
        {
            const int result = run_bundle_service_tests();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--bundle-requests") == 0)
        {
            const int result = run_bundle_request_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--common-bulk") == 0)
        {
            const int result = run_common_bulk_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--common-establishment") == 0)
        {
            const int result = run_common_establishment_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--common-pod") == 0)
        {
            const int result = run_common_pod_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--common-native-join") == 0)
        {
            const int result = run_common_native_join_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--common-multi") == 0)
        {
            const int result = run_common_multi_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--host-maintenance") == 0)
        {
            const int result = run_host_maintenance_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--audio") == 0)
        {
            return run_audio_tests();
        }
        if (std::strcmp(argv[i], "--texture-service") == 0)
        {
            return run_texture_service_tests();
        }
        if (std::strcmp(argv[i], "--module-preamble") == 0)
        {
            const int result = run_module_preamble_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--ordered-execution") == 0)
        {
            const int result = run_ordered_execution_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--execution-failure") == 0)
        {
            const int result = run_execution_failure_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--platform-execution") == 0)
        {
            const int result = run_platform_execution_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--lifecycle-executive") == 0)
        {
            const int result = run_lifecycle_executive_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--provider-execution") == 0)
        {
            const int result = run_provider_execution_focused(false);
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--provider-refusals") == 0)
        {
            const int result = run_provider_execution_focused(true);
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--pump-executive") == 0)
        {
            const int result = run_pump_executive_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--destroy-refusal") == 0)
        {
            const int result = run_destroy_refusal_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--pump-credits") == 0)
        {
            const int result = run_pump_credits_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--pump-borrow") == 0)
        {
            const int result = run_pump_borrow_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--provider-retirement") == 0)
        {
            const int result = run_provider_execution_focused(false, true);
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--pump-close") == 0)
        {
            const int result = run_pump_close_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--pump-notification") == 0)
        {
            const int result = run_pump_notification_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--provider-notification") == 0)
        {
            const int result = run_provider_execution_focused(false, false, true);
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--pump-modal") == 0)
        {
            const int result = run_pump_modal_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--maintenance-pressure") == 0)
        {
            const int result = run_maintenance_pressure_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--pump-schema") == 0)
        {
            const int result = run_pump_schema_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--pump-platform-shell") == 0)
        {
            const int result = run_pump_platform_shell_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--pump-report") == 0)
        {
            const int result = run_pump_report_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--pump-report-injected") == 0)
        {
            const int result = run_pump_report_injected_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--pump-native-custody") == 0)
        {
            const int result = run_pump_native_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--input-freeze-cleanup") == 0)
        {
            const int result = run_input_freeze_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--platform-input-freeze") == 0)
        {
            const int result = run_platform_input_freeze_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--input-source-custody") == 0)
        {
            const int result = run_input_source_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--input-borrow-custody") == 0)
        {
            const int result = run_input_borrow_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--input-installation-cleanup") == 0)
        {
            const int result = run_input_installation_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--input-unexpected-close") == 0)
        {
            const int result = run_input_unexpected_close_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--input-secondary-cleanup") == 0)
        {
            const int result = run_input_secondary_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--lifecycle-service") == 0)
        {
            const int result = run_lifecycle_service_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--audio-execution") == 0)
        {
            const int result = run_audio_execution_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--rendering-execution") == 0)
        {
            const int result = run_rendering_execution_focused();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--texture-batch") == 0)
        {
            const int result = run_texture_batch_tests();
            return test_environment::is_clean() ? result : 1;
        }
        if (std::strcmp(argv[i], "--platform-input-shutdown") == 0)
        {
            return run_platform_input_shutdown_focused();
        }
        if (std::strcmp(argv[i], "--platform-window") == 0) return run_platform_window_focused();
        if (std::strcmp(argv[i], "--platform-input") == 0)
        {
            return run_platform_input_focused();
        }
        if (std::strcmp(argv[i], "--platform-native") == 0) return run_platform_native_tests();
        if (std::strcmp(argv[i], "--platform-input-native") == 0) return run_platform_input_native(false);
        if (std::strcmp(argv[i], "--platform-input-interactive") == 0) return run_platform_input_native(true);
        if (std::strcmp(argv[i], "--platform-controller-native") == 0)
        {
            return run_platform_controller_native(false);
        }
        if (std::strcmp(argv[i], "--platform-controller-interactive") == 0)
        {
            return run_platform_controller_native(true);
        }
        constexpr char prefix[] = "--reflection-arena-fatal=";
        if (std::strncmp(argv[i], prefix, sizeof(prefix) - 1u) == 0)
        {
            return run_shader_reflection_arena_fatal(argv[i] + sizeof(prefix) - 1u);
        }
    }
    const int result = run_tests(parse_test_run_mode(argc, argv));
    if (!test_environment::is_clean())
    {
        std::cerr << "MorphicTests finished with residual service or memory state.\n";
        return (result == 0) ? 1 : result;
    }
    return result;
}
