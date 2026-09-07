
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
#include "data_model/data_model_types.hpp"

enum class EBakedValueType : std::uint8_t
{
    invalid = 0u,
    null_value,
    boolean,
    integer,
    floating_point,
    string,
    array,
    object,
    recovered_array,
};

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

struct SBakedDocumentHeader;
struct SBakedValueRecord;

class CBakedDocument
{
public:
    CBakedDocument() noexcept = default;
    CBakedDocument(const void* bytes, std::size_t byte_count) noexcept;

    [[nodiscard]] bool reset(const void* bytes, std::size_t byte_count) noexcept;
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
    [[nodiscard]] bool contains(CBakedValueIndex value) const noexcept;
    [[nodiscard]] EBakedValueType value_type(CBakedValueIndex value) const noexcept;
    [[nodiscard]] bool is_object_entry(CBakedValueIndex value) const noexcept;
    [[nodiscard]] CPropertyNameId name_id(CBakedValueIndex value) const noexcept;
    [[nodiscard]] CStringView name(CBakedValueIndex value) const noexcept;
    [[nodiscard]] CPropertyNameId property_name_id_at_rank(std::uint32_t rank) const noexcept;
    [[nodiscard]] CStringValueId string_value_id_at_rank(std::uint32_t rank) const noexcept;
    [[nodiscard]] CStringView property_name(CPropertyNameId id) const noexcept;
    [[nodiscard]] CStringView string_value(CStringValueId id) const noexcept;

    //  Tree relationships
    [[nodiscard]] CBakedValueIndex parent(CBakedValueIndex value) const noexcept;
    [[nodiscard]] CBakedValueIndex previous_sibling(CBakedValueIndex value) const noexcept;
    [[nodiscard]] CBakedValueIndex next_sibling(CBakedValueIndex value) const noexcept;
    [[nodiscard]] std::uint32_t child_count(CBakedValueIndex container_value) const noexcept;
    [[nodiscard]] CBakedValueIndex first_child(CBakedValueIndex container_value) const noexcept;
    [[nodiscard]] CBakedValueIndex last_child(CBakedValueIndex container_value) const noexcept;
    [[nodiscard]] CBakedValueIndex array_at(CBakedValueIndex array, std::uint32_t index) const noexcept;
    [[nodiscard]] CBakedValueIndex object_child(CBakedValueIndex object, CPropertyNameId name) const noexcept;
    [[nodiscard]] CBakedValueIndex object_child(CBakedValueIndex object, const CStringView& name) const noexcept;

    //  Typed payload access
    [[nodiscard]] bool boolean_value(CBakedValueIndex value, bool& result) const noexcept;
    [[nodiscard]] bool signed_integer_value(CBakedValueIndex value, std::int64_t& result) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(CBakedValueIndex value, std::uint64_t& result) const noexcept;
    [[nodiscard]] bool integer_metadata(CBakedValueIndex value, CIntegerMetadata& result) const noexcept;
    [[nodiscard]] bool floating_point_value(CBakedValueIndex value, double& result) const noexcept;
    [[nodiscard]] CStringValueId string_value_id(CBakedValueIndex value) const noexcept;
    [[nodiscard]] CStringView string_value(CBakedValueIndex value) const noexcept;

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
        std::size_t supplied_byte_count,
        SLayout& layout) noexcept;
    [[nodiscard]] static bool validate(const std::uint8_t* bytes, std::size_t byte_count) noexcept;
    [[nodiscard]] static bool validate_string_table(
        const std::uint8_t* bytes,
        std::uint32_t references_offset,
        std::uint32_t reference_count,
        std::uint32_t bytes_offset,
        std::uint32_t string_byte_count) noexcept;
    [[nodiscard]] static bool value_type_is_array(EBakedValueType type) noexcept;
    [[nodiscard]] static bool value_type_is_container(EBakedValueType type) noexcept;
    [[nodiscard]] static bool decode_integer_metadata(std::uint8_t flags, CIntegerMetadata& metadata) noexcept;
    [[nodiscard]] static bool validate_integer(const SBakedValueRecord& value) noexcept;
    [[nodiscard]] const SBakedDocumentHeader* header() const noexcept;
    [[nodiscard]] const SBakedValueRecord* values(const SLayout& layout) const noexcept;
    [[nodiscard]] const SBakedValueRecord* value_record(CBakedValueIndex value) const noexcept;
    [[nodiscard]] CPropertyNameId find_property_name_id(const CStringView& name) const noexcept;
    [[nodiscard]] CStringView string_from(
        std::uint32_t id,
        std::uint32_t references_offset,
        std::uint32_t reference_count,
        std::uint32_t bytes_offset) const noexcept;

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

    //  Construction and release
    [[nodiscard]] bool build_from(const CLiveDocument& source) noexcept;
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
    void replace_with(CBakedDocumentBlock& source) noexcept;

    CByteBuffer m_bytes;
    CBakedDocument m_document;
};

static_assert(std::is_nothrow_move_constructible_v<CBakedDocumentBlock>);
static_assert(std::is_nothrow_move_assignable_v<CBakedDocumentBlock>);
static_assert(!std::is_copy_constructible_v<CBakedDocumentBlock>);
static_assert(!std::is_copy_assignable_v<CBakedDocumentBlock>);

#endif // BAKED_DOCUMENT_HPP_INCLUDED
