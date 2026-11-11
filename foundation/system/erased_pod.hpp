
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    erased_pod.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 May 2026
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Inline POD erased storage.
//
//  Provides fixed-capacity, at least 16-byte-aligned erased storage for
//  trivially copyable, standard-layout payloads identified by project type
//  ids. Payloads may be value-initialised
//  in place and accessed directly without copying.
//
//  This is a value mechanism only. Redefinition begins the lifetime of a
//  safely destructible payload in place; it does not allocate or transfer
//  ownership. The tag belongs to the carrier and is preserved when the payload
//  is redefined.
//
//  The category-bearing type-to-id binding is provided by
//  system/type_registration.hpp.

#pragma once

#ifndef ERASED_POD_HPP_INCLUDED
#define ERASED_POD_HPP_INCLUDED

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint32_t, std::uint64_t
#include <cstring>      //  std::memset
#include <new>          //  placement new, std::launder
#include <type_traits>  //  payload compatibility traits

#include "system/type_registration.hpp"

//==============================================================================
//  TErasedPod
//  Inline fixed-capacity POD erased storage
//==============================================================================

template<std::size_t PayloadSize, std::size_t PayloadAlign = 16u>
class TErasedPod
{
private:
    static_assert((PayloadSize > 0u), "TErasedPod payload size must be non-zero.");
    static_assert((PayloadAlign > 0u), "TErasedPod payload alignment must be non-zero.");
    static_assert(((PayloadAlign & (PayloadAlign - 1u)) == 0u), "TErasedPod payload alignment must be a power of two.");

    struct SHeader
    {
        ::type_id type_id{ type_ids::undefined };
        std::uint32_t tag{ 0u };
        std::uint64_t reserved{ 0u };
    };

    static_assert((sizeof(SHeader) == 16u), "TErasedPod header must occupy 16 bytes.");

public:

    static constexpr std::size_t k_payload_size = PayloadSize;
    static constexpr std::size_t k_payload_align = (PayloadAlign < 16u) ? 16u : PayloadAlign;

    //  Default lifetime
    TErasedPod() noexcept = default;
    TErasedPod(const TErasedPod&) noexcept = default;
    TErasedPod& operator=(const TErasedPod&) noexcept = default;
    ~TErasedPod() noexcept = default;

    [[nodiscard]] type_id query_type_id() const noexcept;
    [[nodiscard]] std::uint32_t query_tag() const noexcept;
    void set_tag(const std::uint32_t tag) noexcept;

    template<typename T> static constexpr bool is_compatible_with() noexcept;

    template<typename T> [[nodiscard]] bool is_a() const noexcept;

    //  Redefinition preserves the tag and reserved value, clears every byte
    //  after the header, then begins and value-initialises the POD in place.
    template<typename T> [[nodiscard]] T& redefine() noexcept;

    //  Typed pointers remain valid until the storage is redefined.
    template<typename T> [[nodiscard]] T* payload() noexcept;
    template<typename T> [[nodiscard]] const T* payload() const noexcept;

private:

    template<typename T> static constexpr void validate_payload_type() noexcept;

    SHeader m_header{};
    alignas(k_payload_align) unsigned char m_payload[PayloadSize]{};
};

//==============================================================================
//  TErasedPodFor
//  Convenience alias for storage shaped by a payload-layout type
//==============================================================================

template<typename TPayloadShape>
using TErasedPodFor = TErasedPod<sizeof(TPayloadShape), alignof(TPayloadShape)>;

//==============================================================================
//  TErasedPod out of class function bodies
//==============================================================================

template<std::size_t PayloadSize, std::size_t PayloadAlign>
type_id TErasedPod<PayloadSize, PayloadAlign>::query_type_id() const noexcept
{
    return m_header.type_id;
}

template<std::size_t PayloadSize, std::size_t PayloadAlign>
std::uint32_t TErasedPod<PayloadSize, PayloadAlign>::query_tag() const noexcept
{
    return m_header.tag;
}

template<std::size_t PayloadSize, std::size_t PayloadAlign>
void TErasedPod<PayloadSize, PayloadAlign>::set_tag(const std::uint32_t tag) noexcept
{
    m_header.tag = tag;
}

template<std::size_t PayloadSize, std::size_t PayloadAlign>
template<typename T>
constexpr bool TErasedPod<PayloadSize, PayloadAlign>::is_compatible_with() noexcept
{
    return std::is_trivially_copyable_v<T>
        && std::is_standard_layout_v<T>
        && std::is_trivially_destructible_v<T>
        && std::is_nothrow_default_constructible_v<T>
        && (sizeof(T) <= PayloadSize)
        && (alignof(T) <= k_payload_align);
}

template<std::size_t PayloadSize, std::size_t PayloadAlign>
template<typename T>
bool TErasedPod<PayloadSize, PayloadAlign>::is_a() const noexcept
{
    validate_payload_type<T>();
    return m_header.type_id == k_type_id_v<T>;
}

template<std::size_t PayloadSize, std::size_t PayloadAlign>
template<typename T>
T& TErasedPod<PayloadSize, PayloadAlign>::redefine() noexcept
{
    validate_payload_type<T>();
    unsigned char* const clear_begin =
        reinterpret_cast<unsigned char*>(this) + sizeof(m_header);
    std::memset(
        clear_begin, 0,
        sizeof(TErasedPod<PayloadSize, PayloadAlign>) - sizeof(m_header));
    T* const value = ::new (static_cast<void*>(m_payload)) T{};
    m_header.type_id = k_type_id_v<T>;
    return *value;
}

template<std::size_t PayloadSize, std::size_t PayloadAlign>
template<typename T>
T* TErasedPod<PayloadSize, PayloadAlign>::payload() noexcept
{
    validate_payload_type<T>();
    return is_a<T>() ? std::launder(reinterpret_cast<T*>(m_payload)) : nullptr;
}

template<std::size_t PayloadSize, std::size_t PayloadAlign>
template<typename T>
const T* TErasedPod<PayloadSize, PayloadAlign>::payload() const noexcept
{
    validate_payload_type<T>();
    return is_a<T>() ? std::launder(reinterpret_cast<const T*>(m_payload)) : nullptr;
}

template<std::size_t PayloadSize, std::size_t PayloadAlign>
template<typename T>
constexpr void TErasedPod<PayloadSize, PayloadAlign>::validate_payload_type() noexcept
{
    static_assert(type_ids::is_valid_id(k_type_id_v<T>), "TErasedPod requires a valid, non-zero payload type id.");
    static_assert(std::is_trivially_copyable_v<T>, "TErasedPod requires trivially copyable payloads.");
    static_assert(std::is_standard_layout_v<T>, "TErasedPod requires standard-layout payloads.");
    static_assert(std::is_trivially_destructible_v<T>, "TErasedPod requires trivially destructible payloads.");
    static_assert(std::is_nothrow_default_constructible_v<T>, "TErasedPod requires nothrow default-constructible payloads.");
    static_assert((sizeof(T) <= PayloadSize), "TErasedPod payload storage is too small.");
    static_assert((alignof(T) <= k_payload_align), "TErasedPod payload storage is under-aligned.");
}

#endif  //  #ifndef ERASED_POD_HPP_INCLUDED
