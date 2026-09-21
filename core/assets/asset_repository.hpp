
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
//  CAssetRecord
//  Repository-owned asset and future asset-lifecycle metadata boundary.
//==============================================================================

class CAssetRecord
{
public:
    explicit CAssetRecord(CErasedOwner&& owner) noexcept : m_owner(std::move(owner)) {}

    CAssetRecord(const CAssetRecord&) = delete;
    CAssetRecord& operator=(const CAssetRecord&) = delete;
    CAssetRecord(CAssetRecord&&) noexcept = default;
    CAssetRecord& operator=(CAssetRecord&&) noexcept = default;
    ~CAssetRecord() noexcept = default;

    [[nodiscard]] bool is_ready() const noexcept { return m_owner.is_ready(); }
    [[nodiscard]] type_id query_type_id() const noexcept { return m_owner.query_type_id(); }
    [[nodiscard]] bool has_dependency(const mount_point_ids::id_type mount) const noexcept { return m_owner.has_hazard(mount); }

    template<typename T>
    [[nodiscard]] T* payload() noexcept { return m_owner.payload<T>(); }

    template<typename T>
    [[nodiscard]] const T* payload() const noexcept { return m_owner.payload<T>(); }

private:
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

    [[nodiscard]] CAssetId insert(CErasedOwner&& owner) noexcept;

    [[nodiscard]] CAssetRecord* resolve(const CAssetId id) noexcept;

    [[nodiscard]] const CAssetRecord* resolve(const CAssetId id) const noexcept;

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

inline CAssetId CAssetRepository::insert(CErasedOwner&& owner) noexcept
{
    if (!owner.is_ready() || (m_next_id == 0u))
    {
        return CAssetId{};
    }

    const CAssetId id{ m_next_id++ };
    return (m_assets.emplace(id, std::move(owner)) >= 0) ? id : CAssetId{};
}

inline CAssetRecord* CAssetRepository::resolve(const CAssetId id) noexcept
{
    return id.is_valid() ? m_assets.get_object(id) : nullptr;
}

inline const CAssetRecord* CAssetRepository::resolve(const CAssetId id) const noexcept
{
    return id.is_valid() ? m_assets.get_object(id) : nullptr;
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
