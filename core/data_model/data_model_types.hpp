
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_model_types.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    1 Sep 26
//
//  Fundamental live-document identities, roles, and numeric intent.

#pragma once

#ifndef DATA_MODEL_TYPES_HPP_INCLUDED
#define DATA_MODEL_TYPES_HPP_INCLUDED

#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

//==============================================================================
//  Node identity
//==============================================================================

class CLiveDocument;

class CNodeKey
{
public:
    constexpr CNodeKey() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_value != 0u; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }
    [[nodiscard]] constexpr std::uint64_t query_value() const noexcept { return m_value; }
    [[nodiscard]] constexpr std::int32_t relationship(const CNodeKey other) const noexcept
    {
        return (m_value < other.m_value) ? -1 : ((m_value > other.m_value) ? 1 : 0);
    }

private:
    explicit constexpr CNodeKey(const std::uint64_t value) noexcept : m_value(value) {}
    std::uint64_t m_value{ 0u };
    friend class CLiveDocument;
};

[[nodiscard]] constexpr bool operator==(const CNodeKey lhs, const CNodeKey rhs) noexcept
{
    return lhs.query_value() == rhs.query_value();
}

[[nodiscard]] constexpr bool operator!=(const CNodeKey lhs, const CNodeKey rhs) noexcept
{
    return !(lhs == rhs);
}

//==============================================================================
//  Interned-string identities
//==============================================================================

namespace data_model_string_id
{

struct SPropertyNameIdTag;
struct SStringValueIdTag;

template<typename TDomainTag>
class TStringId
{
public:
    //  The canonical empty ID is valid. Default construction produces the
    //  distinct invalid result used by failed or inapplicable queries.
    constexpr TStringId() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_value != k_invalid_value; }
    [[nodiscard]] constexpr bool is_empty() const noexcept { return m_value == k_empty_value; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }
    [[nodiscard]] constexpr std::uint32_t query_value() const noexcept { return m_value; }

    static constexpr std::uint32_t k_empty_value = 0u;
    static constexpr std::uint32_t k_invalid_value = std::numeric_limits<std::uint32_t>::max();

private:
    explicit constexpr TStringId(const std::uint32_t value) noexcept : m_value(value) {}
    std::uint32_t m_value{ k_invalid_value };
    friend class ::CLiveDocument;
};

template<typename TDomainTag>
[[nodiscard]] constexpr bool operator==(const TStringId<TDomainTag> lhs, const TStringId<TDomainTag> rhs) noexcept
{
    return lhs.query_value() == rhs.query_value();
}

template<typename TDomainTag>
[[nodiscard]] constexpr bool operator!=(const TStringId<TDomainTag> lhs, const TStringId<TDomainTag> rhs) noexcept
{
    return !(lhs == rhs);
}

}   //  namespace data_model_string_id

using CPropertyNameId = data_model_string_id::TStringId<data_model_string_id::SPropertyNameIdTag>;
using CStringValueId = data_model_string_id::TStringId<data_model_string_id::SStringValueIdTag>;

//==============================================================================
//  Live node roles and value kinds
//==============================================================================

enum class ELiveNodeRole : std::uint8_t
{
    invalid = 0u,
    value,
    aggregate,
};

enum class ELiveValueType : std::uint8_t
{
    invalid = 0u,
    null_value,
    boolean,
    integer,
    floating_point,
    string,
    array,
    object,
};

enum class ELiveAggregateKind : std::uint8_t
{
    invalid = 0u,
    array,
    object,
    recovered_array,
};

//==============================================================================
//  Attachment results
//==============================================================================

enum class ELiveAttachmentOutcome : std::uint8_t
{
    rejected = 0u,
    inserted,
    recovery_created,
    recovery_extended,
    allocation_failed,
};

enum class ELiveAttachmentRejection : std::uint8_t
{
    none = 0u,
    document_not_ready,
    destination_not_found,
    candidate_not_found,
    destination_not_container,
    candidate_is_root,
    candidate_not_detached,
    unsupported_destination_kind,
    object_entry_required,
    duplicate_object_name,
    insert_before_not_child,
    index_out_of_range,
    cycle,
    relationship_limit,
    accounting_limit,
    corrupt_structure,
};

struct CLiveAttachmentResult
{
    ELiveAttachmentOutcome outcome{ ELiveAttachmentOutcome::rejected };
    ELiveAttachmentRejection rejection{ ELiveAttachmentRejection::none };

    [[nodiscard]] constexpr bool succeeded() const noexcept
    {
        return
            (outcome == ELiveAttachmentOutcome::inserted) ||
            (outcome == ELiveAttachmentOutcome::recovery_created) ||
            (outcome == ELiveAttachmentOutcome::recovery_extended);
    }
};

//==============================================================================
//  Integer intent and metadata
//==============================================================================

enum class EIntegerDomain : std::uint8_t
{
    signed_value = 0u,
    unsigned_value,
};

enum class EIntegerWidth : std::uint8_t
{
    bits_8 = 0u,
    bits_16,
    bits_32,
    bits_64,
};

enum class EIntegerNotation : std::uint8_t
{
    decimal = 0u,
    hexadecimal,
    binary,
};

enum class EIntegerPrefix : std::uint8_t
{
    standard = 0u,
    alternate,
};

struct CIntegerMetadata
{
    EIntegerDomain domain{ EIntegerDomain::signed_value };
    EIntegerWidth width{ EIntegerWidth::bits_8 };
    EIntegerNotation notation{ EIntegerNotation::decimal };
    EIntegerPrefix prefix{ EIntegerPrefix::standard };
};

[[nodiscard]] constexpr bool operator==(const CIntegerMetadata lhs, const CIntegerMetadata rhs) noexcept
{
    return (lhs.domain == rhs.domain) && (lhs.width == rhs.width) &&
        (lhs.notation == rhs.notation) && (lhs.prefix == rhs.prefix);
}

[[nodiscard]] constexpr bool operator!=(const CIntegerMetadata lhs, const CIntegerMetadata rhs) noexcept
{
    return !(lhs == rhs);
}

[[nodiscard]] constexpr bool live_integer_metadata_is_valid(const CIntegerMetadata metadata) noexcept
{
    return
        (static_cast<std::uint8_t>(metadata.domain) <= static_cast<std::uint8_t>(EIntegerDomain::unsigned_value)) &&
        (static_cast<std::uint8_t>(metadata.width) <= static_cast<std::uint8_t>(EIntegerWidth::bits_64)) &&
        (static_cast<std::uint8_t>(metadata.notation) <= static_cast<std::uint8_t>(EIntegerNotation::binary)) &&
        (static_cast<std::uint8_t>(metadata.prefix) <= static_cast<std::uint8_t>(EIntegerPrefix::alternate)) &&
        ((metadata.notation == EIntegerNotation::hexadecimal) || (metadata.prefix == EIntegerPrefix::standard));
}

[[nodiscard]] constexpr EIntegerWidth live_signed_integer_smallest_width(const std::int64_t value) noexcept
{
    return
        ((value >= -128) && (value <= 127)) ? EIntegerWidth::bits_8 :
        ((value >= -32768) && (value <= 32767)) ? EIntegerWidth::bits_16 :
        ((value >= (-2147483647 - 1)) && (value <= 2147483647)) ? EIntegerWidth::bits_32 : EIntegerWidth::bits_64;
}

[[nodiscard]] constexpr EIntegerWidth live_unsigned_integer_smallest_width(const std::uint64_t value) noexcept
{
    return
        (value <= 0xffu) ? EIntegerWidth::bits_8 :
        (value <= 0xffffu) ? EIntegerWidth::bits_16 :
        (value <= 0xffffffffu) ? EIntegerWidth::bits_32 : EIntegerWidth::bits_64;
}

[[nodiscard]] constexpr bool live_integer_metadata_matches_signed(const std::int64_t value, const CIntegerMetadata metadata) noexcept
{
    return live_integer_metadata_is_valid(metadata) &&
        (metadata.domain == EIntegerDomain::signed_value) &&
        (metadata.width == live_signed_integer_smallest_width(value));
}

[[nodiscard]] constexpr bool live_integer_metadata_matches_unsigned(const std::uint64_t value, const CIntegerMetadata metadata) noexcept
{
    return live_integer_metadata_is_valid(metadata) &&
        (metadata.domain == EIntegerDomain::unsigned_value) &&
        (metadata.width == live_unsigned_integer_smallest_width(value));
}

//==============================================================================
//  Numeric payload representation
//==============================================================================

[[nodiscard]] inline std::uint64_t live_signed_integer_bits(const std::int64_t value) noexcept
{
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

[[nodiscard]] inline std::int64_t live_signed_integer_from_bits(const std::uint64_t bits) noexcept
{
    std::int64_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

[[nodiscard]] inline std::uint64_t live_floating_point_bits(const double value) noexcept
{
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

[[nodiscard]] inline double live_floating_point_from_bits(const std::uint64_t bits) noexcept
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

[[nodiscard]] inline bool live_floating_point_is_finite(const double value) noexcept
{
    return (live_floating_point_bits(value) & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
}

//==============================================================================
//  Representation guarantees
//==============================================================================

static_assert(std::is_trivially_copyable_v<CNodeKey>);
static_assert(std::is_standard_layout_v<CNodeKey>);
static_assert(sizeof(CNodeKey) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<CPropertyNameId>);
static_assert(std::is_trivially_copyable_v<CStringValueId>);
static_assert(!std::is_same_v<CPropertyNameId, CStringValueId>);
static_assert(sizeof(CPropertyNameId) == sizeof(std::uint32_t));
static_assert(sizeof(CStringValueId) == sizeof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<CIntegerMetadata>);
static_assert(sizeof(CIntegerMetadata) == 4u);
static_assert(std::is_trivially_copyable_v<CLiveAttachmentResult>);
static_assert(sizeof(CLiveAttachmentResult) == 2u);
static_assert(((sizeof(double) == sizeof(std::uint64_t)) && std::numeric_limits<double>::is_iec559),
    "Live floating payloads require IEEE-754 binary64.");

#endif // DATA_MODEL_TYPES_HPP_INCLUDED
