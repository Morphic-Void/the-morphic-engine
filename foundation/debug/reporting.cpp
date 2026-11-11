
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    reporting.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Nov 26
//
//  Per-binary reporting access to the shared Foundation debug service.

#include "debug/service.hpp"

#include <cstdio>

#include "platform/system/debugger.hpp"

namespace debug_system
{

namespace ambient_state
{

//  Each linked module stores its own installed debug service pointer.
static CDebugServiceState* s_service = nullptr;

}   //  namespace ambient_state

namespace reporting_detail
{

bool submit_event(CDebugServiceState& service,
    const EEventLevel level, const EEventType type, const bool immediate,
    const SEventUsagePoint& usage_point, const char* const format, const std::size_t format_size,
    const SEventArguments& arguments) noexcept
{
    return service.submit_encoded_event(level, type, immediate, usage_point, format, format_size, arguments);
}

bool process_event(CDebugServiceState& service,
    const EEventLevel level, const EEventType type, const bool immediate,
    const SEventUsagePoint& usage_point, EBreakpointOverride& breakpoint_override,
    const bool breakpoint_enabled_by_default, const EShutdownReason shutdown_reason,
    const char* const expression, const std::size_t expression_size,
    const char* const format, const std::size_t format_size,
    const SEventArguments& arguments) noexcept
{
    return service.process_encoded_event(level, type, immediate,
        usage_point, breakpoint_override, breakpoint_enabled_by_default, shutdown_reason,
        expression, expression_size, format, format_size, arguments);
}

}   //  namespace reporting_detail

CDebugServiceState* get_service() noexcept
{
    return ambient_state::s_service;
}

bool install_service(CDebugServiceState* const service) noexcept
{
    if ((service == nullptr) || (ambient_state::s_service != nullptr))
    {
        return false;
    }

    ambient_state::s_service = service;
    return true;
}

bool uninstall_service(CDebugServiceState* const expected) noexcept
{
    if ((expected == nullptr) || (ambient_state::s_service != expected))
    {
        return false;
    }

    ambient_state::s_service = nullptr;
    return true;
}

bool submit_text(const char* const text) noexcept
{
    CDebugServiceState* const service = get_service();
    return (service != nullptr) && service->submit_text(text);
}

bool informational_event_enabled(const EEventLevel level) noexcept
{
    CDebugServiceState* const service = get_service();
    return (service != nullptr) && service->informational_event_enabled(level);
}

bool report(const EEventLevel level, const SEventUsagePoint& usage_point, const char* const format, ...) noexcept
{
    CDebugServiceState* const service = get_service();
    if (service == nullptr)
    {
        return false;
    }

    std::va_list arguments;
    va_start(arguments, format);
    const bool reported = service->report_va(level, usage_point, format, arguments);
    va_end(arguments);
    return reported;
}

bool report_immediate(
    const EEventLevel level, const SEventUsagePoint& usage_point,
    const char* const format, ...) noexcept
{
    CDebugServiceState* const service = get_service();
    if (service == nullptr)
    {
        return false;
    }

    std::va_list arguments;
    va_start(arguments, format);
    const bool reported = service->report_immediate_va(level, usage_point, format, arguments);
    va_end(arguments);
    return reported;
}

[[noreturn]] void panic(const SEventUsagePoint& usage_point, const char* const format, ...) noexcept
{
    constexpr char format_failure[] = "Invalid panic report";
    char text[k_format_buffer_capacity]{};

    int formatted_size = -1;
    if ((format != nullptr) && (format[0] != 0))
    {
        std::va_list arguments;
        va_start(arguments, format);
        formatted_size = std::vsnprintf(text, sizeof(text), format, arguments);
        va_end(arguments);
    }

    const bool formatted = (formatted_size > 0) && (static_cast<std::size_t>(formatted_size) < sizeof(text));
    const char* const panic_text = formatted ? text : format_failure;
    const std::size_t panic_text_size = formatted ? static_cast<std::size_t>(formatted_size) : sizeof(format_failure) - 1u;

    CDebugServiceState* const service = get_service();
    if (service != nullptr)
    {
        service->request_shutdown(EShutdownReason::panic_incident);
        const std::uint32_t incident_id = service->allocate_incident_id();
        (void)service->try_write_panic_record(usage_point, incident_id, panic_text, panic_text_size);
    }

    platform::system::write_debugger_output("PANIC: ");
    platform::system::write_debugger_output(panic_text);
    platform::system::write_debugger_output("\n");

    if (platform::system::debugger_attached())
    {
        platform::system::break_into_debugger();
    }

    platform::system::fail_fast();
}

}   //  namespace debug_system
