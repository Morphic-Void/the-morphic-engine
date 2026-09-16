
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    erased_owner.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Jul 26
//
//  Component-local operation dispatch and lifetime implementation for
//  CErasedOwner.

#include <utility>      //  std::move

#include "system/erased_owner.hpp"
#include "system/local_type_registry.hpp"
#include "system/system_id_registry.hpp"

CErasedOwner::CErasedOwner(CErasedOwner&& other) noexcept
    : m_storage(std::move(other.m_storage))
    , m_type_id(other.m_type_id)
    , m_hazards(other.m_hazards)
{
    other.make_canonical_empty();
}

CErasedOwner& CErasedOwner::operator=(CErasedOwner&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_storage = std::move(other.m_storage);
        m_type_id = other.m_type_id;
        m_hazards = other.m_hazards;
        other.make_canonical_empty();
    }
    return *this;
}

CErasedOwner::~CErasedOwner() noexcept
{
    destroy();
}

void CErasedOwner::destroy() noexcept
{
    if (!is_empty())
    {
        void* const payload = m_storage.data();
        const erased_owner_operations::SRegistration* const registration =
            operations();
        const bool can_destroy =
            (payload != nullptr) && (registration != nullptr) &&
            (!m_type_id.is_local() || memory_context_belongs_to_current_component(m_storage.context()));
        MV_CRITICAL_ASSERT(can_destroy);
        if (can_destroy)
        {
            registration->operations.destroy(payload);
        }
    }

    m_storage.deallocate();
    make_canonical_empty();
}

void CErasedOwner::add_hazard(const mount_point_ids::id_type mount_point_id) noexcept
{
    m_hazards |= hazard_bit(mount_point_id);
}

void CErasedOwner::remove_hazard(const mount_point_ids::id_type mount_point_id) noexcept
{
    m_hazards &= ~hazard_bit(mount_point_id);
}

bool CErasedOwner::has_hazard(const mount_point_ids::id_type mount_point_id) const noexcept
{
    const std::uint32_t bit = hazard_bit(mount_point_id);
    return (bit != 0u) && ((m_hazards & bit) != 0u);
}

bool CErasedOwner::can_reattribute_to(memory::CMemoryContext* const target) const noexcept
{
    memory::CMemoryContext* const resolved_target = (target != nullptr) ? target : memory::get_ambient_memory_context();
    if (resolved_target == nullptr)
    {
        return false;
    }
    if (is_empty())
    {
        return true;
    }

    memory::CMemoryContext* source = nullptr;
    const erased_owner_operations::SRegistration* const registration =
        operations();
    return is_ready() && memory_source_context(source) &&
        (registration != nullptr) &&
        (!m_type_id.is_local() ||
            (memory_context_belongs_to_current_component(source) &&
                memory_context_belongs_to_current_component(resolved_target))) &&
        m_storage.can_reattribute_to(resolved_target) &&
        registration->operations.can_reattribute_to(m_storage.data(), resolved_target);
}

bool CErasedOwner::reattribute(memory::CMemoryContext* const target) noexcept
{
    memory::CMemoryContext* const resolved_target = (target != nullptr) ? target : memory::get_ambient_memory_context();
    if ((resolved_target == nullptr) || !can_reattribute_to(resolved_target))
    {
        return false;
    }
    if (is_empty())
    {
        return true;
    }

    memory::CMemoryContext* source = nullptr;
    if (!memory_source_context(source))
    {
        return false;
    }

    const erased_owner_operations::SRegistration* const registration = operations();
    MV_CRITICAL_ASSERT(registration != nullptr);
    if (registration == nullptr)
    {
        return false;
    }

    if ((source != nullptr) && (source != resolved_target) &&
        !memory::reattribute(*source, *resolved_target, memory_allocation_count(), memory_allocation_size()))
    {
        return false;
    }

    m_storage.unsafe_replace_context_without_accounting(source, resolved_target);
    registration->operations.replace_memory_context(m_storage.data(), source, resolved_target);
    return true;
}

std::uint32_t CErasedOwner::hazard_bit(const mount_point_ids::id_type mount_point_id) noexcept
{
    const mount_point_ids::index_type index = mount_point_ids::ops::decode_id(mount_point_id);
    const bool valid = mount_point_ids::ops::is_valid_index(index) && (index.raw_value() < mount_point_ids::k_count);
    MV_ASSERT(valid);
    return valid ? (std::uint32_t{ 1u } << static_cast<std::uint32_t>(index.raw_value())) : 0u;
}

bool CErasedOwner::memory_source_context(memory::CMemoryContext*& source) const noexcept
{
    source = m_storage.owns_storage() ? m_storage.context() : nullptr;
    if (!is_ready() || (source == nullptr))
    {
        return false;
    }

    const erased_owner_operations::SRegistration* const registration = operations();
    MV_CRITICAL_ASSERT(registration != nullptr);
    return (registration != nullptr) && registration->operations.validate_memory_source(m_storage.data(), source);
}

std::uint32_t CErasedOwner::memory_allocation_count() const noexcept
{
    if (is_empty())
    {
        return 0u;
    }
    const erased_owner_operations::SRegistration* const registration = operations();
    MV_CRITICAL_ASSERT(registration != nullptr);
    return (registration != nullptr)
        ? memory::add_accounting_counts(m_storage.memory_allocation_count(), registration->operations.memory_allocation_count(m_storage.data()))
        : 0u;
}

std::uint64_t CErasedOwner::memory_allocation_size() const noexcept
{
    if (is_empty())
    {
        return 0u;
    }
    const erased_owner_operations::SRegistration* const registration = operations();
    MV_CRITICAL_ASSERT(registration != nullptr);
    return (registration != nullptr)
        ? memory::add_accounting_bytes(m_storage.memory_allocation_size(), registration->operations.memory_allocation_size(m_storage.data()))
        : 0u;
}

const erased_owner_operations::SRegistration* CErasedOwner::operations() const noexcept
{
    return erased_owner_operations::find(m_type_id);
}

bool CErasedOwner::identity_is_registered(const type_id identity) noexcept
{
    system_type_id system_identity;
    if (identity.try_system_type_id(system_identity))
    {
        return system_id_registry::find_type(system_identity) != nullptr;
    }

    local_type_id local_identity;
    return identity.try_local_type_id(local_identity) &&
        (local_type_registry::find_type(local_identity) != nullptr);
}

bool CErasedOwner::memory_context_belongs_to_current_component(memory::CMemoryContext* const context) noexcept
{
    const module_ids::id_type ambient_module_id = system_context::get_ambient_module_id();
    return (context != nullptr) && context->is_usable() &&
        module_ids::ops::is_valid_id(ambient_module_id) &&
        system_ids::ops::is_valid_id(context->get_system_id()) &&
        (system_ids::ops::get_module_id(context->get_system_id()) == ambient_module_id);
}

bool CErasedOwner::validate_no_nested_memory_source(const void* const payload, memory::CMemoryContext* const source) noexcept
{
    return (payload != nullptr) && (source != nullptr);
}

std::uint32_t CErasedOwner::no_nested_memory_allocation_count(const void* const payload) noexcept
{
    MV_ASSERT(payload != nullptr);
    return 0u;
}

std::uint64_t CErasedOwner::no_nested_memory_allocation_size(const void* const payload) noexcept
{
    MV_ASSERT(payload != nullptr);
    return 0u;
}

bool CErasedOwner::no_nested_can_reattribute_to(const void* const payload, memory::CMemoryContext* const target) noexcept
{
    return (payload != nullptr) && (target != nullptr);
}

void CErasedOwner::replace_no_nested_memory_context(
    void* const payload,
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    MV_ASSERT((payload != nullptr) && (expected_source != nullptr) && (target != nullptr));
}

void CErasedOwner::make_canonical_empty() noexcept
{
    m_storage = memory::CMemoryToken{};
    m_type_id = type_ids::undefined;
    m_hazards = 0u;
}
