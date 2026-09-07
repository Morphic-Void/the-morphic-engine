
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_document_format.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Sep 26
//
//  Physical records and constants for the replacement baked-document format.

#pragma once

#ifndef BAKED_DOCUMENT_FORMAT_HPP_INCLUDED
#define BAKED_DOCUMENT_FORMAT_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

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

namespace baked_document_format
{

constexpr std::uint32_t k_magic = 0x3244424du; // "MBD2"
constexpr std::uint16_t k_version = 1u;
constexpr std::uint16_t k_header_size = 32u;
constexpr std::size_t k_block_alignment = 32u;
constexpr std::uint32_t k_invalid_index = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint8_t k_integer_metadata_flags = 0x3fu;
constexpr std::uint8_t k_first_sibling_flag = 0x40u;
constexpr std::uint8_t k_last_sibling_flag = 0x80u;
constexpr std::uint8_t k_sibling_position_flags = k_first_sibling_flag | k_last_sibling_flag;

} // namespace baked_document_format

static_assert(static_cast<std::uint8_t>(EBakedValueType::null_value) == 1u);
static_assert(static_cast<std::uint8_t>(EBakedValueType::boolean) == 2u);
static_assert(static_cast<std::uint8_t>(EBakedValueType::integer) == 3u);
static_assert(static_cast<std::uint8_t>(EBakedValueType::floating_point) == 4u);
static_assert(static_cast<std::uint8_t>(EBakedValueType::string) == 5u);
static_assert(static_cast<std::uint8_t>(EBakedValueType::array) == 6u);
static_assert(static_cast<std::uint8_t>(EBakedValueType::object) == 7u);
static_assert(static_cast<std::uint8_t>(EBakedValueType::recovered_array) == 8u);

struct SBakedDocumentHeader
{
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t header_size;
    std::uint32_t total_size;
    std::uint32_t value_count;
    std::uint32_t property_name_reference_count;
    std::uint32_t property_name_byte_count;
    std::uint32_t string_value_reference_count;
    std::uint32_t string_value_byte_count;
};

struct alignas(32) SBakedValueRecord
{
    std::uint64_t payload_bits;
    std::uint32_t parent_index;
    std::uint32_t first_child_index;
    std::uint32_t child_count;
    std::uint32_t property_name_index;
    EBakedValueType value_type;
    std::uint8_t value_flags;
    std::uint16_t reserved_16;
    std::uint32_t reserved_32;
};

struct SBakedStringReference
{
    std::uint32_t offset;
    std::uint32_t length;
};

static_assert(std::is_trivially_copyable_v<SBakedDocumentHeader>);
static_assert(std::is_standard_layout_v<SBakedDocumentHeader>);
static_assert(sizeof(SBakedDocumentHeader) == 32u);
static_assert(offsetof(SBakedDocumentHeader, magic) == 0u);
static_assert(offsetof(SBakedDocumentHeader, version) == 4u);
static_assert(offsetof(SBakedDocumentHeader, header_size) == 6u);
static_assert(offsetof(SBakedDocumentHeader, total_size) == 8u);
static_assert(offsetof(SBakedDocumentHeader, value_count) == 12u);
static_assert(offsetof(SBakedDocumentHeader, property_name_reference_count) == 16u);
static_assert(offsetof(SBakedDocumentHeader, property_name_byte_count) == 20u);
static_assert(offsetof(SBakedDocumentHeader, string_value_reference_count) == 24u);
static_assert(offsetof(SBakedDocumentHeader, string_value_byte_count) == 28u);
static_assert(std::is_trivially_copyable_v<SBakedValueRecord>);
static_assert(std::is_standard_layout_v<SBakedValueRecord>);
static_assert(sizeof(SBakedValueRecord) == 32u);
static_assert(alignof(SBakedValueRecord) == baked_document_format::k_block_alignment);
static_assert(offsetof(SBakedValueRecord, payload_bits) == 0u);
static_assert(offsetof(SBakedValueRecord, parent_index) == 8u);
static_assert(offsetof(SBakedValueRecord, first_child_index) == 12u);
static_assert(offsetof(SBakedValueRecord, child_count) == 16u);
static_assert(offsetof(SBakedValueRecord, property_name_index) == 20u);
static_assert(offsetof(SBakedValueRecord, value_type) == 24u);
static_assert(offsetof(SBakedValueRecord, value_flags) == 25u);
static_assert(offsetof(SBakedValueRecord, reserved_16) == 26u);
static_assert(offsetof(SBakedValueRecord, reserved_32) == 28u);
static_assert(std::is_trivially_copyable_v<SBakedStringReference>);
static_assert(std::is_standard_layout_v<SBakedStringReference>);
static_assert(sizeof(SBakedStringReference) == 8u);
static_assert(offsetof(SBakedStringReference, offset) == 0u);
static_assert(offsetof(SBakedStringReference, length) == 4u);

#endif // BAKED_DOCUMENT_FORMAT_HPP_INCLUDED
