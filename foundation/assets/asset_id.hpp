
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    asset_id.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    11 Nov 26
//
//  Neutral runtime asset identity. Construction does not acquire, register or
//  validate an asset in any repository, and does not extend backing lifetime.

#pragma once

#include <cstdint>
#include <type_traits>

//==============================================================================
//  CAssetId
//  Strong repository-local asset identity. Zero is always invalid.
//==============================================================================

class CAssetId
{
public:
    constexpr CAssetId() noexcept = default;
    explicit constexpr CAssetId(const std::uint64_t value) noexcept : m_value(value) {}

    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_value != 0u; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }
    [[nodiscard]] constexpr std::uint64_t query_value() const noexcept { return m_value; }

    [[nodiscard]] constexpr std::int32_t relationship(const CAssetId& other) const noexcept;

private:
    std::uint64_t m_value{ 0u };
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
