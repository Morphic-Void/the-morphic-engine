
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    event_arguments.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    29 Jul 26
//
//  Bounded argument encoding for transported debug events.

#pragma once

#ifndef DEBUG_EVENT_ARGUMENTS_HPP_INCLUDED
#define DEBUG_EVENT_ARGUMENTS_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include "system/local_type_registry.hpp"
#include "foundation/system/system_ids.hpp"

namespace debug_system
{

constexpr std::size_t k_event_argument_count = 8u;
constexpr std::size_t k_event_argument_slot_size = 16u;
constexpr std::size_t k_inline_text_capacity = 16u;

enum class EEventArgumentType : std::uint8_t
{
    unused = 0u,
    false_value = 1u,
    true_value = 2u,
    int32 = 3u,
    uint32 = 4u,
    int64 = 5u,
    uint64 = 6u,
    float32 = 7u,
    float64 = 8u,
    inline_text = 9u,
    system_type_id = 10u,
    local_type_name = 11u,
    local_type_id_failure = 12u,
    system_id = 13u,
    module_id = 14u,
    thread_id = 15u
};

enum class EEventFormatResult : std::uint32_t
{
    success = 0u,
    invalid_descriptor,
    malformed_format,
    argument_mismatch,
    unsupported_argument,
    output_too_small
};

class CInlineText16
{
public:
    constexpr CInlineText16() noexcept = default;

    template<std::size_t t_size>
    explicit constexpr CInlineText16(const char (&text)[t_size]) noexcept;

    [[nodiscard]] constexpr const char* data() const noexcept { return m_text; }

private:
    char m_text[k_inline_text_capacity]{};
};

//==============================================================================
//  CInlineText16 out of class function bodies
//==============================================================================

template<std::size_t t_size>
constexpr CInlineText16::CInlineText16(const char (&text)[t_size]) noexcept
{
    static_assert((t_size > 0u), "CInlineText16 requires storage for a terminator.");
    static_assert((t_size <= k_inline_text_capacity), "CInlineText16 text exceeds 15 characters plus its terminator.");

    for (std::size_t index = 0u; index < (t_size - 1u); ++index)
    {
        m_text[index] = text[index];
    }
}

struct alignas(16) SEventParameterValue
{
    std::byte bytes[k_event_argument_slot_size]{};
};

struct alignas(16) SEventArguments
{
    std::uint8_t parameter_count = 0u;
    EEventArgumentType parameter_types[k_event_argument_count]{};
    alignas(16) SEventParameterValue parameters[k_event_argument_count]{};
};

static_assert((sizeof(SEventParameterValue) == 16u), "A debug event parameter must occupy exactly 16 bytes.");
static_assert((alignof(SEventParameterValue) == 16u), "Every debug event parameter must be explicitly 16-byte aligned.");
static_assert(std::is_standard_layout_v<SEventParameterValue>, "A debug event parameter must retain standard layout.");
static_assert(std::is_trivially_copyable_v<SEventParameterValue>, "A debug event parameter must remain trivially copyable.");
static_assert((sizeof(SEventArguments) == 144u), "The direct-path argument helper must retain its fixed representation.");
static_assert((alignof(SEventArguments) == 16u), "The direct-path argument helper must be explicitly 16-byte aligned.");
static_assert(std::is_standard_layout_v<SEventArguments>, "The direct-path argument helper must retain standard layout.");
static_assert(std::is_trivially_copyable_v<SEventArguments>, "The direct-path argument helper must remain trivially copyable.");

namespace event_argument_detail
{

template<typename T>
using CArgumentType = std::remove_cv_t<std::remove_reference_t<T>>;

template<typename T>
struct TArgumentTraits
{
    static constexpr bool k_supported = false;
    static constexpr EEventArgumentType k_type = EEventArgumentType::unused;
    static constexpr std::size_t k_value_size = 0u;
};

#define MV_DEFINE_EVENT_ARGUMENT_TRAITS(cpp_type, argument_type) \
    template<> \
    struct TArgumentTraits<cpp_type> \
    { \
        static constexpr bool k_supported = true; \
        static constexpr EEventArgumentType k_type = argument_type; \
        static constexpr std::size_t k_value_size = sizeof(cpp_type); \
    }

MV_DEFINE_EVENT_ARGUMENT_TRAITS(std::int32_t, EEventArgumentType::int32);
MV_DEFINE_EVENT_ARGUMENT_TRAITS(std::uint32_t, EEventArgumentType::uint32);
MV_DEFINE_EVENT_ARGUMENT_TRAITS(std::int64_t, EEventArgumentType::int64);
MV_DEFINE_EVENT_ARGUMENT_TRAITS(std::uint64_t, EEventArgumentType::uint64);
MV_DEFINE_EVENT_ARGUMENT_TRAITS(float, EEventArgumentType::float32);
MV_DEFINE_EVENT_ARGUMENT_TRAITS(double, EEventArgumentType::float64);
MV_DEFINE_EVENT_ARGUMENT_TRAITS(CInlineText16, EEventArgumentType::inline_text);
MV_DEFINE_EVENT_ARGUMENT_TRAITS(system_type_id, EEventArgumentType::system_type_id);
MV_DEFINE_EVENT_ARGUMENT_TRAITS(system_ids::id_type, EEventArgumentType::system_id);
MV_DEFINE_EVENT_ARGUMENT_TRAITS(module_ids::id_type, EEventArgumentType::module_id);
MV_DEFINE_EVENT_ARGUMENT_TRAITS(thread_ids::id_type, EEventArgumentType::thread_id);

#undef MV_DEFINE_EVENT_ARGUMENT_TRAITS

template<>
struct TArgumentTraits<bool>
{
    static constexpr bool k_supported = true;
    static constexpr EEventArgumentType k_type = EEventArgumentType::unused;
    static constexpr std::size_t k_value_size = 0u;
};

template<>
struct TArgumentTraits<local_type_id>
{
    static constexpr bool k_supported = true;
    static constexpr EEventArgumentType k_type = EEventArgumentType::unused;
    static constexpr std::size_t k_value_size = k_event_argument_slot_size;
};

template<>
struct TArgumentTraits<type_id>
{
    static constexpr bool k_supported = true;
    static constexpr EEventArgumentType k_type = EEventArgumentType::unused;
    static constexpr std::size_t k_value_size = k_event_argument_slot_size;
};

template<typename T>
inline constexpr bool k_supported = TArgumentTraits<CArgumentType<T>>::k_supported;

template<typename T>
void encode_argument(
    EEventArgumentType (&parameter_types)[k_event_argument_count],
    SEventParameterValue (&parameters)[k_event_argument_count],
    T&& argument, const std::size_t parameter_index) noexcept
{
    using CValue = CArgumentType<T>;

    if constexpr (std::is_same_v<CValue, local_type_id>)
    {
        const local_type_registry::SLocalTypeName* const name = local_type_registry::lookup_name(argument);
        if (name != nullptr)
        {
            parameter_types[parameter_index] = EEventArgumentType::local_type_name;
            std::memcpy(parameters[parameter_index].bytes, name->bytes, local_type_registry::k_short_name_capacity);
        }
        else
        {
            parameter_types[parameter_index] = EEventArgumentType::local_type_id_failure;
            const std::uint32_t raw_value = argument.raw_value();
            std::memcpy(parameters[parameter_index].bytes, &raw_value, sizeof(raw_value));
        }
    }
    else if constexpr (std::is_same_v<CValue, type_id>)
    {
        system_type_id system_id;
        local_type_id local_id;
        if (argument.try_system_type_id(system_id))
        {
            parameter_types[parameter_index] = EEventArgumentType::system_type_id;
            std::memcpy(parameters[parameter_index].bytes, &system_id, sizeof(system_id));
        }
        else if (argument.try_local_type_id(local_id))
        {
            encode_argument(parameter_types, parameters, local_id, parameter_index);
        }
        else
        {
            parameter_types[parameter_index] = EEventArgumentType::local_type_id_failure;
            const std::uint32_t raw_value = argument.raw_value();
            std::memcpy(parameters[parameter_index].bytes, &raw_value, sizeof(raw_value));
        }
    }
    else
    {
        EEventArgumentType type = TArgumentTraits<CValue>::k_type;
        if constexpr (std::is_same_v<CValue, bool>)
        {
            type = argument ? EEventArgumentType::true_value : EEventArgumentType::false_value;
        }

        parameter_types[parameter_index] = type;

        if constexpr (TArgumentTraits<CValue>::k_value_size != 0u)
        {
            if constexpr (std::is_same_v<CValue, CInlineText16>)
            {
                std::memcpy(parameters[parameter_index].bytes, argument.data(), k_inline_text_capacity);
            }
            else
            {
                const CValue value = std::forward<T>(argument);
                std::memcpy(parameters[parameter_index].bytes, &value, sizeof(value));
            }
        }
    }
}

}   //  namespace event_argument_detail

//==============================================================================
//  Event argument encoding
//==============================================================================

template<typename T>
inline constexpr bool is_supported_event_argument_v = event_argument_detail::k_supported<T>;

template<typename... Args>
void encode_event_arguments_into(
    std::uint8_t& parameter_count,
    EEventArgumentType (&parameter_types)[k_event_argument_count],
    SEventParameterValue (&parameters)[k_event_argument_count],
    Args&&... arguments) noexcept
{
    static_assert((sizeof...(Args) <= k_event_argument_count), "A debug event accepts at most eight dynamic arguments.");
    static_assert((event_argument_detail::k_supported<Args> && ...), "A debug event argument has an unsupported type.");
    static_assert(((event_argument_detail::TArgumentTraits<
        event_argument_detail::CArgumentType<Args>>::k_value_size <=
        k_event_argument_slot_size) && ...),
        "A debug event argument exceeds its fixed 16-byte slot.");
    parameter_count = static_cast<std::uint8_t>(sizeof...(Args));
    for (std::size_t index = 0u; index < k_event_argument_count; ++index)
    {
        parameter_types[index] = EEventArgumentType::unused;
        parameters[index] = {};
    }

    std::size_t parameter_index = 0u;
    (event_argument_detail::encode_argument(parameter_types, parameters, std::forward<Args>(arguments), parameter_index++), ...);
}

template<typename... Args>
void encode_event_arguments_into(SEventArguments& result, Args&&... arguments) noexcept
{
    result = {};
    encode_event_arguments_into(
        result.parameter_count, result.parameter_types, result.parameters,
        std::forward<Args>(arguments)...);
}

template<typename... Args>
[[nodiscard]] SEventArguments encode_event_arguments(Args&&... arguments) noexcept
{
    SEventArguments result{};
    encode_event_arguments_into(result, std::forward<Args>(arguments)...);
    return result;
}

[[nodiscard]] EEventFormatResult format_event_text(
    char* const destination, const std::size_t destination_capacity,
    const char* const format, const std::size_t format_size,
    const std::uint8_t parameter_count,
    const EEventArgumentType (&parameter_types)[k_event_argument_count],
    const SEventParameterValue (&parameters)[k_event_argument_count],
    std::size_t& out_size) noexcept;

[[nodiscard]] inline EEventFormatResult format_event_text(
    char* const destination, const std::size_t destination_capacity,
    const char* const format, const std::size_t format_size,
    const SEventArguments& arguments, std::size_t& out_size) noexcept
{
    return format_event_text(
        destination, destination_capacity, format, format_size,
        arguments.parameter_count, arguments.parameter_types,
        arguments.parameters, out_size);
}

}   //  namespace debug_system

#endif  //  #ifndef DEBUG_EVENT_ARGUMENTS_HPP_INCLUDED
