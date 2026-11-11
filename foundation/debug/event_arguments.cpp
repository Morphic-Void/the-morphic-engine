
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    event_arguments.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    29 Jul 26
//
//  Bounded argument decoding and formatting for transported debug events.

#include "debug/event_arguments.hpp"

#include "system/system_id_registry.hpp"

#include <cstdio>
#include <cstring>

namespace debug_system
{

namespace event_formatting
{

struct SOutput
{
    char* destination;
    std::size_t capacity;
    std::size_t size;
};

[[nodiscard]] std::size_t value_size(const EEventArgumentType type) noexcept
{
    switch (type)
    {
        case EEventArgumentType::false_value:
        case EEventArgumentType::true_value:
        {
            return 0u;
        }
        case EEventArgumentType::int32:
        case EEventArgumentType::uint32:
        case EEventArgumentType::float32:
        {
            return 4u;
        }
        case EEventArgumentType::int64:
        case EEventArgumentType::uint64:
        case EEventArgumentType::float64:
        {
            return 8u;
        }
        case EEventArgumentType::inline_text:
        {
            return k_inline_text_capacity;
        }
        case EEventArgumentType::system_type_id:
        {
            return sizeof(system_type_id);
        }
        case EEventArgumentType::local_type_name:
        {
            return local_type_registry::k_short_name_capacity;
        }
        case EEventArgumentType::local_type_id_failure:
        {
            return sizeof(std::uint32_t);
        }
        case EEventArgumentType::system_id:
        {
            return sizeof(system_ids::id_type);
        }
        case EEventArgumentType::module_id:
        {
            return sizeof(module_ids::id_type);
        }
        case EEventArgumentType::thread_id:
        {
            return sizeof(thread_ids::id_type);
        }
        default:
        {
            return k_event_argument_slot_size + 1u;
        }
    }
}

[[nodiscard]] EEventFormatResult append(SOutput& output, const char* const text, const std::size_t size) noexcept
{
    if ((size >= output.capacity) || (output.size > (output.capacity - size - 1u)))
    {
        return EEventFormatResult::output_too_small;
    }

    std::memcpy(output.destination + output.size, text, size);
    output.size += size;
    output.destination[output.size] = 0;
    return EEventFormatResult::success;
}

template<typename T>
[[nodiscard]] T read_value(const SEventParameterValue (&parameters)[k_event_argument_count], const std::size_t parameter_index) noexcept
{
    T value{};
    std::memcpy(&value, parameters[parameter_index].bytes, sizeof(value));
    return value;
}

[[nodiscard]] EEventFormatResult append_integer_hex(SOutput& output, const std::uint64_t value, const char insertion_specifier) noexcept
{
    const char* format = nullptr;
    switch (insertion_specifier)
    {
        case '#':
        {
            format = "#%llx";
            break;
        }
        case 'x':
        {
            format = "x%llx";
            break;
        }
        case 'X':
        {
            format = "x%llX";
            break;
        }
        default:
        {
            return EEventFormatResult::invalid_descriptor;
        }
    }

    char text[64]{};
    const int size = std::snprintf(text, sizeof(text), format, static_cast<unsigned long long>(value));

    return append(output, text, static_cast<std::size_t>(size));
}

[[nodiscard]] EEventFormatResult append_hex_argument(
    SOutput& output,
    const SEventParameterValue (&parameters)[k_event_argument_count],
    const EEventArgumentType type,
    const std::size_t parameter_index,
    const char insertion_specifier) noexcept
{
    switch (type)
    {
        case EEventArgumentType::int32:
        case EEventArgumentType::uint32:
        {
            return append_integer_hex(
                output,
                read_value<std::uint32_t>(parameters, parameter_index),
                insertion_specifier);
        }
        case EEventArgumentType::int64:
        case EEventArgumentType::uint64:
        {
            return append_integer_hex(
                output,
                read_value<std::uint64_t>(parameters, parameter_index),
                insertion_specifier);
        }
        case EEventArgumentType::false_value:
        case EEventArgumentType::true_value:
        case EEventArgumentType::float32:
        case EEventArgumentType::float64:
        case EEventArgumentType::inline_text:
        case EEventArgumentType::system_type_id:
        case EEventArgumentType::local_type_name:
        case EEventArgumentType::local_type_id_failure:
        case EEventArgumentType::system_id:
        case EEventArgumentType::module_id:
        case EEventArgumentType::thread_id:
        {
            return EEventFormatResult::unsupported_argument;
        }
        default:
        {
            return EEventFormatResult::invalid_descriptor;
        }
    }
}

[[nodiscard]] EEventFormatResult append_type_id(SOutput& output, const system_type_id id) noexcept
{
    const system_id_registry::STypeRegistration* const registration = system_id_registry::find_type(id);
    if (registration != nullptr)
    {
        return append(output, registration->name, registration->name_size);
    }

    char text[64]{};
    const bool valid = system_type_ids::ops::is_valid_id(id);
    const int size = std::snprintf(text, sizeof(text),
        valid ? "unregistered-type:0x%08x" : "invalid-type:0x%08x",
        static_cast<unsigned int>(id.raw_value()));

    return append(output, text, static_cast<std::size_t>(size));
}

[[nodiscard]] EEventFormatResult append_local_type_failure(SOutput& output, const std::uint32_t raw_value) noexcept
{
    char text[64]{};
    const bool valid = local_type_ids::ops::is_valid_id(local_type_id{ raw_value });
    const int size = std::snprintf(text, sizeof(text),
        valid ? "unregistered-local-type:0x%08x" : "invalid-local-type:0x%08x",
        static_cast<unsigned int>(raw_value));

    return append(output, text, static_cast<std::size_t>(size));
}

[[nodiscard]] EEventFormatResult append_system_id(
    SOutput& output, const system_ids::id_type id) noexcept
{
    char text[64]{};
    std::size_t name_size = 0u;
    if (system_id_registry::format_system_name(id, text, sizeof(text), name_size))
    {
        return append(output, text, name_size);
    }

    const bool valid = system_ids::ops::is_valid_id(id);
    const int size = std::snprintf(text, sizeof(text),
        valid ? "unregistered-system:0x%016llx" : "invalid-system:0x%016llx",
        static_cast<unsigned long long>(id.raw_value()));
    return append(output, text, static_cast<std::size_t>(size));
}

[[nodiscard]] EEventFormatResult append_module_id(SOutput& output, const module_ids::id_type id) noexcept
{
    const char* const name = system_id_registry::lookup_module_name(id);
    if (name != nullptr)
    {
        return append(output, name, std::strlen(name));
    }

    char text[64]{};
    const bool valid = module_ids::ops::is_valid_id(id);
    const int size = std::snprintf(text, sizeof(text),
        valid ? "unregistered-module:0x%016llx" : "invalid-module:0x%016llx",
        static_cast<unsigned long long>(id.raw_value()));
    return append(output, text, static_cast<std::size_t>(size));
}

[[nodiscard]] EEventFormatResult append_thread_id(
    SOutput& output, const thread_ids::id_type id) noexcept
{
    const char* const name = system_id_registry::lookup_thread_name(id);
    if (name != nullptr)
    {
        return append(output, name, std::strlen(name));
    }

    char text[64]{};
    const bool valid = thread_ids::ops::is_valid_id(id);
    const int size = std::snprintf(text, sizeof(text),
        valid ? "unregistered-thread:0x%016llx" : "invalid-thread:0x%016llx",
        static_cast<unsigned long long>(id.raw_value()));
    return append(output, text, static_cast<std::size_t>(size));
}

[[nodiscard]] EEventFormatResult append_argument(
    SOutput& output,
    const SEventParameterValue (&parameters)[k_event_argument_count],
    const EEventArgumentType type,
    const std::size_t parameter_index) noexcept
{
    char text[64]{};
    int size = 0;

    switch (type)
    {
        case EEventArgumentType::false_value:
        {
            return append(output, "false", 5u);
        }
        case EEventArgumentType::true_value:
        {
            return append(output, "true", 4u);
        }
        case EEventArgumentType::int32:
        {
            size = std::snprintf(text, sizeof(text), "%d", read_value<std::int32_t>(parameters, parameter_index));
            break;
        }
        case EEventArgumentType::uint32:
        {
            size = std::snprintf(text, sizeof(text), "%u", read_value<std::uint32_t>(parameters, parameter_index));
            break;
        }
        case EEventArgumentType::int64:
        {
            size = std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(read_value<std::int64_t>(parameters, parameter_index)));
            break;
        }
        case EEventArgumentType::uint64:
        {
            size = std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(read_value<std::uint64_t>(parameters, parameter_index)));
            break;
        }
        case EEventArgumentType::float32:
        {
            size = std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(read_value<float>(parameters, parameter_index)));
            break;
        }
        case EEventArgumentType::float64:
        {
            size = std::snprintf(text, sizeof(text), "%.17g", read_value<double>(parameters, parameter_index));
            break;
        }
        case EEventArgumentType::inline_text:
        {
            const char* const value = reinterpret_cast<const char*>(parameters[parameter_index].bytes);
            const void* const terminator = std::memchr(value, 0, k_inline_text_capacity);
            if (terminator == nullptr)
            {
                return EEventFormatResult::invalid_descriptor;
            }

            const std::size_t value_size = static_cast<const char*>(terminator) - value;
            return append(output, value, value_size);
        }
        case EEventArgumentType::system_type_id:
        {
            return append_type_id(output, read_value<system_type_id>(parameters, parameter_index));
        }
        case EEventArgumentType::local_type_name:
        {
            const char* const value = reinterpret_cast<const char*>(parameters[parameter_index].bytes);
            const void* const terminator = std::memchr(
                value, 0, local_type_registry::k_short_name_capacity);
            if (terminator == nullptr)
            {
                return EEventFormatResult::invalid_descriptor;
            }

            const std::size_t name_size = static_cast<const char*>(terminator) - value;
            return append(output, value, name_size);
        }
        case EEventArgumentType::local_type_id_failure:
        {
            return append_local_type_failure(output, read_value<std::uint32_t>(parameters, parameter_index));
        }
        case EEventArgumentType::system_id:
        {
            return append_system_id(output, read_value<system_ids::id_type>(parameters, parameter_index));
        }
        case EEventArgumentType::module_id:
        {
            return append_module_id(output, read_value<module_ids::id_type>(parameters, parameter_index));
        }
        case EEventArgumentType::thread_id:
        {
            return append_thread_id(output, read_value<thread_ids::id_type>(parameters, parameter_index));
        }
        default:
        {
            return EEventFormatResult::invalid_descriptor;
        }
    }

    return append(output, text, static_cast<std::size_t>(size));
}

[[nodiscard]] bool bytes_are_zero(const std::byte* const bytes, const std::size_t begin) noexcept
{
    for (std::size_t index = begin; index < k_event_argument_slot_size; ++index)
    {
        if (bytes[index] != std::byte{})
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] EEventFormatResult validate(
    const std::uint8_t parameter_count,
    const EEventArgumentType (&parameter_types)[k_event_argument_count],
    const SEventParameterValue (&parameters)[k_event_argument_count]) noexcept
{
    if (parameter_count > k_event_argument_count)
    {
        return EEventFormatResult::invalid_descriptor;
    }

    for (std::size_t index = 0u; index < parameter_count; ++index)
    {
        const EEventArgumentType type = parameter_types[index];
        const std::size_t size = value_size(type);
        if ((size > k_event_argument_slot_size) || !bytes_are_zero(parameters[index].bytes, size))
        {
            return EEventFormatResult::invalid_descriptor;
        }

        if ((type == EEventArgumentType::inline_text) || (type == EEventArgumentType::local_type_name))
        {
            const void* const terminator = std::memchr(parameters[index].bytes, 0, k_inline_text_capacity);
            if (terminator == nullptr)
            {
                return EEventFormatResult::invalid_descriptor;
            }

            const std::size_t terminator_index = static_cast<const std::byte*>(terminator) - parameters[index].bytes;
            if (((type == EEventArgumentType::local_type_name) && (terminator_index == 0u)) ||
                !bytes_are_zero(parameters[index].bytes, terminator_index + 1u))
            {
                return EEventFormatResult::invalid_descriptor;
            }
        }
    }

    for (std::size_t index = parameter_count; index < k_event_argument_count; ++index)
    {
        if ((parameter_types[index] != EEventArgumentType::unused) || !bytes_are_zero(parameters[index].bytes, 0u))
        {
            return EEventFormatResult::invalid_descriptor;
        }
    }

    return EEventFormatResult::success;
}

}   //  namespace event_formatting

EEventFormatResult format_event_text(
    char* const destination, const std::size_t destination_capacity,
    const char* const format, const std::size_t format_size,
    const std::uint8_t parameter_count,
    const EEventArgumentType (&parameter_types)[k_event_argument_count],
    const SEventParameterValue (&parameters)[k_event_argument_count],
    std::size_t& out_size) noexcept
{
    out_size = 0u;
    if ((destination == nullptr) || (destination_capacity == 0u) || (format == nullptr))
    {
        return EEventFormatResult::invalid_descriptor;
    }

    destination[0] = 0;
    const EEventFormatResult validation = event_formatting::validate(parameter_count, parameter_types, parameters);
    if (validation != EEventFormatResult::success)
    {
        return validation;
    }

    event_formatting::SOutput output{ destination, destination_capacity, 0u };
    std::size_t argument_index = 0u;
    std::size_t literal_begin = 0u;
    EEventFormatResult result;

    for (std::size_t index = 0u; index < format_size; ++index)
    {
        const char token = format[index];
        if (token == 0)
        {
            return EEventFormatResult::malformed_format;
        }

        if ((token != '{') && (token != '}'))
        {
            continue;
        }

        if ((index + 1u) >= format_size)
        {
            return EEventFormatResult::malformed_format;
        }

        const char next = format[index + 1u];
        if ((token == '{') && (next == '}'))
        {
            std::size_t insertion_begin = index;
            if (index > literal_begin)
            {
                const char preceding = format[index - 1u];
                if ((preceding == '#') || (preceding == 'x') || (preceding == 'X'))
                {
                    --insertion_begin;
                }
            }

            result = event_formatting::append(output, format + literal_begin, insertion_begin - literal_begin);
            if (result != EEventFormatResult::success)
            {
                return result;
            }

            if (argument_index >= parameter_count)
            {
                return EEventFormatResult::argument_mismatch;
            }

            const EEventArgumentType type = parameter_types[argument_index];
            if (insertion_begin < index)
            {
                result = event_formatting::append_hex_argument(output, parameters, type, argument_index, format[insertion_begin]);
            }
            else
            {
                result = event_formatting::append_argument(output, parameters, type, argument_index);
            }

            if (result != EEventFormatResult::success)
            {
                return result;
            }

            ++argument_index;
            ++index;
            literal_begin = index + 1u;
            continue;
        }

        if (next != token)
        {
            return EEventFormatResult::malformed_format;
        }

        result = event_formatting::append(output, format + literal_begin, index - literal_begin);
        if (result != EEventFormatResult::success)
        {
            return result;
        }

        result = event_formatting::append(output, &token, 1u);
        if (result != EEventFormatResult::success)
        {
            return result;
        }

        ++index;
        literal_begin = index + 1u;
    }

    result = event_formatting::append(output, format + literal_begin, format_size - literal_begin);
    if (result == EEventFormatResult::success)
    {
        if (argument_index == parameter_count)
        {
            out_size = output.size;
        }
        else
        {
            result = EEventFormatResult::argument_mismatch;
        }
    }

    return result;
}

}   //  namespace debug_system
