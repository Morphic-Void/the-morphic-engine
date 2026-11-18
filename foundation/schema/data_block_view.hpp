
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_block_view.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Checked borrowed access to a complete schema block and its reference context.

#pragma once

#include "schema/baked_data_block.hpp"
#include "schema/data_operation.hpp"

namespace schema
{

class CDataBlockView;
namespace detail { struct SDataViewOwner; }

enum class EDataEntryKind : std::uint8_t { instance, collection };

class CDataEntryHandle
{
public:
    [[nodiscard]] bool is_valid() const noexcept { return m_owner != nullptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return is_valid(); }

private:
    friend class CDataBlockView;
    const CDataBlockView* m_owner{};
    std::uint64_t m_generation{};
    std::uint32_t m_index{};
    EDataEntryKind m_kind{};
};

class CDataValueView
{
public:
    [[nodiscard]] bool is_valid() const noexcept { return m_owner != nullptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return is_valid(); }
    [[nodiscard]] CTypesIndex type() const noexcept { return m_type; }

private:
    friend class CDataBlockView;
    const CDataBlockView* m_owner{};
    std::uint64_t m_generation{};
    CTypesIndex m_type;
    const std::uint8_t* m_bytes{};
    std::size_t m_size{};
};

struct SDataEntryInfo
{
    CStringView type_name, name;
    CTypesIndex type;
    CDataEntryHandle parent;
    EDataEntryKind kind{};
    std::uint32_t count{};
    std::uint64_t stride{};
};

//  Stored text uses the document's canonical UTF-8 with C0 80 for logical NUL.
//  Use copy_text_utf8 for counted strict UTF-8 with literal zero bytes.
struct SDataTextView
{
    bool absent{};
    CStringView stored_text;
};

struct SDataRangeView
{
    CDataEntryHandle collection;
    CTypesIndex element_type;
    std::uint32_t first{}, count{};
    bool absent{};
};

class CDataBlockView
{
public:
    CDataBlockView() noexcept = default;
    ~CDataBlockView() noexcept;
    CDataBlockView(const CDataBlockView&) = delete;
    CDataBlockView& operator=(const CDataBlockView&) = delete;

    //  No backing acquisition. The caller preserves source bytes until clear or
    //  destruction. Binding owns only resolved/index metadata and validates all
    //  values, references, selections and cycles under the supplied limits.
    //  The allocator context active at bind must outlive the bound view.
    [[nodiscard]] bool bind(const CBakedDataBlock& source, const SDataOperationLimits limits,
        SDataOperationDiagnostic& diagnostic) noexcept;
    void clear() noexcept;
    [[nodiscard]] bool is_ready() const noexcept { return m_owner != nullptr; }
    [[nodiscard]] const CResolvedTypes* types() const noexcept;
    [[nodiscard]] const CBakedDocument* document() const noexcept;
    [[nodiscard]] CByteConstView file_bytes() const noexcept;
    [[nodiscard]] std::uint32_t entry_count(const EDataEntryKind kind) const noexcept;
    [[nodiscard]] CDataEntryHandle entry_at(const EDataEntryKind kind, const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] CDataEntryHandle find_collection(const CStringView& type, const CStringView& name) const noexcept;
    [[nodiscard]] CDataEntryHandle find_instance(const CStringView& type, const CStringView& name,
        const CDataEntryHandle parent = {}) const noexcept;
    //  Validate ownership and generation before returning the per-kind ordinal
    //  in this binding.
    //  Both outputs remain unchanged on failure.
    [[nodiscard]] bool entry_identity(const CDataEntryHandle handle, EDataEntryKind& kind,
        std::uint32_t& ordinal) const noexcept;
    [[nodiscard]] bool entry(const CDataEntryHandle handle, SDataEntryInfo& result) const noexcept;
    [[nodiscard]] bool value(const CDataEntryHandle handle, const std::uint32_t record, CDataValueView& result) const noexcept;
    [[nodiscard]] bool member(const CDataValueView& value, const CStringView& name, CDataValueView& result) const noexcept;
    [[nodiscard]] bool element(const CDataValueView& value, const std::uint32_t index, CDataValueView& result) const noexcept;
    [[nodiscard]] bool bytes(const CDataValueView& value, CByteConstView& result) const noexcept;
    [[nodiscard]] bool text(const CDataValueView& value, SDataTextView& result) const noexcept;
    [[nodiscard]] bool span(const CDataValueView& value, SDataRangeView& result) const noexcept;
    [[nodiscard]] bool element(const SDataRangeView& range, const std::uint32_t index, CDataValueView& result) const noexcept;
    [[nodiscard]] bool copy_text_utf8(const CDataValueView& value, CByteBuffer& output) const noexcept;
    [[nodiscard]] bool copy_text_utf8(const CDataValueView& value, const SDataOperationLimits limits,
        CByteBuffer& output, SDataOperationDiagnostic& diagnostic) const noexcept;

private:
    [[nodiscard]] bool contains(const CDataEntryHandle handle) const noexcept;
    [[nodiscard]] bool contains(const CDataValueView& value) const noexcept;
    [[nodiscard]] CDataValueView make_value(const CTypesIndex type, const std::uint8_t* const bytes,
        const std::size_t size) const noexcept;
    detail::SDataViewOwner* m_owner{};
    std::uint64_t m_generation{};
};

}   // namespace schema
