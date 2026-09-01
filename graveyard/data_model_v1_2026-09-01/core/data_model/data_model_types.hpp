
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_model_types.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Aug 26
//
//  Fundamental POD types for the mutable structured-data model.

#pragma once

#ifndef DATA_MODEL_TYPES_HPP_INCLUDED
#define DATA_MODEL_TYPES_HPP_INCLUDED

#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

class CLiveDocument;
class CBakedDocumentBuilder;
class CBakedDocument;
class CBakedDocumentBlock;

class CNodeKey
{
public:
    constexpr CNodeKey() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept;
    [[nodiscard]] explicit constexpr operator bool() const noexcept;
    [[nodiscard]] constexpr std::uint64_t query_value() const noexcept;
    [[nodiscard]] constexpr std::int32_t relationship(const CNodeKey& other) const noexcept;

private:
    explicit constexpr CNodeKey(const std::uint64_t value) noexcept;
    std::uint64_t m_value{ 0u };
    friend class CLiveDocument;
    friend class CBakedDocumentBuilder;
    friend class CBakedDocument;
    friend class CBakedDocumentBlock;
};

[[nodiscard]] constexpr bool operator==(const CNodeKey lhs, const CNodeKey rhs) noexcept;
[[nodiscard]] constexpr bool operator!=(const CNodeKey lhs, const CNodeKey rhs) noexcept;

class CPropertyNameId
{
public:
    constexpr CPropertyNameId() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept;
    [[nodiscard]] explicit constexpr operator bool() const noexcept;
    [[nodiscard]] constexpr std::uint32_t query_value() const noexcept;

private:
    explicit constexpr CPropertyNameId(const std::uint32_t value) noexcept;
    std::uint32_t m_value{ 0u };
    friend class CLiveDocument;
    friend class CBakedDocumentBuilder;
    friend class CBakedDocument;
    friend class CBakedDocumentBlock;
};

class CStringValueId
{
public:
    constexpr CStringValueId() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept;
    [[nodiscard]] explicit constexpr operator bool() const noexcept;
    [[nodiscard]] constexpr std::uint32_t query_value() const noexcept;

private:
    explicit constexpr CStringValueId(const std::uint32_t value) noexcept;
    std::uint32_t m_value{ 0u };
    friend class CLiveDocument;
    friend class CBakedDocumentBuilder;
    friend class CBakedDocument;
    friend class CBakedDocumentBlock;
};

[[nodiscard]] constexpr bool operator==(const CPropertyNameId lhs, const CPropertyNameId rhs) noexcept;
[[nodiscard]] constexpr bool operator!=(const CPropertyNameId lhs, const CPropertyNameId rhs) noexcept;
[[nodiscard]] constexpr bool operator==(const CStringValueId lhs, const CStringValueId rhs) noexcept;
[[nodiscard]] constexpr bool operator!=(const CStringValueId lhs, const CStringValueId rhs) noexcept;

//==============================================================================
//  CBakedNodeIndex
//  Dense, document-lifecycle-local index. Zero is invalid.
//==============================================================================

class CBakedNodeIndex
{
public:
    constexpr CBakedNodeIndex() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept;
    [[nodiscard]] explicit constexpr operator bool() const noexcept;
    [[nodiscard]] constexpr std::uint32_t query_value() const noexcept;

private:
    explicit constexpr CBakedNodeIndex(const std::uint32_t value) noexcept;
    std::uint32_t m_value{ 0u };
    friend class CBakedDocumentBuilder;
    friend class CBakedDocument;
    friend class CBakedDocumentBlock;
};

[[nodiscard]] constexpr bool operator==(const CBakedNodeIndex lhs, const CBakedNodeIndex rhs) noexcept;
[[nodiscard]] constexpr bool operator!=(const CBakedNodeIndex lhs, const CBakedNodeIndex rhs) noexcept;

enum class EJsonNodeType : std::uint8_t
{
    invalid = 0,
    null_value,
    boolean,
    integer,
    floating_point,
    string,
    array,
    object,
    recovered_duplicate_array,
};

//  Numeric intent is stored in the existing node flags byte.  Integer width is
//  semantic information: it is always the smallest width which holds the
//  value under the selected signedness.
enum class EJsonIntegerSign : std::uint8_t
{
    unsigned_value = 0u,
    signed_value = 1u,          //  '-' for negative values and '+' for non-negative values.
};

enum class EJsonIntegerWidth : std::uint8_t
{
    bits_8 = 0u,
    bits_16 = 1u,
    bits_32 = 2u,
    bits_64 = 3u,
};

enum class EJsonIntegerNotation : std::uint8_t
{
    decimal = 0u,
    hexadecimal = 1u,
    binary = 2u,
};

enum class EJsonIntegerPrefix : std::uint8_t
{
    standard = 0u,              //  0x for hexadecimal; 0b for binary.
    alternate = 1u,             //  # for hexadecimal only.
};

struct CJsonIntegerMetadata
{
    EJsonIntegerSign sign{ EJsonIntegerSign::unsigned_value };
    EJsonIntegerWidth width{ EJsonIntegerWidth::bits_8 };
    EJsonIntegerNotation notation{ EJsonIntegerNotation::decimal };
    EJsonIntegerPrefix prefix{ EJsonIntegerPrefix::standard };
};

constexpr std::uint8_t k_json_numeric_sign_mask = 0x01u;
constexpr std::uint8_t k_json_numeric_width_mask = 0x06u;
constexpr std::uint8_t k_json_numeric_width_shift = 1u;
constexpr std::uint8_t k_json_numeric_notation_mask = 0x18u;
constexpr std::uint8_t k_json_numeric_notation_shift = 3u;
constexpr std::uint8_t k_json_numeric_alternate_prefix = 0x20u;
constexpr std::uint8_t k_json_numeric_reserved_mask = 0xC0u;

[[nodiscard]] constexpr bool json_integer_metadata_is_valid(const CJsonIntegerMetadata& metadata) noexcept
{
    return
        (static_cast<std::uint8_t>(metadata.sign) <= static_cast<std::uint8_t>(EJsonIntegerSign::signed_value)) &&
        (static_cast<std::uint8_t>(metadata.width) <= static_cast<std::uint8_t>(EJsonIntegerWidth::bits_64)) &&
        (static_cast<std::uint8_t>(metadata.notation) <= static_cast<std::uint8_t>(EJsonIntegerNotation::binary)) &&
        (static_cast<std::uint8_t>(metadata.prefix) <= static_cast<std::uint8_t>(EJsonIntegerPrefix::alternate)) &&
        (metadata.notation != EJsonIntegerNotation::decimal || metadata.prefix == EJsonIntegerPrefix::standard) &&
        (metadata.notation != EJsonIntegerNotation::binary || metadata.prefix == EJsonIntegerPrefix::standard);
}

[[nodiscard]] constexpr std::uint8_t json_integer_flags(const CJsonIntegerMetadata& metadata) noexcept
{
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(metadata.sign) |
        (static_cast<std::uint8_t>(metadata.width) << k_json_numeric_width_shift) |
        (static_cast<std::uint8_t>(metadata.notation) << k_json_numeric_notation_shift) |
        (metadata.prefix == EJsonIntegerPrefix::alternate ? k_json_numeric_alternate_prefix : 0u));
}

[[nodiscard]] constexpr bool json_integer_metadata_from_flags(const std::uint8_t flags, CJsonIntegerMetadata& metadata) noexcept
{
    if ((flags & k_json_numeric_reserved_mask) != 0u ||
        ((flags & k_json_numeric_notation_mask) >> k_json_numeric_notation_shift) == 3u)
    {
        return false;
    }
    metadata = CJsonIntegerMetadata{
        static_cast<EJsonIntegerSign>(flags & k_json_numeric_sign_mask),
        static_cast<EJsonIntegerWidth>((flags & k_json_numeric_width_mask) >> k_json_numeric_width_shift),
        static_cast<EJsonIntegerNotation>((flags & k_json_numeric_notation_mask) >> k_json_numeric_notation_shift),
        (flags & k_json_numeric_alternate_prefix) ? EJsonIntegerPrefix::alternate : EJsonIntegerPrefix::standard };
    return json_integer_metadata_is_valid(metadata);
}

[[nodiscard]] constexpr EJsonIntegerWidth json_unsigned_integer_smallest_width(const std::uint64_t value) noexcept
{
    return
        (value <= 0xFFu) ? EJsonIntegerWidth::bits_8 :
        (value <= 0xFFFFu) ? EJsonIntegerWidth::bits_16 :
        (value <= 0xFFFFFFFFu) ? EJsonIntegerWidth::bits_32 : EJsonIntegerWidth::bits_64;
}

[[nodiscard]] constexpr EJsonIntegerWidth json_signed_integer_smallest_width(const std::int64_t value) noexcept
{
    return
        (value >= -128 && value <= 127) ? EJsonIntegerWidth::bits_8 :
        (value >= -32768 && value <= 32767) ? EJsonIntegerWidth::bits_16 :
        (value >= (-2147483647 - 1) && value <= 2147483647) ? EJsonIntegerWidth::bits_32 : EJsonIntegerWidth::bits_64;
}

[[nodiscard]] constexpr bool json_integer_metadata_matches_signed_value(const std::int64_t value, const CJsonIntegerMetadata& metadata) noexcept
{
    if (!json_integer_metadata_is_valid(metadata)) return false;
    if (metadata.sign == EJsonIntegerSign::signed_value)
    {
        return metadata.width == json_signed_integer_smallest_width(value);
    }
    return (value >= 0) && (metadata.width == json_unsigned_integer_smallest_width(static_cast<std::uint64_t>(value)));
}

[[nodiscard]] constexpr bool json_integer_metadata_matches_unsigned_value(const std::uint64_t value, const CJsonIntegerMetadata& metadata) noexcept
{
    return json_integer_metadata_is_valid(metadata) &&
        (metadata.sign == EJsonIntegerSign::unsigned_value) &&
        (metadata.width == json_unsigned_integer_smallest_width(value));
}

[[nodiscard]] inline std::uint64_t json_integer_bits(const std::int64_t value) noexcept
{
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

[[nodiscard]] inline std::int64_t json_signed_integer_value(const std::uint64_t bits) noexcept
{
    std::int64_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

[[nodiscard]] inline std::uint64_t json_floating_point_bits(const double value) noexcept
{
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

[[nodiscard]] inline bool json_floating_point_is_finite(const double value) noexcept
{
    return (json_floating_point_bits(value) & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
}

[[nodiscard]] inline bool json_integer_requires_morphic_extensions(const std::uint64_t bits, const CJsonIntegerMetadata& metadata) noexcept
{
    return (metadata.notation != EJsonIntegerNotation::decimal) ||
        ((metadata.sign == EJsonIntegerSign::signed_value) && (json_signed_integer_value(bits) >= 0));
}

[[nodiscard]] constexpr bool json_numeric_flags_are_valid(const EJsonNodeType type, const std::uint8_t flags) noexcept
{
    if (type == EJsonNodeType::floating_point)
    {
        return flags == 0u;
    }
    if (type != EJsonNodeType::integer)
    {
        return flags == 0u;
    }
    CJsonIntegerMetadata metadata;
    return json_integer_metadata_from_flags(flags, metadata);
}

[[nodiscard]] inline bool json_integer_value_matches_flags(const std::uint8_t flags, const std::uint64_t bits) noexcept
{
    CJsonIntegerMetadata metadata;
    if (!json_integer_metadata_from_flags(flags, metadata)) return false;
    return (metadata.sign == EJsonIntegerSign::signed_value) ?
        json_integer_metadata_matches_signed_value(json_signed_integer_value(bits), metadata) :
        json_integer_metadata_matches_unsigned_value(bits, metadata);
}

struct CChildList
{
    CNodeKey first;
    CNodeKey last;
    std::uint32_t count;
    std::uint32_t revision;
};

union CJsonPayload
{
    constexpr CJsonPayload() noexcept : unsigned_bits(0u) {}

    std::uint64_t unsigned_bits;
    double floating_value;
    CStringValueId string_value;
    CChildList children;
};

//  The self key permits a registry scan without exposing slot indices.
struct CJsonSlot
{
    CNodeKey self;
    CNodeKey parent;
    CNodeKey previous_sibling;
    CNodeKey next_sibling;
    CPropertyNameId name_in_parent;
    EJsonNodeType type;
    std::uint8_t flags;
    std::uint16_t reserved;
    CJsonPayload payload;
};

struct CArrayCursor
{
    CNodeKey parent;
    CNodeKey current;
    std::uint32_t index{ 0u };
    std::uint32_t revision{ 0u };
};

union CBakedPayload
{
    constexpr CBakedPayload() noexcept : unsigned_bits(0u) {}

    std::uint64_t unsigned_bits;
    double floating_value;
    CStringValueId string_value;
};

//  Direct children occupy a contiguous range in baked node storage.  A parent
//  records the first child index and child count; sibling access is derived
//  from that range rather than stored redundantly in every child.
struct CBakedNode
{
    CBakedNodeIndex parent;
    std::uint32_t first_child_index;
    std::uint32_t child_count;
    CPropertyNameId name_in_parent;
    EJsonNodeType type;
    std::uint8_t flags;
    std::uint16_t reserved;
    CBakedPayload payload;
};

//==============================================================================
//  Fundamental data-model type out of class function bodies
//==============================================================================

constexpr CNodeKey::CNodeKey(const std::uint64_t value) noexcept : m_value(value) {}
constexpr bool CNodeKey::is_valid() const noexcept { return m_value != 0u; }
constexpr CNodeKey::operator bool() const noexcept { return is_valid(); }
constexpr std::uint64_t CNodeKey::query_value() const noexcept { return m_value; }

constexpr std::int32_t CNodeKey::relationship(const CNodeKey& other) const noexcept
{
    return (m_value < other.m_value) ? -1 : ((m_value > other.m_value) ? 1 : 0);
}

constexpr bool operator==(const CNodeKey lhs, const CNodeKey rhs) noexcept { return lhs.query_value() == rhs.query_value(); }
constexpr bool operator!=(const CNodeKey lhs, const CNodeKey rhs) noexcept { return !(lhs == rhs); }

constexpr CPropertyNameId::CPropertyNameId(const std::uint32_t value) noexcept : m_value(value) {}
constexpr bool CPropertyNameId::is_valid() const noexcept { return m_value != 0u; }
constexpr CPropertyNameId::operator bool() const noexcept { return is_valid(); }
constexpr std::uint32_t CPropertyNameId::query_value() const noexcept { return m_value; }

constexpr CStringValueId::CStringValueId(const std::uint32_t value) noexcept : m_value(value) {}
constexpr bool CStringValueId::is_valid() const noexcept { return m_value != 0u; }
constexpr CStringValueId::operator bool() const noexcept { return is_valid(); }
constexpr std::uint32_t CStringValueId::query_value() const noexcept { return m_value; }

constexpr bool operator==(const CPropertyNameId lhs, const CPropertyNameId rhs) noexcept { return lhs.query_value() == rhs.query_value(); }
constexpr bool operator!=(const CPropertyNameId lhs, const CPropertyNameId rhs) noexcept { return !(lhs == rhs); }
constexpr bool operator==(const CStringValueId lhs, const CStringValueId rhs) noexcept { return lhs.query_value() == rhs.query_value(); }
constexpr bool operator!=(const CStringValueId lhs, const CStringValueId rhs) noexcept { return !(lhs == rhs); }

constexpr CBakedNodeIndex::CBakedNodeIndex(const std::uint32_t value) noexcept : m_value(value) {}
constexpr bool CBakedNodeIndex::is_valid() const noexcept { return m_value != 0u; }
constexpr CBakedNodeIndex::operator bool() const noexcept { return is_valid(); }
constexpr std::uint32_t CBakedNodeIndex::query_value() const noexcept { return m_value; }

constexpr bool operator==(const CBakedNodeIndex lhs, const CBakedNodeIndex rhs) noexcept { return lhs.query_value() == rhs.query_value(); }
constexpr bool operator!=(const CBakedNodeIndex lhs, const CBakedNodeIndex rhs) noexcept { return !(lhs == rhs); }

static_assert(std::is_trivially_copyable_v<CNodeKey>);
static_assert(std::is_standard_layout_v<CNodeKey>);
static_assert(sizeof(CNodeKey) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<CJsonPayload>);
static_assert((sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559), "Document floating payloads require IEEE binary64.");
static_assert(std::is_trivially_copyable_v<CJsonSlot>);
static_assert(std::is_standard_layout_v<CJsonSlot>);
static_assert(sizeof(CJsonSlot) == 64u);
static_assert(alignof(CJsonSlot) >= alignof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<CBakedNodeIndex>);
static_assert(std::is_trivially_copyable_v<CBakedNode>);
static_assert(std::is_standard_layout_v<CBakedNode>);
static_assert(sizeof(CBakedNode) == 32u);
static_assert(alignof(CBakedNode) >= alignof(std::uint64_t));

#endif  //  DATA_MODEL_TYPES_HPP_INCLUDED
