
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_document.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Sep 26
//
//  Checked immutable view over the replacement baked-document format.

#pragma once

#ifndef BAKED_DOCUMENT_HPP_INCLUDED
#define BAKED_DOCUMENT_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "containers/ByteBuffers.hpp"
#include "containers/StringBuffers.hpp"
#include "data_model/baked_document_format.hpp"
#include "data_model/data_model_types.hpp"

class CBakedValueIndex
{
public:
    constexpr CBakedValueIndex() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return m_value != std::numeric_limits<std::uint32_t>::max();
    }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }
    [[nodiscard]] constexpr std::uint32_t query_value() const noexcept { return m_value; }

private:
    explicit constexpr CBakedValueIndex(const std::uint32_t value) noexcept : m_value(value) {}
    std::uint32_t m_value{ std::numeric_limits<std::uint32_t>::max() };
    friend class CBakedDocument;
};

[[nodiscard]] constexpr bool operator==(const CBakedValueIndex lhs, const CBakedValueIndex rhs) noexcept
{
    return lhs.query_value() == rhs.query_value();
}

[[nodiscard]] constexpr bool operator!=(const CBakedValueIndex lhs, const CBakedValueIndex rhs) noexcept
{
    return !(lhs == rhs);
}

static_assert(std::is_trivially_copyable_v<CBakedValueIndex>);
static_assert(std::is_standard_layout_v<CBakedValueIndex>);
static_assert(sizeof(CBakedValueIndex) == sizeof(std::uint32_t));

class CBakedDocumentBaker;

class CBakedDocument
{
public:
    CBakedDocument() noexcept = default;
    CBakedDocument(const void* const bytes, const std::size_t byte_count) noexcept;

    [[nodiscard]] bool reset(const void* const bytes, const std::size_t byte_count) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] bool check_integrity() const noexcept;
    [[nodiscard]] bool is_canonical() const noexcept;
    [[nodiscard]] bool contains_recovered_content() const noexcept;

    [[nodiscard]] std::size_t byte_count() const noexcept;
    [[nodiscard]] CBakedValueIndex root() const noexcept;
    [[nodiscard]] std::uint32_t value_count() const noexcept;
    [[nodiscard]] std::uint32_t property_name_count() const noexcept;
    [[nodiscard]] std::uint32_t string_value_count() const noexcept;

    //  Value classification and interned text
    [[nodiscard]] bool contains(const CBakedValueIndex value) const noexcept;
    [[nodiscard]] EBakedValueType value_type(const CBakedValueIndex value) const noexcept;
    [[nodiscard]] bool is_object_entry(const CBakedValueIndex value) const noexcept;
    [[nodiscard]] CPropertyNameId name_id(const CBakedValueIndex value) const noexcept;
    [[nodiscard]] CStringView name(const CBakedValueIndex value) const noexcept;
    [[nodiscard]] CPropertyNameId property_name_id_at_rank(const std::uint32_t rank) const noexcept;
    [[nodiscard]] CStringValueId string_value_id_at_rank(const std::uint32_t rank) const noexcept;
    [[nodiscard]] CStringView property_name(const CPropertyNameId id) const noexcept;
    [[nodiscard]] CStringView string_value(const CStringValueId id) const noexcept;

    //  Tree relationships
    [[nodiscard]] CBakedValueIndex parent(const CBakedValueIndex value) const noexcept;
    [[nodiscard]] CBakedValueIndex previous_sibling(const CBakedValueIndex value) const noexcept;
    [[nodiscard]] CBakedValueIndex next_sibling(const CBakedValueIndex value) const noexcept;
    [[nodiscard]] std::uint32_t child_count(const CBakedValueIndex container_value) const noexcept;
    [[nodiscard]] CBakedValueIndex first_child(const CBakedValueIndex container_value) const noexcept;
    [[nodiscard]] CBakedValueIndex last_child(const CBakedValueIndex container_value) const noexcept;
    [[nodiscard]] CBakedValueIndex array_at(const CBakedValueIndex array, const std::uint32_t index) const noexcept;
    [[nodiscard]] CBakedValueIndex object_child(const CBakedValueIndex object, const CPropertyNameId name) const noexcept;
    [[nodiscard]] CBakedValueIndex object_child(const CBakedValueIndex object, const CStringView& name) const noexcept;

    //  Typed payload access
    [[nodiscard]] bool boolean_value(const CBakedValueIndex value, bool& result) const noexcept;
    [[nodiscard]] bool signed_integer_value(const CBakedValueIndex value, std::int64_t& result) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(const CBakedValueIndex value, std::uint64_t& result) const noexcept;
    [[nodiscard]] bool integer_metadata(const CBakedValueIndex value, CIntegerMetadata& result) const noexcept;
    [[nodiscard]] bool floating_point_value(const CBakedValueIndex value, double& result) const noexcept;
    [[nodiscard]] CStringValueId string_value_id(const CBakedValueIndex value) const noexcept;
    [[nodiscard]] CStringView string_value(const CBakedValueIndex value) const noexcept;

private:
    struct SLayout
    {
        std::uint32_t values_offset{ 0u };
        std::uint32_t property_name_references_offset{ 0u };
        std::uint32_t string_value_references_offset{ 0u };
        std::uint32_t property_name_bytes_offset{ 0u };
        std::uint32_t string_value_bytes_offset{ 0u };
    };

    [[nodiscard]] static bool derive_layout(
        const SBakedDocumentHeader& header,
        const std::size_t supplied_byte_count,
        SLayout& layout) noexcept;
    [[nodiscard]] static bool validate(
        const std::uint8_t* const bytes,
        const std::size_t byte_count) noexcept;
    [[nodiscard]] static bool validate_string_table(
        const std::uint8_t* const bytes,
        const std::uint32_t references_offset,
        const std::uint32_t reference_count,
        const std::uint32_t bytes_offset,
        const std::uint32_t string_byte_count) noexcept;
    [[nodiscard]] static bool value_type_is_array(const EBakedValueType type) noexcept;
    [[nodiscard]] static bool value_type_is_container(const EBakedValueType type) noexcept;
    [[nodiscard]] static bool decode_integer_metadata(
        const std::uint8_t flags,
        CIntegerMetadata& metadata) noexcept;
    [[nodiscard]] static bool validate_integer(const SBakedValueRecord& value) noexcept;
    [[nodiscard]] const SBakedDocumentHeader* header() const noexcept;
    [[nodiscard]] const SBakedValueRecord* values(const SLayout& layout) const noexcept;
    [[nodiscard]] const SBakedValueRecord* value_record(const CBakedValueIndex value) const noexcept;
    [[nodiscard]] CPropertyNameId find_property_name_id(const CStringView& name) const noexcept;
    [[nodiscard]] CStringView string_from(
        const std::uint32_t id,
        const std::uint32_t references_offset,
        const std::uint32_t reference_count,
        const std::uint32_t bytes_offset) const noexcept;

    const std::uint8_t* m_bytes{ nullptr };
    std::size_t m_byte_count{ 0u };
};

static_assert(std::is_nothrow_copy_constructible_v<CBakedDocument>);
static_assert(std::is_nothrow_copy_assignable_v<CBakedDocument>);

class CBakedDocumentBlock
{
public:

    //  Lifetime and ownership
    CBakedDocumentBlock() noexcept = default;
    CBakedDocumentBlock(CBakedDocumentBlock&& source) noexcept;
    CBakedDocumentBlock& operator=(CBakedDocumentBlock&& source) noexcept;
    CBakedDocumentBlock(const CBakedDocumentBlock&) = delete;
    CBakedDocumentBlock& operator=(const CBakedDocumentBlock&) = delete;
    ~CBakedDocumentBlock() noexcept = default;

    //  Release
    void deallocate() noexcept;

    //  Status and immutable access
    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] const CBakedDocument& document() const noexcept;
    [[nodiscard]] CByteConstView bytes() const noexcept;

    //  Direct storage attribution
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;

private:
    friend class CBakedDocumentBaker;

    void replace_with(CBakedDocumentBlock& source) noexcept;

    CByteBuffer m_bytes;
    CBakedDocument m_document;
};

static_assert(std::is_nothrow_move_constructible_v<CBakedDocumentBlock>);
static_assert(std::is_nothrow_move_assignable_v<CBakedDocumentBlock>);
static_assert(!std::is_copy_constructible_v<CBakedDocumentBlock>);
static_assert(!std::is_copy_assignable_v<CBakedDocumentBlock>);

//==============================================================================
//  CBakedDocument: state and counts
//==============================================================================

inline void CBakedDocument::clear() noexcept
{
    m_bytes = nullptr;
    m_byte_count = 0u;
}

inline bool CBakedDocument::is_ready() const noexcept
{
    return m_bytes != nullptr;
}

inline std::size_t CBakedDocument::byte_count() const noexcept
{
    return is_ready() ? m_byte_count : 0u;
}

inline CBakedValueIndex CBakedDocument::root() const noexcept
{
    return is_ready() ? CBakedValueIndex{ 0u } : CBakedValueIndex{};
}

inline std::uint32_t CBakedDocument::value_count() const noexcept
{
    return is_ready() ? header()->value_count : 0u;
}

inline std::uint32_t CBakedDocument::property_name_count() const noexcept
{
    return is_ready() ? (header()->property_name_reference_count - 1u) : 0u;
}

inline std::uint32_t CBakedDocument::string_value_count() const noexcept
{
    return is_ready() ? (header()->string_value_reference_count - 1u) : 0u;
}

//==============================================================================
//  CBakedDocument: value classification and interned identifiers
//==============================================================================

inline bool CBakedDocument::contains(const CBakedValueIndex value) const noexcept
{
    return value_record(value) != nullptr;
}

inline EBakedValueType CBakedDocument::value_type(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return (record != nullptr) ? record->value_type : EBakedValueType::invalid;
}

inline bool CBakedDocument::is_object_entry(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return (record != nullptr) && (record->property_name_index != 0u);
}

inline CPropertyNameId CBakedDocument::name_id(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return (record != nullptr) ? CPropertyNameId{ record->property_name_index } : CPropertyNameId{};
}

inline CStringView CBakedDocument::name(const CBakedValueIndex value) const noexcept
{
    return property_name(name_id(value));
}

inline CPropertyNameId CBakedDocument::property_name_id_at_rank(const std::uint32_t rank) const noexcept
{
    return (is_ready() && (rank < header()->property_name_reference_count)) ?
        CPropertyNameId{ rank } : CPropertyNameId{};
}

inline CStringValueId CBakedDocument::string_value_id_at_rank(const std::uint32_t rank) const noexcept
{
    return (is_ready() && (rank < header()->string_value_reference_count)) ?
        CStringValueId{ rank } : CStringValueId{};
}

//==============================================================================
//  CBakedDocument: tree relationships
//==============================================================================

inline CBakedValueIndex CBakedDocument::parent(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return ((record != nullptr) && (record->parent_index != baked_document_format::k_invalid_index)) ?
        CBakedValueIndex{ record->parent_index } : CBakedValueIndex{};
}

inline CBakedValueIndex CBakedDocument::previous_sibling(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return ((record != nullptr) &&
        (record->parent_index != baked_document_format::k_invalid_index) &&
        ((record->value_flags & baked_document_format::k_first_sibling_flag) == 0u)) ?
        CBakedValueIndex{ value.query_value() - 1u } : CBakedValueIndex{};
}

inline CBakedValueIndex CBakedDocument::next_sibling(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return ((record != nullptr) &&
        (record->parent_index != baked_document_format::k_invalid_index) &&
        ((record->value_flags & baked_document_format::k_last_sibling_flag) == 0u)) ?
        CBakedValueIndex{ value.query_value() + 1u } : CBakedValueIndex{};
}

inline std::uint32_t CBakedDocument::child_count(const CBakedValueIndex container_value) const noexcept
{
    const SBakedValueRecord* const record = value_record(container_value);
    return ((record != nullptr) && value_type_is_container(record->value_type)) ? record->child_count : 0u;
}

inline CBakedValueIndex CBakedDocument::first_child(const CBakedValueIndex container_value) const noexcept
{
    const SBakedValueRecord* const record = value_record(container_value);
    return ((record != nullptr) && value_type_is_container(record->value_type) && (record->child_count != 0u)) ?
        CBakedValueIndex{ record->first_child_index } : CBakedValueIndex{};
}

inline CBakedValueIndex CBakedDocument::last_child(const CBakedValueIndex container_value) const noexcept
{
    const SBakedValueRecord* const record = value_record(container_value);
    return ((record != nullptr) && value_type_is_container(record->value_type) && (record->child_count != 0u)) ?
        CBakedValueIndex{ record->first_child_index + record->child_count - 1u } : CBakedValueIndex{};
}

inline CBakedValueIndex CBakedDocument::array_at(
    const CBakedValueIndex array,
    const std::uint32_t index) const noexcept
{
    const SBakedValueRecord* const record = value_record(array);
    return ((record != nullptr) && value_type_is_array(record->value_type) && (index < record->child_count)) ?
        CBakedValueIndex{ record->first_child_index + index } : CBakedValueIndex{};
}

//==============================================================================
//  CBakedDocument: typed payload access
//==============================================================================

inline bool CBakedDocument::boolean_value(const CBakedValueIndex value, bool& result) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    if ((record == nullptr) || (record->value_type != EBakedValueType::boolean))
    {
        return false;
    }
    result = record->payload_bits != 0u;
    return true;
}

inline bool CBakedDocument::signed_integer_value(const CBakedValueIndex value, std::int64_t& result) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    CIntegerMetadata metadata;
    if ((record == nullptr) || (record->value_type != EBakedValueType::integer) ||
        !decode_integer_metadata(
            record->value_flags & baked_document_format::k_integer_metadata_flags,
            metadata) ||
        (metadata.domain != EIntegerDomain::signed_value))
    {
        return false;
    }
    result = live_signed_integer_from_bits(record->payload_bits);
    return true;
}

inline bool CBakedDocument::unsigned_integer_value(const CBakedValueIndex value, std::uint64_t& result) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    CIntegerMetadata metadata;
    if ((record == nullptr) || (record->value_type != EBakedValueType::integer) ||
        !decode_integer_metadata(
            record->value_flags & baked_document_format::k_integer_metadata_flags,
            metadata) ||
        (metadata.domain != EIntegerDomain::unsigned_value))
    {
        return false;
    }
    result = record->payload_bits;
    return true;
}

inline bool CBakedDocument::integer_metadata(
    const CBakedValueIndex value,
    CIntegerMetadata& result) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return (record != nullptr) && (record->value_type == EBakedValueType::integer) &&
        decode_integer_metadata(
            record->value_flags & baked_document_format::k_integer_metadata_flags,
            result);
}

inline bool CBakedDocument::floating_point_value(const CBakedValueIndex value, double& result) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    if ((record == nullptr) || (record->value_type != EBakedValueType::floating_point))
    {
        return false;
    }
    result = live_floating_point_from_bits(record->payload_bits);
    return true;
}

inline CStringValueId CBakedDocument::string_value_id(const CBakedValueIndex value) const noexcept
{
    const SBakedValueRecord* const record = value_record(value);
    return ((record != nullptr) && (record->value_type == EBakedValueType::string)) ?
        CStringValueId{ static_cast<std::uint32_t>(record->payload_bits) } : CStringValueId{};
}

inline CStringView CBakedDocument::string_value(const CBakedValueIndex value) const noexcept
{
    return string_value(string_value_id(value));
}

//==============================================================================
//  CBakedDocument: direct record helpers
//==============================================================================

inline bool CBakedDocument::value_type_is_array(const EBakedValueType type) noexcept
{
    return (type == EBakedValueType::array) || (type == EBakedValueType::recovered_array);
}

inline bool CBakedDocument::value_type_is_container(const EBakedValueType type) noexcept
{
    return value_type_is_array(type) || (type == EBakedValueType::object);
}

inline bool CBakedDocument::decode_integer_metadata(
    const std::uint8_t flags,
    CIntegerMetadata& metadata) noexcept
{
    if ((flags & static_cast<std::uint8_t>(~baked_document_format::k_integer_metadata_flags)) != 0u)
    {
        return false;
    }
    const CIntegerMetadata decoded{
        ((flags & 0x01u) != 0u) ? EIntegerDomain::unsigned_value : EIntegerDomain::signed_value,
        static_cast<EIntegerWidth>((flags >> 1u) & 0x03u),
        static_cast<EIntegerNotation>((flags >> 3u) & 0x03u),
        ((flags & 0x20u) != 0u) ? EIntegerPrefix::alternate : EIntegerPrefix::standard,
    };
    if (!live_integer_metadata_is_valid(decoded))
    {
        return false;
    }
    metadata = decoded;
    return true;
}

inline const SBakedDocumentHeader* CBakedDocument::header() const noexcept
{
    return is_ready() ? reinterpret_cast<const SBakedDocumentHeader*>(m_bytes) : nullptr;
}

inline const SBakedValueRecord* CBakedDocument::values(const SLayout& layout) const noexcept
{
    return reinterpret_cast<const SBakedValueRecord*>(m_bytes + layout.values_offset);
}

inline const SBakedValueRecord* CBakedDocument::value_record(const CBakedValueIndex value) const noexcept
{
    if (!is_ready() || !value.is_valid() || (value.query_value() >= header()->value_count))
    {
        return nullptr;
    }
    return reinterpret_cast<const SBakedValueRecord*>(m_bytes + sizeof(SBakedDocumentHeader)) +
        value.query_value();
}

//==============================================================================
//  CBakedDocumentBlock: direct observation
//==============================================================================

inline bool CBakedDocumentBlock::is_ready() const noexcept
{
    return m_bytes.is_ready() && m_document.is_ready();
}

inline const CBakedDocument& CBakedDocumentBlock::document() const noexcept
{
    return m_document;
}

inline CByteConstView CBakedDocumentBlock::bytes() const noexcept
{
    return m_bytes.const_view();
}

inline std::uint32_t CBakedDocumentBlock::memory_token_count() const noexcept
{
    return m_bytes.memory_token_count();
}

inline std::uint32_t CBakedDocumentBlock::memory_allocation_count() const noexcept
{
    return m_bytes.memory_allocation_count();
}

inline std::uint64_t CBakedDocumentBlock::memory_allocation_size() const noexcept
{
    return m_bytes.memory_allocation_size();
}

#endif // BAKED_DOCUMENT_HPP_INCLUDED
