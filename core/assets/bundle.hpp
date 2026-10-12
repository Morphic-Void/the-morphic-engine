
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    bundle.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Oct 26
//
//  Checked immutable bundle views. Storage is owned by CBakedDocumentBlock.

#pragma once

#ifndef BUNDLE_HPP_INCLUDED
#define BUNDLE_HPP_INCLUDED

#include "data_model/baked_document.hpp"

namespace bundle
{

constexpr std::size_t k_alignment = 128u;
constexpr std::uint64_t k_version = 1u;

enum class EValidationStatus : std::uint8_t
{
    success = 0u,
    invalid_buffer,
    document_validation_failed,
    invalid_schema,
    unsupported_version,
    invalid_name,
    invalid_extent,
    missing_terminator,
    allocation_failed
};

struct SValidationResult
{
    EValidationStatus status{ EValidationStatus::success };

    //  Offending value when available. Borrows identity from the input document;
    //  callers can traverse its parents to report a path while storage is alive.
    CBakedValueIndex value;
    [[nodiscard]] bool succeeded() const noexcept { return status == EValidationStatus::success; }
};

enum class ELookupStatus : std::uint8_t { success = 0u, invalid_path, not_found, not_file, not_ready };

struct SLookupResult
{
    ELookupStatus status{ ELookupStatus::not_ready };
    memory::CMemoryConstView bytes;
    CBakedValueIndex entry;
};

class CBundleView
{
public:
    //  Full validation, including the baked prefix. Failure clears this view.
    //  Input storage must remain alive and immutable for every borrowed view.
    [[nodiscard]] SValidationResult reset(const void* bytes, std::size_t size) noexcept;
    void clear() noexcept { m_document.clear(); m_bytes.reset(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_bytes.is_valid(); }
    [[nodiscard]] CBakedDocument document() const noexcept { return m_document; }
    [[nodiscard]] CByteConstView bytes() const noexcept
    {
        return CByteConstView{ static_cast<const std::uint8_t*>(m_bytes.data()), m_bytes.bytes(), k_alignment };
    }

    //  Strict relative paths, ASCII case-insensitive. Allocation-free, with no
    //  shared lookup state. Successful bytes exclude terminators and padding.
    [[nodiscard]] SLookupResult lookup(CStringView path) const noexcept;

private:
    CBakedDocument m_document;
    memory::CMemoryConstView m_bytes;
};

static_assert(std::is_trivially_copyable_v<CBundleView>);
static_assert(std::is_standard_layout_v<CBundleView>);
static_assert(sizeof(CBundleView) == sizeof(CBakedDocument) + sizeof(memory::CMemoryConstView));

//  Validate before transferring ownership. Failure preserves source, destination
//  and view; success replaces them without moving the bytes. Source and
//  destination must not be in use by borrowers. No separate bundle owner exists.
[[nodiscard]] SValidationResult adopt(CByteBuffer&& source, CBakedDocumentBlock& destination, CBundleView& view) noexcept;

}   // namespace bundle

#endif // BUNDLE_HPP_INCLUDED
