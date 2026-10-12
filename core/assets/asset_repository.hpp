
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    asset_repository.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    6 Aug 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Stable asset storage addressed by monotonically issued repository-local
//  identities. Slot indices remain private implementation details.

#pragma once

#ifndef ASSET_REPOSITORY_HPP_INCLUDED
#define ASSET_REPOSITORY_HPP_INCLUDED

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::int32_t, std::uint64_t
#include <type_traits>  //  std::is_standard_layout_v, std::is_trivially_copyable_v
#include <utility>      //  std::move

#include "containers/TOrderedCollection.hpp"
#include "system/erased_owner.hpp"

//==============================================================================
//  CAssetId
//  Strong repository-local asset identity. Zero is always invalid.
//==============================================================================

class CAssetId
{
public:
    constexpr CAssetId() noexcept = default;

    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_value != 0u; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }
    [[nodiscard]] constexpr std::uint64_t query_value() const noexcept { return m_value; }

    [[nodiscard]] constexpr std::int32_t relationship(const CAssetId& other) const noexcept;

private:
    explicit constexpr CAssetId(const std::uint64_t value) noexcept : m_value(value) {}

    std::uint64_t m_value{ 0u };

    friend class CAssetRepository;
};

//==============================================================================
//  CAssetId out of class function bodies
//==============================================================================

constexpr std::int32_t CAssetId::relationship(const CAssetId& other) const noexcept
{
    return (m_value < other.m_value) ? -1 : ((m_value > other.m_value) ? 1 : 0);
}

//==============================================================================
//  Asset identity operators
//==============================================================================

[[nodiscard]] constexpr bool operator==(const CAssetId lhs, const CAssetId rhs) noexcept
{
    return lhs.query_value() == rhs.query_value();
}

[[nodiscard]] constexpr bool operator!=(const CAssetId lhs, const CAssetId rhs) noexcept
{
    return !(lhs == rhs);
}

static_assert(std::is_trivially_copyable_v<CAssetId>, "CAssetId must be trivially copyable.");
static_assert(std::is_standard_layout_v<CAssetId>, "CAssetId must have standard layout.");
static_assert((sizeof(CAssetId) == sizeof(std::uint64_t)), "CAssetId must occupy 64 bits.");

//==============================================================================
//  Backing leases
//  Optional shared storage protected by retained records and accepted work.
//==============================================================================

//  Host-thread-only backing leases. Releasing a lease never destroys backing
//  storage; the Host finalises it after all records and operations have drained.
enum class EAssetBackingRole : std::uint8_t { none = 0, original, complete, file, operation };

struct SAssetBackingState
{
    std::uint32_t records{ 0u };
    std::uint32_t complete_views{ 0u };
    std::uint32_t operations{ 0u };
    bool open{ true };
};

class CAssetBackingLease
{
public:
    CAssetBackingLease() noexcept = default;
    CAssetBackingLease(const CAssetBackingLease&) = delete;
    CAssetBackingLease& operator=(const CAssetBackingLease&) = delete;
    CAssetBackingLease(CAssetBackingLease&& other) noexcept { *this = std::move(other); }
    CAssetBackingLease& operator=(CAssetBackingLease&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_state = std::exchange(other.m_state, nullptr);
            m_role = std::exchange(other.m_role, EAssetBackingRole::none);
        }
        return *this;
    }
    ~CAssetBackingLease() noexcept { reset(); }

    [[nodiscard]] bool acquire(SAssetBackingState& state, const EAssetBackingRole role) noexcept
    {
        if ((m_state != nullptr) || (role == EAssetBackingRole::none)) return false;
        std::uint32_t& count = (role == EAssetBackingRole::operation) ? state.operations : state.records;
        if (count == UINT32_MAX) return false;
        ++count;
        if (role == EAssetBackingRole::complete) ++state.complete_views;
        m_state = &state;
        m_role = role;
        return true;
    }
    void reset() noexcept
    {
        if (m_state != nullptr)
        {
            if (m_role == EAssetBackingRole::operation) --m_state->operations;
            else --m_state->records;
            if (m_role == EAssetBackingRole::complete) --m_state->complete_views;
        }
        m_state = nullptr;
        m_role = EAssetBackingRole::none;
    }
    [[nodiscard]] SAssetBackingState* state() const noexcept { return m_state; }
    [[nodiscard]] EAssetBackingRole role() const noexcept { return m_role; }

private:
    SAssetBackingState* m_state{ nullptr };
    EAssetBackingRole m_role{ EAssetBackingRole::none };
};

//==============================================================================
//  CAssetRecord
//  Repository-owned payload and optional backing lease.
//==============================================================================

class CAssetRecord
{
public:
    explicit CAssetRecord(CErasedOwner&& owner, CAssetBackingLease&& backing = {}) noexcept :
        m_backing(std::move(backing)), m_owner(std::move(owner)) {}

    CAssetRecord(const CAssetRecord&) = delete;
    CAssetRecord& operator=(const CAssetRecord&) = delete;
    CAssetRecord(CAssetRecord&&) noexcept = default;
    CAssetRecord& operator=(CAssetRecord&&) noexcept = default;
    ~CAssetRecord() noexcept = default;

    [[nodiscard]] bool is_ready() const noexcept { return m_owner.is_ready(); }
    [[nodiscard]] type_id query_type_id() const noexcept { return m_owner.query_type_id(); }
    [[nodiscard]] bool has_dependency(const mount_point_ids::id_type mount) const noexcept { return m_owner.has_hazard(mount); }
    [[nodiscard]] SAssetBackingState* backing() const noexcept { return m_backing.state(); }
    [[nodiscard]] EAssetBackingRole backing_role() const noexcept { return m_backing.role(); }

    template<typename T>
    [[nodiscard]] T* payload() noexcept { return m_owner.payload<T>(); }

    template<typename T>
    [[nodiscard]] const T* payload() const noexcept { return m_owner.payload<T>(); }

private:
    CAssetBackingLease m_backing; //  Release after the payload is destroyed.
    CErasedOwner m_owner;
};

//==============================================================================
//  CAssetRepository
//  Safe identity-based wrapper over ordered stable object storage.
//==============================================================================

class CAssetRepository
{
public:
    CAssetRepository() noexcept = default;
    CAssetRepository(const CAssetRepository&) = delete;
    CAssetRepository& operator=(const CAssetRepository&) = delete;
    CAssetRepository(CAssetRepository&&) = delete;
    CAssetRepository& operator=(CAssetRepository&&) = delete;
    ~CAssetRepository() noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept { return m_assets.is_valid(); }
    [[nodiscard]] bool is_empty() const noexcept { return m_assets.is_empty(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_assets.is_ready(); }

    [[nodiscard]] bool initialise(
        const std::size_t initial_slot_count = 0u,
        const std::size_t slots_per_buffer = 0u) noexcept;

    void deallocate() noexcept { m_assets.deallocate(); }

    [[nodiscard]] CAssetId insert(CErasedOwner&& owner, CAssetBackingLease backing = {}) noexcept;

    [[nodiscard]] CAssetRecord* resolve(const CAssetId id) noexcept;

    [[nodiscard]] const CAssetRecord* resolve(const CAssetId id) const noexcept;

    //  Revalidate a process-local identity stored in a live metadata document.
    [[nodiscard]] CAssetId find_identity(const std::uint64_t value) const noexcept;

    [[nodiscard]] bool erase(const CAssetId id) noexcept;

    void compact() noexcept { m_assets.sort_and_pack(); }

    [[nodiscard]] bool check_integrity() const noexcept { return m_assets.check_integrity(); }
    [[nodiscard]] bool has_dependency(const mount_point_ids::id_type mount) const noexcept;
    void erase_dependencies(const mount_point_ids::id_type mount) noexcept;

private:
    TOrderedCollection<CAssetRecord, CAssetId> m_assets;
    std::uint64_t m_next_id{ 1u };
};

//==============================================================================
//  CAssetRepository out of class function bodies
//==============================================================================

inline bool CAssetRepository::initialise(
    const std::size_t initial_slot_count,
    const std::size_t slots_per_buffer) noexcept
{
    return m_assets.initialise(initial_slot_count, slots_per_buffer);
}

inline CAssetId CAssetRepository::insert(CErasedOwner&& owner, CAssetBackingLease backing) noexcept
{
    if (!owner.is_ready() || (m_next_id == 0u))
    {
        return CAssetId{};
    }

    const CAssetId id{ m_next_id++ };
    return (m_assets.emplace(id, std::move(owner), std::move(backing)) >= 0) ? id : CAssetId{};
}

inline CAssetRecord* CAssetRepository::resolve(const CAssetId id) noexcept
{
    return id.is_valid() ? m_assets.get_object(id) : nullptr;
}

inline const CAssetRecord* CAssetRepository::resolve(const CAssetId id) const noexcept
{
    return id.is_valid() ? m_assets.get_object(id) : nullptr;
}

inline CAssetId CAssetRepository::find_identity(const std::uint64_t value) const noexcept
{
    const CAssetId id{ value };
    return (resolve(id) != nullptr) ? id : CAssetId{};
}

inline bool CAssetRepository::erase(const CAssetId id) noexcept
{
    return id.is_valid() && m_assets.erase(id);
}

inline void CAssetRepository::erase_dependencies(const mount_point_ids::id_type mount) noexcept
{
    for (std::int32_t slot = m_assets.first_live(); slot >= 0;)
    {
        const std::int32_t next = m_assets.next_live(slot);
        if (m_assets.get_object(slot)->has_dependency(mount))
        {
            (void)m_assets.erase(slot);
        }
        slot = next;
    }
}

inline bool CAssetRepository::has_dependency(const mount_point_ids::id_type mount) const noexcept
{
    for (std::int32_t slot = m_assets.first_live(); slot >= 0; slot = m_assets.next_live(slot))
    {
        if (m_assets.get_object(slot)->has_dependency(mount))
        {
            return true;
        }
    }
    return false;
}

#endif  //  #ifndef ASSET_REPOSITORY_HPP_INCLUDED
