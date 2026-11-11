
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    reporting.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Nov 26
//
//  Bounded reporting without service administration or native thread state.

#pragma once

#ifndef DEBUG_REPORTING_HPP_INCLUDED
#define DEBUG_REPORTING_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <utility>

#include "debug/event_arguments.hpp"

namespace debug_system
{

constexpr std::size_t k_event_expression_capacity = 192u;
constexpr std::size_t k_event_format_capacity = 128u;
enum class EShutdownReason : std::uint32_t
{
    none = 0u,
    critical_incident,
    fatal_incident,
    panic_incident
};

enum class EEventLevel : std::uint8_t
{
    info = 0u,
    detail,
    trace,
    assert,
    warning,
    error,
    critical,
    fatal
};

enum class EEventType : std::uint8_t
{
    condition = 0u,
    event
};

enum class EBreakpointOverride : std::uint32_t
{
    inherit = 0u,
    enabled,
    disabled
};

[[nodiscard]] constexpr bool is_log_level(const EEventLevel level) noexcept
{
    return (level == EEventLevel::info) || (level == EEventLevel::detail) ||
        (level == EEventLevel::trace) || (level == EEventLevel::warning) ||
        (level == EEventLevel::error);
}

//==============================================================================
//  Event metadata validation
//==============================================================================

[[nodiscard]] constexpr bool is_valid_event_metadata(const EEventLevel level, const EEventType type) noexcept
{
    switch (level)
    {
        case EEventLevel::info:
        case EEventLevel::detail:
        case EEventLevel::trace:
        case EEventLevel::warning:
        case EEventLevel::error:
        {
            return type == EEventType::event;
        }
        case EEventLevel::assert:
        {
            return type == EEventType::condition;
        }
        case EEventLevel::critical:
        case EEventLevel::fatal:
        {
            return (type == EEventType::condition) || (type == EEventType::event);
        }
        default:
        {
            return false;
        }
    }
}

struct SEventUsagePoint
{
    const char* file = nullptr;
    std::size_t file_size = 0u;
    std::uint32_t line = 0u;
};

//  The installed pointer belongs to the executing binary. Administration is
//  declared in service.hpp; reporting needs only the incomplete service type.
class CDebugServiceState;
[[nodiscard]] CDebugServiceState* get_service() noexcept;

namespace reporting_detail
{

[[nodiscard]] bool submit_event(CDebugServiceState& service,
    const EEventLevel level, const EEventType type, const bool immediate,
    const SEventUsagePoint& usage_point, const char* const format, const std::size_t format_size,
    const SEventArguments& arguments) noexcept;
[[nodiscard]] bool process_event(CDebugServiceState& service,
    const EEventLevel level, const EEventType type, const bool immediate,
    const SEventUsagePoint& usage_point, EBreakpointOverride& breakpoint_override,
    const bool breakpoint_enabled_by_default, const EShutdownReason shutdown_reason,
    const char* const expression, const std::size_t expression_size,
    const char* const format, const std::size_t format_size,
    const SEventArguments& arguments) noexcept;

}   //  namespace reporting_detail

[[nodiscard]] bool submit_text(const char* const text) noexcept;
[[nodiscard]] bool informational_event_enabled(const EEventLevel level) noexcept;
[[nodiscard]] bool report(const EEventLevel level, const SEventUsagePoint& usage_point, const char* const format, ...) noexcept;
[[nodiscard]] bool report_immediate(const EEventLevel level, const SEventUsagePoint& usage_point, const char* const format, ...) noexcept;
[[noreturn]] void panic(const SEventUsagePoint& usage_point, const char* const format, ...) noexcept;

template<EEventLevel t_level, EEventType t_type, bool t_immediate = false, std::size_t t_format_size, typename... Args>
[[nodiscard]] bool submit_event(const SEventUsagePoint& usage_point, const char (&format)[t_format_size], Args&&... arguments) noexcept
{
    static_assert(is_valid_event_metadata(t_level, t_type), "The debug event level/type combination is invalid.");
    static_assert((t_format_size > 1u), "A debug event format literal must not be empty.");
    static_assert((t_format_size <= k_event_format_capacity), "A debug event format literal exceeds its transport capacity.");

    CDebugServiceState* const service = get_service();
    if (service == nullptr)
    {
        return false;
    }

    const SEventArguments encoded_arguments = encode_event_arguments(std::forward<Args>(arguments)...);
    return reporting_detail::submit_event(*service, t_level, t_type, t_immediate,
        usage_point, format, (t_format_size - 1u), encoded_arguments);
}

template<EEventLevel t_level, EEventType t_type, bool t_immediate = false, std::size_t t_format_size, typename... Args>
[[nodiscard]] bool process_event(
    const SEventUsagePoint& usage_point,
    EBreakpointOverride& breakpoint_override, const bool breakpoint_enabled_by_default,
    const EShutdownReason shutdown_reason, const char (&format)[t_format_size], Args&&... arguments) noexcept
{
    static_assert(is_valid_event_metadata(t_level, t_type), "The debug event level/type combination is invalid.");
    static_assert((t_format_size > 1u), "A debug event format literal must not be empty.");
    static_assert((t_format_size <= k_event_format_capacity), "A debug event format literal exceeds its transport capacity.");

    CDebugServiceState* const service = get_service();
    if (service == nullptr)
    {
        return false;
    }

    const SEventArguments encoded_arguments = encode_event_arguments(std::forward<Args>(arguments)...);
    return reporting_detail::process_event(*service, t_level, t_type, t_immediate,
        usage_point, breakpoint_override, breakpoint_enabled_by_default, shutdown_reason,
        nullptr, 0u, format, (t_format_size - 1u),
        encoded_arguments);
}

template<EEventLevel t_level, EEventType t_type, bool t_immediate = false, std::size_t t_expression_size, std::size_t t_format_size, typename... Args>
[[nodiscard]] bool process_condition_event(
    const SEventUsagePoint& usage_point,
    EBreakpointOverride& breakpoint_override, const bool breakpoint_enabled_by_default,
    const EShutdownReason shutdown_reason,
    const char (&expression)[t_expression_size],
    const char (&format)[t_format_size], Args&&... arguments) noexcept
{
    static_assert(is_valid_event_metadata(t_level, t_type), "The debug event level/type combination is invalid.");
    static_assert((t_type == EEventType::condition), "A condition event requires condition metadata.");
    static_assert((t_expression_size > 1u), "A condition expression literal must not be empty.");
    static_assert((t_expression_size <= k_event_expression_capacity), "A condition expression literal exceeds its transport capacity.");
    static_assert((t_format_size <= k_event_format_capacity), "A debug event format literal exceeds its transport capacity.");

    CDebugServiceState* const service = get_service();
    if (service == nullptr)
    {
        return false;
    }

    const SEventArguments encoded_arguments = encode_event_arguments(std::forward<Args>(arguments)...);
    return reporting_detail::process_event(*service, t_level, t_type, t_immediate,
        usage_point, breakpoint_override, breakpoint_enabled_by_default, shutdown_reason,
        expression, (t_expression_size - 1u), format, (t_format_size - 1u),
        encoded_arguments);
}

//  Keep immediate rich formatting on its separate service-owned buffer path.
template<EEventLevel t_level, bool t_immediate, std::size_t t_format_size, typename... Args>
[[nodiscard]] bool log_rich(const SEventUsagePoint& usage_point, const char (&format)[t_format_size], Args&&... arguments) noexcept
{
    static_assert(is_log_level(t_level), "Rich logging requires an informational, warning or error level.");
    static_assert(t_format_size > 1u, "A rich report format must not be empty.");
    if constexpr (t_immediate)
    {
        return report_immediate(t_level, usage_point, format, std::forward<Args>(arguments)...);
    }
    else
    {
        return report(t_level, usage_point, format, std::forward<Args>(arguments)...);
    }
}

}   //  namespace debug_system

#endif  //  #ifndef DEBUG_REPORTING_HPP_INCLUDED
