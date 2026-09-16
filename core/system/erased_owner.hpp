
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    erased_owner.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Jul 26
//
//  Move-only ownership for one explicitly eligible, registered type-erased
//  payload. SYSTEM payloads may cross component boundaries; LOCAL payloads
//  remain within the component whose operation authority defines them.

#pragma once

#ifndef ERASED_OWNER_HPP_INCLUDED
#define ERASED_OWNER_HPP_INCLUDED

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint32_t
#include <new>          //  ::new
#include <type_traits>  //  std::is_nothrow_*_v
#include <utility>      //  std::move

#include "debug/macros.hpp"
#include "memory/memory_token.hpp"
#include "system/erased_owner_operations.hpp"
#include "system/erased_owner_registration.hpp"
#include "system/type_registration.hpp"

class CErasedOwner
{
public:
    CErasedOwner() noexcept = default;
    CErasedOwner(const CErasedOwner&) = delete;
    CErasedOwner& operator=(const CErasedOwner&) = delete;
    CErasedOwner(CErasedOwner&& other) noexcept;
    CErasedOwner& operator=(CErasedOwner&& other) noexcept;
    ~CErasedOwner() noexcept;

    [[nodiscard]] bool is_empty() const noexcept { return !m_type_id.is_valid(); }
    [[nodiscard]] bool is_ready() const noexcept { return !is_empty() && (m_storage.data() != nullptr); }
    [[nodiscard]] explicit operator bool() const noexcept { return is_ready(); }

    [[nodiscard]] type_id query_type_id() const noexcept { return m_type_id; }

    template<typename T>
    [[nodiscard]] static CErasedOwner create(memory::CMemoryContext* const context = nullptr) noexcept;

    template<typename T>
    [[nodiscard]] T* payload() noexcept;

    template<typename T>
    [[nodiscard]] const T* payload() const noexcept;

    void destroy() noexcept;

    void add_hazard(const mount_point_ids::id_type mount_point_id) noexcept;
    void remove_hazard(const mount_point_ids::id_type mount_point_id) noexcept;
    [[nodiscard]] bool has_hazard(const mount_point_ids::id_type mount_point_id) const noexcept;
    [[nodiscard]] bool has_any_hazard() const noexcept { return m_hazards != 0u; }
    [[nodiscard]] std::uint32_t hazard_mask() const noexcept { return m_hazards; }

    [[nodiscard]] memory::CMemoryContext* memory_context() const noexcept { return m_storage.context(); }
    [[nodiscard]] bool can_reattribute_to(memory::CMemoryContext* const context = nullptr) const noexcept;
    [[nodiscard]] bool reattribute(memory::CMemoryContext* const context = nullptr) noexcept;

private:
    template<typename T>
    friend struct erased_owner_operations::TDefaultOperationsFactory;

    template<typename T, auto Member>
    friend struct erased_owner_operations::TNestedOperationsFactory;

    [[nodiscard]] static std::uint32_t hazard_bit(const mount_point_ids::id_type mount_point_id) noexcept;
    [[nodiscard]] bool memory_source_context(memory::CMemoryContext*& source) const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
    [[nodiscard]] const erased_owner_operations::SRegistration* operations() const noexcept;
    [[nodiscard]] static bool identity_is_registered(const type_id identity) noexcept;
    [[nodiscard]] static bool memory_context_belongs_to_current_component(memory::CMemoryContext* const context) noexcept;
    void make_canonical_empty() noexcept;

    template<typename T>
    static void destroy_payload(void* const payload) noexcept;

    [[nodiscard]] static bool validate_no_nested_memory_source(const void* const payload, memory::CMemoryContext* const source) noexcept;
    [[nodiscard]] static std::uint32_t no_nested_memory_allocation_count(const void* const payload) noexcept;
    [[nodiscard]] static std::uint64_t no_nested_memory_allocation_size(const void* const payload) noexcept;
    [[nodiscard]] static bool no_nested_can_reattribute_to(const void* const payload, memory::CMemoryContext* const target) noexcept;
    static void replace_no_nested_memory_context(void* const payload, memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

    template<typename T, auto Member>
    [[nodiscard]] static bool validate_nested_memory_source(const void* const payload, memory::CMemoryContext* const source) noexcept;

    template<typename T, auto Member>
    [[nodiscard]] static std::uint32_t nested_memory_allocation_count(const void* const payload) noexcept;

    template<typename T, auto Member>
    [[nodiscard]] static std::uint64_t nested_memory_allocation_size(const void* const payload) noexcept;

    template<typename T, auto Member>
    [[nodiscard]] static bool nested_can_reattribute_to(const void* const payload, memory::CMemoryContext* const target) noexcept;

    template<typename T, auto Member>
    static void replace_nested_memory_context(void* const payload, memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

    memory::CMemoryToken m_storage;
    type_id              m_type_id{ type_ids::undefined };
    std::uint32_t        m_hazards{ 0u };
};

static_assert((mount_point_ids::k_count <= 32u), "CErasedOwner hazard storage cannot represent all registered mounting points");
static_assert(((sizeof(void*) != 8u) || (sizeof(CErasedOwner) == 32u)), "CErasedOwner must occupy 32 bytes on a 64-bit target");
static_assert(((sizeof(void*) != 4u) || (sizeof(CErasedOwner) == 24u)), "CErasedOwner must occupy 24 bytes on a 32-bit target");

//==============================================================================
//  CErasedOwner typed payload out of class function bodies
//==============================================================================

template<typename T>
inline CErasedOwner CErasedOwner::create(memory::CMemoryContext* const context) noexcept
{
    static_assert(k_is_erased_owner_payload_v<T>, "CErasedOwner may only create explicitly registered erased-owner payloads.");
    static_assert(k_type_id_v<T>.is_valid(), "CErasedOwner payload registration must provide a valid type id.");
    static_assert(std::is_nothrow_default_constructible_v<T>, "CErasedOwner payloads must be nothrow default constructible.");
    static_assert(std::is_nothrow_move_constructible_v<T>, "CErasedOwner payloads must be nothrow move constructible.");
    static_assert(std::is_nothrow_move_assignable_v<T>, "CErasedOwner payloads must be nothrow move assignable.");
    static_assert(std::is_nothrow_destructible_v<T>, "CErasedOwner payloads must be nothrow destructible.");
    static_assert((sizeof(T) <= 0xffffu), "CErasedOwner payload size exceeds the memory-token stride field.");

    CErasedOwner owner;
    constexpr type_id identity = k_type_id_v<T>;
    const bool authority_available = identity_is_registered(identity) && (erased_owner_operations::find(identity) != nullptr);
    MV_CRITICAL_ASSERT(authority_available);
    if (!authority_available)
    {
        return owner;
    }

    memory::CMemoryContext* const resolved_context = (context != nullptr) ? context : memory::get_ambient_memory_context();
    if (identity.is_local() && !memory_context_belongs_to_current_component(resolved_context))
    {
        return owner;
    }

    memory::CMemoryToken storage{ sizeof(T), alignof(T), resolved_context };
    const bool allocated = storage.allocate(1u, false);
    MV_ASSERT(allocated);
    if (allocated)
    {
        ::new (storage.data()) T();
        owner.m_storage = std::move(storage);
        owner.m_type_id = identity;
    }
    return owner;
}

template<typename T>
inline T* CErasedOwner::payload() noexcept
{
    static_assert(k_is_erased_owner_payload_v<T>, "CErasedOwner may only access explicitly registered erased-owner payloads.");
    return (m_type_id == k_type_id_v<T>) ? static_cast<T*>(m_storage.data()) : nullptr;
}

template<typename T>
inline const T* CErasedOwner::payload() const noexcept
{
    static_assert(k_is_erased_owner_payload_v<T>, "CErasedOwner may only access explicitly registered erased-owner payloads.");
    return (m_type_id == k_type_id_v<T>) ? static_cast<const T*>(m_storage.data()) : nullptr;
}

template<typename T>
inline void CErasedOwner::destroy_payload(void* const payload) noexcept
{
    static_cast<T*>(payload)->~T();
}

template<typename T, auto Member>
inline bool CErasedOwner::validate_nested_memory_source(const void* const payload, memory::CMemoryContext* const source) noexcept
{
    const auto& storage = static_cast<const T*>(payload)->*Member;
    const memory::SMemoryAttribution attribution = storage.memory_attribution();
    return (attribution.source_state == memory::EMemorySourceState::empty) ||
        ((attribution.source_state == memory::EMemorySourceState::coherent) && (attribution.source == source));
}

template<typename T, auto Member>
inline std::uint32_t CErasedOwner::nested_memory_allocation_count(const void* const payload) noexcept
{
    const auto& storage = static_cast<const T*>(payload)->*Member;
    return storage.memory_attribution().allocation_count;
}

template<typename T, auto Member>
inline std::uint64_t CErasedOwner::nested_memory_allocation_size(const void* const payload) noexcept
{
    const auto& storage = static_cast<const T*>(payload)->*Member;
    return storage.memory_attribution().allocation_size;
}

template<typename T, auto Member>
inline bool CErasedOwner::nested_can_reattribute_to(const void* const payload, memory::CMemoryContext* const target) noexcept
{
    const auto& storage = static_cast<const T*>(payload)->*Member;
    return memory::can_reattribute_to(storage, target);
}

template<typename T, auto Member>
inline void CErasedOwner::replace_nested_memory_context(void* const payload, memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
{
    auto& storage = static_cast<T*>(payload)->*Member;
    storage.unsafe_replace_memory_context_without_accounting(expected_source, target);
}

namespace erased_owner_operations
{

template<typename T>
struct TDefaultOperationsFactory
{
    [[nodiscard]] static constexpr SOperations make() noexcept;
};

template<typename T, auto Member>
struct TNestedOperationsFactory
{
    [[nodiscard]] static constexpr SOperations make() noexcept;
};

//==============================================================================
//  Erased owner operation factories out of class function bodies
//==============================================================================

template<typename T>
constexpr SOperations TDefaultOperationsFactory<T>::make() noexcept
{
    return SOperations{
        &CErasedOwner::destroy_payload<T>,
        &CErasedOwner::validate_no_nested_memory_source,
        &CErasedOwner::no_nested_memory_allocation_count,
        &CErasedOwner::no_nested_memory_allocation_size,
        &CErasedOwner::no_nested_can_reattribute_to,
        &CErasedOwner::replace_no_nested_memory_context
    };
}

template<typename T, auto Member>
constexpr SOperations TNestedOperationsFactory<T, Member>::make() noexcept
{
    return SOperations{
        &CErasedOwner::destroy_payload<T>,
        &CErasedOwner::validate_nested_memory_source<T, Member>,
        &CErasedOwner::nested_memory_allocation_count<T, Member>,
        &CErasedOwner::nested_memory_allocation_size<T, Member>,
        &CErasedOwner::nested_can_reattribute_to<T, Member>,
        &CErasedOwner::replace_nested_memory_context<T, Member>
    };
}

}   //  namespace erased_owner_operations

#endif  //  #ifndef ERASED_OWNER_HPP_INCLUDED
