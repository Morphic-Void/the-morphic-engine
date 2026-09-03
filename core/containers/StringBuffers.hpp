
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   StringBuffers.hpp
//  Author: Ritchie Brannan
//  Date:   22 Feb 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Byte-string view, buffer, and stable string table utilities.
//
//  Models byte-sequence strings only. Does not validate encoding or
//  provide locale or Unicode-aware behaviour.
//
//  IMPORTANT TERMINOLOGY NOTE
//  --------------------------
//  Strings are treated as byte sequences with explicit length or
//  zero-terminated packed storage.
//
//  Parser workflows intentionally distinguish:
//      - no string present       : null storage
//      - empty string is present : non-null storage, zero logical length
//
//  The migrated bounded memory views cannot represent a non-null zero-count
//  range, so CStringView and CSimpleString retain that distinction by keeping
//  a one-byte backing view/token for zero-length present strings. That byte is
//  expected to be the terminator when the caller is using the parser-oriented
//  empty-string state.
//
//  CStringBuffer stores strings as:
//      0 + payload bytes + 0
//
//  Offsets refer to the first payload byte. Payload length is not
//  stored internally.
//
//  Stable string IDs are managed separately from payload storage.
//  ID value 0 is reserved as an invalid sentinel.
//
//  See docs/containers/StringBuffers.md for the full documentation.

#pragma once

#ifndef STRING_BUFFERS_HPP_INCLUDED
#define STRING_BUFFERS_HPP_INCLUDED

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint8_t
#include <cstring>      //  std::strlen, std::memcpy
#include <limits>       //  std::numeric_limits
#include <utility>      //  std::move

#include "algo/validate_permutations.hpp"
#include "memory/memory_policies.hpp"
#include "memory/memory_token.hpp"
#include "memory/memory_view.hpp"
#include "ByteBuffers.hpp"
#include "TPodVector.hpp"

#include "debug/macros.hpp"

//==============================================================================
//  Forward declarations
//==============================================================================

class CStringView;
class CSimpleString;
class CStringBuffer;
class CStableStrings;

//==============================================================================
//  CStringBase
//  Shared byte-string helper conversions and length helpers.
//==============================================================================

struct CStringBase
{
static inline char* cast_to_cstring(std::uint8_t* const string) noexcept { return reinterpret_cast<char* const>(string); }
static inline const char* cast_to_cstring(const std::uint8_t* const string) noexcept { return reinterpret_cast<const char* const>(string); }
static inline std::uint8_t* cast_to_string(char* const cstring) noexcept { return reinterpret_cast<std::uint8_t* const>(cstring); }
static inline const std::uint8_t* cast_to_string(const char* const cstring) noexcept { return reinterpret_cast<const std::uint8_t* const>(cstring); }
static inline std::size_t strz_length(const char* const cstring) noexcept { return (cstring != nullptr) ? std::strlen(cstring) : std::size_t{ 0 }; }
static inline std::size_t strz_length(const std::uint8_t* const string) noexcept { return strz_length(cast_to_cstring(string)); }
};

//==============================================================================
//  CStringView
//  Non-owning view over a byte string.
//
//  State model:
//  - Canonical empty is { m_string == nullptr, m_length == 0 }.
//  - Non-empty state stores a pointer to string bytes plus explicit length.
//  - Zero-termination is not required when length is provided explicitly.
//  - See the top-of-file parser-state note for the non-null zero-length case.
//==============================================================================

class CStringView : private CStringBase
{
public:

    //  Default lifetime and assignment
    CStringView() noexcept = default;
    CStringView(const CStringView&) noexcept = default;
    CStringView& operator=(const CStringView&) noexcept = default;
    ~CStringView() noexcept = default;

    //  Construction
    explicit CStringView(const char* const cstring) noexcept { set(cstring); }
    explicit CStringView(const std::uint8_t* const string) noexcept { set(string); }
    explicit CStringView(const char* const cstring, const std::size_t length) noexcept { set(cstring, length); }
    explicit CStringView(const std::uint8_t* const string, const std::size_t length) noexcept { set(string, length); }

    //  View state
    bool set(const char* const cstring) noexcept { return set(cast_to_string(cstring)); }
    bool set(const std::uint8_t* const string) noexcept;
    bool set(const char* const cstring, const std::size_t length) noexcept { return set(cast_to_string(cstring), length); }
    bool set(const std::uint8_t* const string, const std::size_t length) noexcept;
    void reset() noexcept { m_string.reset(); m_length = 0u; }

    //  Accessors
    const char* cstring() const noexcept { return cast_to_cstring(static_cast<const std::uint8_t*>(m_string.data())); }
    const std::uint8_t* string() const noexcept { return static_cast<const std::uint8_t*>(m_string.data()); }
    std::size_t length() const noexcept { return (m_string.data() != nullptr) ? m_length : 0u; }
    bool empty() const noexcept { return (m_string.data() == nullptr); } //  null only; see parser-state note above

    //  Relationship identity
    [[nodiscard]] std::int32_t relationship(const CStringView& other) const noexcept;
    [[nodiscard]] std::int32_t relationship(const CSimpleString& other) const noexcept;

    //  Comparators
    [[nodiscard]] bool operator==(const CStringView& other) const noexcept { return relationship(other) == 0; }
    [[nodiscard]] bool operator>=(const CStringView& other) const noexcept { return relationship(other) >= 0; }
    [[nodiscard]] bool operator<=(const CStringView& other) const noexcept { return relationship(other) <= 0; }
    [[nodiscard]] bool operator>(const CStringView& other) const noexcept { return relationship(other) > 0; }
    [[nodiscard]] bool operator<(const CStringView& other) const noexcept { return relationship(other) < 0; }
    [[nodiscard]] bool operator==(const CSimpleString& other) const noexcept { return relationship(other) == 0; }
    [[nodiscard]] bool operator>=(const CSimpleString& other) const noexcept { return relationship(other) >= 0; }
    [[nodiscard]] bool operator<=(const CSimpleString& other) const noexcept { return relationship(other) <= 0; }
    [[nodiscard]] bool operator>(const CSimpleString& other) const noexcept { return relationship(other) > 0; }
    [[nodiscard]] bool operator<(const CSimpleString& other) const noexcept { return relationship(other) < 0; }

private:
    std::int32_t private_compare(const CStringView& other) const noexcept;

    memory::CMemoryConstView m_string;
    std::size_t m_length = 0u;
};

//==============================================================================
//  CSimpleString
//  Owning byte string with immutable logical size.
//
//  State model:
//  - Canonical empty is { m_string == nullptr, m_length == 0 }.
//  - Non-empty state owns a zero-terminated byte string together with its
//    explicit payload length.
//  - Logical size and capacity are fixed by each successful set().
//  - See the top-of-file parser-state note for the non-null zero-length case.
//==============================================================================

class CSimpleString : private CStringBase
{
public:

    //  Default and deleted lifetime
    CSimpleString() noexcept = default;
    CSimpleString(const CSimpleString&) = delete;
    CSimpleString& operator=(const CSimpleString&) = delete;

    //  Move lifetime
    CSimpleString(CSimpleString&&) noexcept;
    CSimpleString& operator=(CSimpleString&&) noexcept;

    //  Construction and destruction
    explicit CSimpleString(const char* const cstring) noexcept { set(cstring); }
    explicit CSimpleString(const std::uint8_t* const string) noexcept { set(string); }
    explicit CSimpleString(const char* const cstring, const std::size_t length) noexcept { set(cstring, length); }
    explicit CSimpleString(const std::uint8_t* const string, const std::size_t length) noexcept { set(string, length); }
    ~CSimpleString() noexcept { deallocate(); }

    //  String state
    bool set(const char* const cstring) noexcept { return set(cast_to_string(cstring)); }
    bool set(const char* const cstring, const std::size_t length) noexcept { return set(cast_to_string(cstring), length); }
    bool set(const std::uint8_t* const string) noexcept;
    bool set(const std::uint8_t* const string, const std::size_t length) noexcept;

    //  Accessors
    [[nodiscard]] CStringView view() const noexcept { return CStringView{ string(), m_length }; }
    [[nodiscard]] const char* cstring() const noexcept { return cast_to_cstring(string()); }
    [[nodiscard]] const std::uint8_t* string() const noexcept { return static_cast<const std::uint8_t*>(m_string.data()); }
    [[nodiscard]] std::size_t length() const noexcept { return (m_string.data() != nullptr) ? m_length : 0u; }
    [[nodiscard]] bool is_empty() const noexcept { return (m_string.data() == nullptr); } //  null only; see parser-state note above

    //  Relationship identity
    [[nodiscard]] std::int32_t relationship(const CStringView& other) const noexcept { return view().relationship(other); }
    [[nodiscard]] std::int32_t relationship(const CSimpleString& other) const noexcept { return view().relationship(other); }

    //  Comparators
    [[nodiscard]] bool operator==(const CStringView& other) const noexcept { return relationship(other) == 0; }
    [[nodiscard]] bool operator>=(const CStringView& other) const noexcept { return relationship(other) >= 0; }
    [[nodiscard]] bool operator<=(const CStringView& other) const noexcept { return relationship(other) <= 0; }
    [[nodiscard]] bool operator>(const CStringView& other) const noexcept { return relationship(other) > 0; }
    [[nodiscard]] bool operator<(const CStringView& other) const noexcept { return relationship(other) < 0; }
    [[nodiscard]] bool operator==(const CSimpleString& other) const noexcept { return relationship(other) == 0; }
    [[nodiscard]] bool operator>=(const CSimpleString& other) const noexcept { return relationship(other) >= 0; }
    [[nodiscard]] bool operator<=(const CSimpleString& other) const noexcept { return relationship(other) <= 0; }
    [[nodiscard]] bool operator>(const CSimpleString& other) const noexcept { return relationship(other) > 0; }
    [[nodiscard]] bool operator<(const CSimpleString& other) const noexcept { return relationship(other) < 0; }

    //  Ownership
    void deallocate() noexcept;

    //  Direct storage attribution
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
    [[nodiscard]] bool can_reattribute_to(memory::CMemoryContext* const context = nullptr) const noexcept;
    [[nodiscard]] bool reattribute(memory::CMemoryContext* const context = nullptr) noexcept;

private:
    friend class CErasedOwner;

    [[nodiscard]] memory::CMemoryContext* memory_source_context() const noexcept;
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source,
        memory::CMemoryContext* const target) noexcept;

    bool private_allocate(const std::uint8_t* const string, const std::size_t length) noexcept;

    memory::CMemoryToken m_string{ 1u, 1u };
    std::size_t m_length = 0u;
};

//==============================================================================
//  CStringBuffer
//  Owning appendable packed byte-string buffer.
//
//  Representation model:
//  - Strings are stored in packed form as:
//      0 + payload bytes + 0
//  - Returned offsets refer to the first payload byte, not the prefixed
//    zero byte.
//  - Payload length is not stored internally.
//  - Strings with embedded terminators therefore require externally
//    associated length metadata.
//==============================================================================

class CStringBuffer : private CStringBase
{
public:

    //  Default and deleted lifetime
    CStringBuffer() noexcept = default;
    CStringBuffer(const CStringBuffer&) = delete;
    CStringBuffer& operator=(const CStringBuffer&) = delete;
    CStringBuffer(CStringBuffer&&) noexcept = default;
    CStringBuffer& operator=(CStringBuffer&&) noexcept = default;

    //  Lifetime
    explicit CStringBuffer(const std::size_t size) noexcept { (void)reserve(size); }
    ~CStringBuffer() noexcept { deallocate(); }

    //  Append helpers
    [[nodiscard]] std::size_t operator+=(const char* const cstring) noexcept { return append(cstring); }
    [[nodiscard]] std::size_t operator+=(const std::uint8_t* const string) noexcept { return append(string); }

    //  Storage accessors
    [[nodiscard]] const std::uint8_t* data() const noexcept { return m_buffer.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_buffer.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_buffer.capacity(); }
    [[nodiscard]] std::size_t available() const noexcept { return m_buffer.available(); }
    [[nodiscard]] bool empty() const noexcept { return m_buffer.size() == 0u; }
    [[nodiscard]] bool storage_overlaps(const std::uint8_t* bytes, const std::size_t size) const noexcept;

    //  String accessors
    [[nodiscard]] CStringView view(const std::size_t offset) const noexcept { return CStringView(string(offset)); }
    [[nodiscard]] const char* cstring(const std::size_t offset) const noexcept { return cast_to_cstring(string(offset)); }
    [[nodiscard]] const std::uint8_t* string(const std::size_t offset) const noexcept;

    //  Offset validity checker for packed string heads (checks non-zero, in-range offset with prefixed zero byte)
    [[nodiscard]] bool is_valid_offset(const std::size_t offset) const noexcept;

    //  String appending
    [[nodiscard]] std::size_t append(const char* const cstring) noexcept { return append(cast_to_string(cstring)); }
    [[nodiscard]] std::size_t append(const char* const cstring, const std::size_t length) noexcept { return append(cast_to_string(cstring), length); }
    [[nodiscard]] std::size_t append(const std::uint8_t* const string) noexcept;
    [[nodiscard]] std::size_t append(const std::uint8_t* const string, const std::size_t length) noexcept;
    [[nodiscard]] std::size_t append(const CStringView& view) noexcept { return append(view.string(), view.length()); }
    [[nodiscard]] std::size_t append(const CSimpleString& string) noexcept { return append(string.view()); }

    //  Capacity management (state unchanged on failure)
    [[nodiscard]] bool reserve(const std::size_t minimum_capacity) noexcept;
    [[nodiscard]] bool reserve_exact(const std::size_t exact_capacity) noexcept;
    [[nodiscard]] bool ensure_free(const std::size_t length) noexcept;
    [[nodiscard]] bool shrink_to_fit() noexcept { return m_buffer.shrink_to_fit(); }
    void deallocate() noexcept { m_buffer.deallocate(); }

    //  Invariant and validity checking
    [[nodiscard]] bool check_invariants() const noexcept;
    [[nodiscard]] bool assert_invariants() const noexcept;

    //  Invalid offset return value
    static constexpr std::size_t k_invalid_offset = 0u;

    //  Direct storage attribution
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
    [[nodiscard]] bool can_reattribute_to(memory::CMemoryContext* const context = nullptr) const noexcept;
    [[nodiscard]] bool reattribute(memory::CMemoryContext* const context = nullptr) noexcept;

private:
    friend class CStableStrings;

    [[nodiscard]] memory::CMemoryContext* memory_source_context() const noexcept;
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

    [[nodiscard]] std::size_t private_append(const std::uint8_t* const string, const std::size_t length) noexcept;

    CByteBuffer m_buffer;
};

//==============================================================================
//  CStableStrings
//  Owning appendable stable-ID byte-string table.
//
//  Storage model:
//  - String payload bytes are stored in a CStringBuffer.
//  - Each logical string also has a StringRef recording payload offset and
//    explicit length.
//  - Stable IDs, ref-indices, and lexical order are maintained by side
//    tables.
//  - ID value 0 is reserved as an invalid sentinel.
//==============================================================================

class CStableStrings : private CStringBase
{
public:

    //  Default and deleted lifetime
    CStableStrings() noexcept = default;
    CStableStrings(const CStableStrings&) = delete;
    CStableStrings& operator=(const CStableStrings&) = delete;
    CStableStrings(CStableStrings&&) noexcept = default;
    CStableStrings& operator=(CStableStrings&&) noexcept = default;

    //  Lifetime
    explicit CStableStrings(const std::size_t string_count, const std::size_t string_storage_size) noexcept { (void)initialise(string_count, string_storage_size); }
    ~CStableStrings() noexcept { deallocate(); }

    //  Append helpers
    [[nodiscard]] std::size_t operator+=(const char* const cstring) noexcept { return append(cstring); }
    [[nodiscard]] std::size_t operator+=(const std::uint8_t* const string) noexcept { return append(string); }

    //  Accessors
    [[nodiscard]] CStringView view(const std::size_t id) const noexcept;
    [[nodiscard]] bool is_valid_id(const std::size_t id) const noexcept;
    [[nodiscard]] bool storage_overlaps(const std::uint8_t* bytes, const std::size_t size) const noexcept;

    //  Index conversions
    //
    //  Notes:
    //    id, ref_index and rank reserve the value 0 for internal use.
    //    A return value of 0 therefore indicates failure.
    //
    //    After sort(), and until new strings are appended:
    //        ref_index == rank
    //
    //    Lookup complexity:
    //      id_to_ref_index      : O(1)
    //      ref_index_to_id      : O(1)
    //      rank_to_ref_index    : O(1)
    //      ref_index_to_rank    : O(N)  (linear search)
    [[nodiscard]] std::size_t id_to_ref_index(const std::size_t id) const noexcept;
    [[nodiscard]] std::size_t ref_index_to_id(const std::size_t ref_index) const noexcept;
    [[nodiscard]] std::size_t rank_to_ref_index(const std::size_t rank) const noexcept;
    [[nodiscard]] std::size_t ref_index_to_rank(const std::size_t ref_index) const noexcept;

    //  String id queries
    [[nodiscard]] std::size_t find_id(const char* const cstring) noexcept { return find_id(cast_to_string(cstring)); }
    [[nodiscard]] std::size_t find_id(const char* const cstring, const std::size_t length) noexcept { return find_id(cast_to_string(cstring), length); }
    [[nodiscard]] std::size_t find_id(const std::uint8_t* const string) noexcept;
    [[nodiscard]] std::size_t find_id(const std::uint8_t* const string, const std::size_t length) noexcept;

    //  String appending
    [[nodiscard]] std::size_t append(const char* const cstring) noexcept { return append(cast_to_string(cstring)); }
    [[nodiscard]] std::size_t append(const char* const cstring, const std::size_t length) noexcept { return append(cast_to_string(cstring), length); }
    [[nodiscard]] std::size_t append(const std::uint8_t* const string) noexcept;
    [[nodiscard]] std::size_t append(const std::uint8_t* const string, const std::size_t length) noexcept;

    //  Storage order management
    [[nodiscard]] bool sort() noexcept;

    //  Capacity management
    [[nodiscard]] bool initialise(const std::size_t string_count, const std::size_t string_storage_size) noexcept;
    [[nodiscard]] bool ensure_free(const std::size_t length) noexcept;
    [[nodiscard]] bool shrink_to_fit() noexcept;
    void deallocate() noexcept;

    //  Integrity checking
    [[nodiscard]] bool check_integrity(const bool check_lexical_order = true) const noexcept;

    //  Invalid return values
    static constexpr std::size_t k_invalid_id = 0u;
    static constexpr std::size_t k_invalid_ref_index = 0u;
    static constexpr std::size_t k_invalid_offset = 0u;
    static constexpr std::size_t k_invalid_rank = 0u;

    //  Direct storage attribution
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
    [[nodiscard]] bool can_reattribute_to(memory::CMemoryContext* context = nullptr) const noexcept;
    [[nodiscard]] bool reattribute(memory::CMemoryContext* context = nullptr) noexcept;

private:
    [[nodiscard]] bool memory_source_context(memory::CMemoryContext*& source) const noexcept;
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

    std::size_t private_find_ref_index(const std::uint8_t* const string, const std::size_t length, std::size_t& insert_at) noexcept;
    std::size_t private_find_id(const std::uint8_t* const string, const std::size_t length) noexcept;
    std::size_t private_append(const std::uint8_t* const string, const std::size_t length) noexcept;

    static inline [[nodiscard]] bool failed_integrity_check() noexcept;

    struct StringRef
    {
        std::size_t offset = 0u;
        std::size_t length = 0u;
    };

    static constexpr std::size_t k_max_elements = memory::t_max_elements<StringRef>();

    CStringBuffer m_string_buffer;
    TPodVector<StringRef> m_string_refs;
    TPodVector<std::size_t> m_ref_index_to_id;
    TPodVector<std::size_t> m_id_to_ref_index;
    TPodVector<std::size_t> m_sorted_ref_indices;
};

//==============================================================================
//  CStringView out of class function bodies
//==============================================================================

inline bool CStringView::set(const std::uint8_t* const string) noexcept
{
    reset();
    if (string != nullptr)
    {
        m_length = strz_length(string);
        const std::size_t storage_count = m_length + 1u;
        if (!m_string.set(string, storage_count, 1u, 1u))
        {
            reset();
        }
    }
    return m_string.data() != nullptr;
}

inline bool CStringView::set(const std::uint8_t* const string, const std::size_t length) noexcept
{
    reset();
    if (string != nullptr)
    {
        m_length = length;
        //  Preserve a non-null zero-length string as a distinct parser state.
        const std::size_t storage_count = (length != 0u) ? length : 1u;
        if (!m_string.set(string, storage_count, 1u, 1u))
        {
            reset();
        }
    }
    return m_string.data() != nullptr;
}

[[nodiscard]] inline std::int32_t CStringView::relationship(const CStringView& other) const noexcept
{
    return private_compare(other);
}

[[nodiscard]] inline std::int32_t CStringView::relationship(const CSimpleString& other) const noexcept
{
    return relationship(other.view());
}

[[nodiscard]] inline std::int32_t CStringView::private_compare(const CStringView& other) const noexcept
{
    const std::uint8_t* const a = string();
    const std::uint8_t* const b = other.string();
    if ((a != nullptr) && (b != nullptr))
    {
        std::size_t count = (m_length <= other.m_length) ? m_length : other.m_length;
        for (std::size_t index = 0; index < count; ++index)
        {
            if (a[index] != b[index])
            {
                return (a[index] > b[index]) ? 1 : -1;
            }
        }
    }
    return (m_length == other.m_length) ? 0 : ((m_length > other.m_length) ? 1 : -1);
}

//==============================================================================
//  CSimpleString out of class function bodies
//==============================================================================

inline CSimpleString::CSimpleString(CSimpleString&& src) noexcept
{
    m_string = std::move(src.m_string);
    m_length = src.m_length;
    src.m_length = 0u;
}

inline CSimpleString& CSimpleString::operator=(CSimpleString&& src) noexcept
{
    if (this != &src)
    {
        m_string = std::move(src.m_string);
        m_length = src.m_length;
        src.m_length = 0u;
    }
    return *this;
}

inline bool CSimpleString::set(const std::uint8_t* const string) noexcept
{
    deallocate();
    return (string != nullptr) ? private_allocate(string, strz_length(string)) : false;
}

inline bool CSimpleString::set(const std::uint8_t* const string, const std::size_t length) noexcept
{
    deallocate();
    return (string != nullptr) ? private_allocate(string, length) : false;
}

inline void CSimpleString::deallocate() noexcept
{
    m_string.deallocate();
    m_length = 0u;
}

inline memory::CMemoryContext* CSimpleString::memory_source_context() const noexcept
{
    return m_string.owns_storage() ? m_string.context() : nullptr;
}

inline void CSimpleString::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    m_string.unsafe_replace_context_without_accounting(expected_source, target);
}

inline std::uint32_t CSimpleString::memory_token_count() const noexcept
{
    return m_string.memory_token_count();
}

inline std::uint32_t CSimpleString::memory_allocation_count() const noexcept
{
    return m_string.memory_allocation_count();
}

inline std::uint64_t CSimpleString::memory_allocation_size() const noexcept
{
    return m_string.memory_allocation_size();
}

inline bool CSimpleString::can_reattribute_to(memory::CMemoryContext* const context) const noexcept
{
    return m_string.can_reattribute_to(context);
}

inline bool CSimpleString::reattribute(memory::CMemoryContext* const context) noexcept
{
    return m_string.reattribute(context);
}

inline bool CSimpleString::private_allocate(const std::uint8_t* const string, const std::size_t length) noexcept
{
    if ((string != nullptr) && (length < memory::k_byte_size_ceiling))
    {
        if (m_string.allocate(length + 1u, false))
        {
            std::uint8_t* const dst = static_cast<std::uint8_t*>(m_string.data());
            std::memcpy(dst, string, length);
            dst[length] = 0u;
            m_length = length;
            return true;
        }
    }
    return false;
}

//==============================================================================
//  CStringBuffer out of class function bodies
//==============================================================================

[[nodiscard]] inline const std::uint8_t* CStringBuffer::string(const std::size_t offset) const noexcept
{
    const std::uint8_t* const base = m_buffer.data();
    return ((base != nullptr) && (offset > 0u) && (offset < m_buffer.size()) && (base[offset - 1u] == 0u)) ? (base + offset) : nullptr;
}

[[nodiscard]] inline bool CStringBuffer::is_valid_offset(const std::size_t offset) const noexcept
{
    const std::uint8_t* const base = m_buffer.data();
    return (base != nullptr) && (offset > 0u) && (offset < m_buffer.size()) && (base[offset - 1u] == 0u);
}

[[nodiscard]] inline bool CStringBuffer::storage_overlaps(const std::uint8_t* const bytes, const std::size_t length) const noexcept
{
    const std::uint8_t* const storage = m_buffer.data();
    const std::size_t storage_size = m_buffer.size();
    if ((bytes == nullptr) || (length == 0u) || (storage == nullptr) || (storage_size == 0u))
    {
        return false;
    }

    const std::uintptr_t bytes_address = reinterpret_cast<std::uintptr_t>(bytes);
    const std::uintptr_t storage_address = reinterpret_cast<std::uintptr_t>(storage);
    return (bytes_address >= storage_address) ?
        ((bytes_address - storage_address) < storage_size) :
        ((storage_address - bytes_address) < length);
}

[[nodiscard]] inline std::size_t CStringBuffer::append(const std::uint8_t* const string) noexcept
{
    return (string != nullptr) ? private_append(string, strz_length(string)) : k_invalid_offset;
}

[[nodiscard]] inline std::size_t CStringBuffer::append(const std::uint8_t* const string, const std::size_t length) noexcept
{
    return (string != nullptr) ? private_append(string, length) : k_invalid_offset;
}

inline bool CStringBuffer::reserve(const std::size_t minimum_capacity) noexcept
{
    return (minimum_capacity <= m_buffer.capacity()) ? true : m_buffer.reserve(minimum_capacity);
}

inline bool CStringBuffer::reserve_exact(const std::size_t exact_capacity) noexcept
{
    if (exact_capacity < m_buffer.size())
    {
        return false;
    }
    return (exact_capacity == m_buffer.capacity()) ? true : m_buffer.reallocate(m_buffer.size(), exact_capacity);
}

inline bool CStringBuffer::ensure_free(const std::size_t length) noexcept
{
    return (length <= (memory::k_byte_size_ceiling - 2u)) ? m_buffer.ensure_free(length + 2u) : false;
}

[[nodiscard]] inline bool CStringBuffer::check_invariants() const noexcept
{
    if (!m_buffer.is_valid())
    {
        return false;
    }

    const std::size_t used = m_buffer.size();
    const std::size_t cap = m_buffer.capacity();

    if (used > cap)
    {
        return false;
    }

    if (used == 0u)
    {
        return true;
    }

    if (used < 2u)
    {
        return false;
    }

    const std::uint8_t* const base = m_buffer.data();
    if (base == nullptr)
    {
        return false;
    }

    if ((base[0] != 0u) || (base[used - 1u] != 0u))
    {
        return false;
    }

    return true;
}

[[nodiscard]] inline bool CStringBuffer::assert_invariants() const noexcept
{
    const bool valid = check_invariants();
    if (!valid)
    {
        MV_ERROR("CStringBuffer invariant check failed");
        //  Re-enter for debugger stepping on failure.
        (void)check_invariants();
    }
    return valid;
}

inline std::uint32_t CStringBuffer::memory_token_count() const noexcept
{
    return m_buffer.memory_token_count();
}

inline std::uint32_t CStringBuffer::memory_allocation_count() const noexcept
{
    return m_buffer.memory_allocation_count();
}

inline std::uint64_t CStringBuffer::memory_allocation_size() const noexcept
{
    return m_buffer.memory_allocation_size();
}

inline bool CStringBuffer::can_reattribute_to(memory::CMemoryContext* const context) const noexcept
{
    return m_buffer.can_reattribute_to(context);
}

inline bool CStringBuffer::reattribute(memory::CMemoryContext* const context) noexcept
{
    return m_buffer.reattribute(context);
}

inline memory::CMemoryContext* CStringBuffer::memory_source_context() const noexcept
{
    return m_buffer.memory_source_context();
}

inline void CStringBuffer::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    m_buffer.unsafe_replace_memory_context_without_accounting(expected_source, target);
}

[[nodiscard]] inline std::size_t CStringBuffer::private_append(const std::uint8_t* const string, const std::size_t length) noexcept
{
    if (!ensure_free(length))
    {
        return k_invalid_offset;
    }

    const std::size_t old_size = m_buffer.size();
    const std::size_t new_size = old_size + length + 2u;
    MV_ASSERT(new_size >= old_size);

    if (m_buffer.set_size(new_size))
    {
        std::uint8_t* const base = m_buffer.data();
        MV_ASSERT(base != nullptr);
        MV_ASSERT(new_size <= m_buffer.capacity());

        base[old_size] = 0u;
        std::memcpy((base + old_size + 1u), string, length);
        base[old_size + 1u + length] = 0u;

        MV_ASSERT(assert_invariants());
        return old_size + 1u;
    }

    return k_invalid_offset;
}

//==============================================================================
//  CStableStrings out of class function bodies
//==============================================================================

inline CStringView CStableStrings::view(const std::size_t id) const noexcept
{
    if (is_valid_id(id))
    {
        const std::size_t ref_index = m_id_to_ref_index[id];
        const StringRef& ref = m_string_refs[ref_index];
        return CStringView{ m_string_buffer.string(ref.offset), ref.length };
    }
    return CStringView{};
}

[[nodiscard]] inline bool CStableStrings::is_valid_id(const std::size_t id) const noexcept
{
    return (id != k_invalid_id) && (id < m_id_to_ref_index.size());
}

[[nodiscard]] inline bool CStableStrings::storage_overlaps(const std::uint8_t* const bytes, const std::size_t size) const noexcept
{
    return m_string_buffer.storage_overlaps(bytes, size);
}

[[nodiscard]] inline std::size_t CStableStrings::id_to_ref_index(const std::size_t id) const noexcept
{
    if ((id != k_invalid_id) && (id < m_id_to_ref_index.size()))
    {
        return m_id_to_ref_index[id];
    }
    return k_invalid_ref_index;
}

[[nodiscard]] inline std::size_t CStableStrings::ref_index_to_id(const std::size_t ref_index) const noexcept
{
    if ((ref_index != k_invalid_ref_index) && (ref_index < m_ref_index_to_id.size()))
    {
        return m_ref_index_to_id[ref_index];
    }
    return k_invalid_id;
}

[[nodiscard]] inline std::size_t CStableStrings::rank_to_ref_index(const std::size_t rank) const noexcept
{
    if ((rank != k_invalid_rank) && (rank < m_sorted_ref_indices.size()))
    {
        return m_sorted_ref_indices[rank];
    }
    return k_invalid_ref_index;
}

[[nodiscard]] inline std::size_t CStableStrings::ref_index_to_rank(const std::size_t ref_index) const noexcept
{
    if (ref_index != k_invalid_ref_index)
    {
        const std::size_t count = m_sorted_ref_indices.size();
        if (ref_index < count)
        {
            const std::size_t* const sorted_ref_indices_data = m_sorted_ref_indices.data();
            if (sorted_ref_indices_data[ref_index] == ref_index)
            {   //  this ref_index is already in its sorted location (O(1) for sorted data)
                return ref_index;
            }

            for (std::size_t rank = 1u; rank < count; ++rank)
            {
                if (sorted_ref_indices_data[rank] == ref_index)
                {
                    return rank;
                }
            }
        }
    }
    return k_invalid_rank;
}

[[nodiscard]] inline std::size_t CStableStrings::find_id(const std::uint8_t* const string) noexcept
{
    return (string != nullptr) ? private_find_id(string, strz_length(string)) : k_invalid_id;
}

[[nodiscard]] inline std::size_t CStableStrings::find_id(const std::uint8_t* const string, const std::size_t length) noexcept
{
    return (string != nullptr) ? private_find_id(string, length) : k_invalid_id;
}

[[nodiscard]] inline std::size_t CStableStrings::append(const std::uint8_t* const string) noexcept
{
    return (string != nullptr) ? private_append(string, strz_length(string)) : k_invalid_id;
}

[[nodiscard]] inline std::size_t CStableStrings::append(const std::uint8_t* const string, const std::size_t length) noexcept
{
    return (string != nullptr) ? private_append(string, length) : k_invalid_id;
}

inline bool CStableStrings::sort() noexcept
{
    bool success = true;

    if (m_string_refs.size() > 2u)
    {   //  there is something to sort
        MV_DEBUG_ONLY(check_integrity());

        CStringBuffer string_buffer;
        TPodVector<StringRef> string_refs;
        TPodVector<std::size_t> ref_index_to_id;
        TPodVector<std::size_t> id_to_ref_index;
        TPodVector<std::size_t> sorted_ref_indices;

        const std::size_t count = m_string_refs.size();

        if (success) success = string_buffer.reserve_exact(m_string_buffer.size());
        if (success) success = string_refs.allocate(count);
        if (success) success = ref_index_to_id.allocate(count);
        if (success) success = id_to_ref_index.allocate(count);
        if (success) success = sorted_ref_indices.allocate(count);

        if (success)
        {
            (void)string_refs.set_size(count);
            (void)ref_index_to_id.set_size(count);
            (void)id_to_ref_index.set_size(count);
            (void)sorted_ref_indices.set_size(count);

            string_refs[0] = StringRef{};
            ref_index_to_id[0] = k_invalid_id;
            id_to_ref_index[0] = k_invalid_ref_index;
            sorted_ref_indices[0] = k_invalid_ref_index;

            for (std::size_t index = 1u; index < count; ++index)
            {
                const std::size_t old_ref_index = m_sorted_ref_indices[index];
                const std::size_t id = m_ref_index_to_id[old_ref_index];
                const StringRef& old_ref = m_string_refs[old_ref_index];

                const std::size_t new_offset = string_buffer.append(m_string_buffer.string(old_ref.offset), old_ref.length);
                if (new_offset == CStringBuffer::k_invalid_offset)
                {
                    success = false;
                    break;
                }

                const StringRef new_ref{ new_offset, old_ref.length };
                const std::size_t new_ref_index = index;

                string_refs[new_ref_index] = new_ref;
                ref_index_to_id[new_ref_index] = id;
                id_to_ref_index[id] = new_ref_index;
                sorted_ref_indices[new_ref_index] = new_ref_index;
            }

            if (success)
            {
                m_string_buffer = std::move(string_buffer);
                m_string_refs = std::move(string_refs);
                m_ref_index_to_id = std::move(ref_index_to_id);
                m_id_to_ref_index = std::move(id_to_ref_index);
                m_sorted_ref_indices = std::move(sorted_ref_indices);
                MV_DEBUG_ONLY(check_integrity());
            }
        }
    }

    return success;
}

inline bool CStableStrings::initialise(const std::size_t string_count, const std::size_t string_storage_size) noexcept
{
    bool success = false;
    if ((string_count != 0) && (string_count < k_max_elements) &&
        memory::in_non_empty_range(string_storage_size, memory::k_byte_size_ceiling))
    {
        const std::size_t string_count_with_sentinel = string_count + 1u;
        success = true;

        if (success) success = m_string_buffer.reserve(string_storage_size);
        if (success) success = m_string_refs.reserve(string_count_with_sentinel);
        if (success) success = m_ref_index_to_id.reserve(string_count_with_sentinel);
        if (success) success = m_id_to_ref_index.reserve(string_count_with_sentinel);
        if (success) success = m_sorted_ref_indices.reserve(string_count_with_sentinel);

        if (success) success = m_string_refs.push_back(StringRef{});
        if (success) success = m_ref_index_to_id.push_back(k_invalid_id);
        if (success) success = m_id_to_ref_index.push_back(k_invalid_ref_index);
        if (success) success = m_sorted_ref_indices.push_back(k_invalid_ref_index);

        if (!success)
        {
            deallocate();
        }
    }

    if (!success)
    {
        MV_ERROR("CStableStrings::initialise failed");
    }
    return success;
}

inline bool CStableStrings::ensure_free(const std::size_t length) noexcept
{   //  Success guarantees that one subsequent append of payload length 'length'
    //  will succeed without reallocation, provided no intervening mutation
    //  consumes the reserved spare capacity.
    const bool success =
        m_string_buffer.ensure_free(length) &&
        m_string_refs.ensure_free(1u) &&
        m_ref_index_to_id.ensure_free(1u) &&
        m_id_to_ref_index.ensure_free(1u) &&
        m_sorted_ref_indices.ensure_free(1u);

    if (!success)
    {
        MV_ERROR("CStableStrings::ensure_free failed");
    }
    return success;
}

inline bool CStableStrings::shrink_to_fit() noexcept
{
    bool success = true;
    if (!m_string_buffer.shrink_to_fit()) success = false;
    if (!m_string_refs.shrink_to_fit()) success = false;
    if (!m_ref_index_to_id.shrink_to_fit()) success = false;
    if (!m_id_to_ref_index.shrink_to_fit()) success = false;
    if (!m_sorted_ref_indices.shrink_to_fit()) success = false;
    return success;
}

inline void CStableStrings::deallocate() noexcept
{
    m_string_buffer.deallocate();
    m_string_refs.deallocate();
    m_ref_index_to_id.deallocate();
    m_id_to_ref_index.deallocate();
    m_sorted_ref_indices.deallocate();
}

[[nodiscard]] inline bool CStableStrings::check_integrity(const bool check_lexical_order) const noexcept
{
    if (!m_string_buffer.check_invariants())
    {   //  CStringBuffer already checked and reported its own invariant failure style
        return failed_integrity_check();
    }

    if (!m_string_refs.is_ready() ||
        !m_ref_index_to_id.is_ready() ||
        !m_id_to_ref_index.is_ready() ||
        !m_sorted_ref_indices.is_ready())
    {
        return failed_integrity_check();
    }

    const std::size_t count = m_string_refs.size();

    if ((m_ref_index_to_id.size() != count) ||
        (m_id_to_ref_index.size() != count) ||
        (m_sorted_ref_indices.size() != count))
    {   //  all side containers must match size
        return failed_integrity_check();
    }

    if (count == 0u)
    {   //  fully empty stable table is valid
        return true;
    }

    //  fetch raw data pointers to avoid repeated observer side-effects during checks
    const std::uint8_t* const string_buffer_data = m_string_buffer.data();
    const std::size_t string_buffer_size = m_string_buffer.size();
    const StringRef* const string_refs_data = m_string_refs.data();
    const std::size_t* const ref_index_to_id_data = m_ref_index_to_id.data();
    const std::size_t* const id_to_ref_index_data = m_id_to_ref_index.data();
    const std::size_t* const sorted_ref_indices_data = m_sorted_ref_indices.data();

    if ((string_refs_data == nullptr) ||
        (ref_index_to_id_data == nullptr) ||
        (id_to_ref_index_data == nullptr) ||
        (sorted_ref_indices_data == nullptr))
    {
        return failed_integrity_check();
    }

    //  check the trivial invariants for the default invalid sentinel slot
    if ((string_refs_data[0].offset != 0u) || (string_refs_data[0].length != 0u))
    {
        return failed_integrity_check();
    }
    if ((ref_index_to_id_data[0] != k_invalid_id) ||
        (id_to_ref_index_data[0] != k_invalid_ref_index) ||
        (sorted_ref_indices_data[0] != k_invalid_ref_index))
    {
        return failed_integrity_check();
    }

    //  validate the bijection of ref_index_to_id and id_to_ref_index
    for (std::size_t index = 1u; index < count; ++index)
    {
        const std::size_t id = ref_index_to_id_data[index];
        const std::size_t ref_index = id_to_ref_index_data[index];

        if ((id == k_invalid_id) || (id >= count) || (ref_index == k_invalid_ref_index) || (ref_index >= count))
        {
            return failed_integrity_check();
        }

        if ((id_to_ref_index_data[id] != index) || (ref_index_to_id_data[ref_index] != index))
        {
            return failed_integrity_check();
        }
    }

    //  validate string references against the new CStringBuffer layout
    for (std::size_t ref_index = 1u; ref_index < count; ++ref_index)
    {
        const StringRef& ref = string_refs_data[ref_index];

        if ((ref.offset == k_invalid_offset) || (ref.offset >= string_buffer_size))
        {
            return failed_integrity_check();
        }

        //  is_valid_offset() covers:
        //      - offset != 0
        //      - offset < buffer size
        //      - prefix zero at offset - 1
        if (!m_string_buffer.is_valid_offset(ref.offset))
        {
            return failed_integrity_check();
        }

        if (ref.length > (string_buffer_size - ref.offset - 1u))
        {
            return failed_integrity_check();
        }

        if (string_buffer_data[ref.offset + ref.length] != 0u)
        {
            return failed_integrity_check();
        }
    }

    //  validate the range and permutation of sorted_ref_indices
    if (!algo::validate_extracted_permutation(
        sorted_ref_indices_data, static_cast<std::uint32_t>(count), [](const std::size_t value) noexcept { return value; }))
    {
        return failed_integrity_check();
    }

    //  validate lexical ordering if requested
    if (check_lexical_order && (count > 2u))
    {
        const std::size_t initial_ref_index = sorted_ref_indices_data[1u];
        const StringRef& initial_ref = string_refs_data[initial_ref_index];
        CStringView prev_view{ (string_buffer_data + initial_ref.offset), initial_ref.length };

        for (std::size_t index = 2u; index < count; ++index)
        {
            const std::size_t ref_index = sorted_ref_indices_data[index];
            const StringRef& ref = string_refs_data[ref_index];
            const CStringView curr_view{ (string_buffer_data + ref.offset), ref.length };

            if (!(prev_view < curr_view))
            {
                return failed_integrity_check();
            }

            prev_view = curr_view;
        }
    }

    return true;
}

inline std::uint32_t CStableStrings::memory_token_count() const noexcept
{
    return m_string_buffer.memory_token_count() +
        m_string_refs.memory_token_count() +
        m_ref_index_to_id.memory_token_count() +
        m_id_to_ref_index.memory_token_count() +
        m_sorted_ref_indices.memory_token_count();
}

inline std::uint32_t CStableStrings::memory_allocation_count() const noexcept
{
    return m_string_buffer.memory_allocation_count() +
        m_string_refs.memory_allocation_count() +
        m_ref_index_to_id.memory_allocation_count() +
        m_id_to_ref_index.memory_allocation_count() +
        m_sorted_ref_indices.memory_allocation_count();
}

inline std::uint64_t CStableStrings::memory_allocation_size() const noexcept
{
    return m_string_buffer.memory_allocation_size() +
        m_string_refs.memory_allocation_size() +
        m_ref_index_to_id.memory_allocation_size() +
        m_id_to_ref_index.memory_allocation_size() +
        m_sorted_ref_indices.memory_allocation_size();
}

inline bool CStableStrings::can_reattribute_to(memory::CMemoryContext* target) const noexcept
{
    target = (target != nullptr) ? target : memory::get_ambient_memory_context();
    memory::CMemoryContext* source = nullptr;
    return (target != nullptr) && memory_source_context(source) &&
        m_string_buffer.can_reattribute_to(target) &&
        m_string_refs.can_reattribute_to(target) &&
        m_ref_index_to_id.can_reattribute_to(target) &&
        m_id_to_ref_index.can_reattribute_to(target) &&
        m_sorted_ref_indices.can_reattribute_to(target);
}

inline bool CStableStrings::reattribute(memory::CMemoryContext* target) noexcept
{
    target = (target != nullptr) ? target : memory::get_ambient_memory_context();
    memory::CMemoryContext* source = nullptr;
    if ((target == nullptr) || !memory_source_context(source) ||
        !m_string_buffer.can_reattribute_to(target) ||
        !m_string_refs.can_reattribute_to(target) ||
        !m_ref_index_to_id.can_reattribute_to(target) ||
        !m_id_to_ref_index.can_reattribute_to(target) ||
        !m_sorted_ref_indices.can_reattribute_to(target))
    {
        return false;
    }

    const std::uint32_t allocation_count = memory_allocation_count();
    const std::uint64_t allocation_size = memory_allocation_size();
    if ((source != nullptr) && (source != target) &&
        !memory::reattribute(*source, *target, allocation_count, allocation_size))
    {
        return false;
    }

    unsafe_replace_memory_context_without_accounting(source, target);
    return true;
}

inline bool CStableStrings::memory_source_context(memory::CMemoryContext*& source) const noexcept
{
    source = m_string_buffer.memory_source_context();

    memory::CMemoryContext* context = m_string_refs.memory_source_context();
    if ((source != nullptr) && (context != nullptr) && (context != source))
    {
        return false;
    }
    if (source == nullptr)
    {
        source = context;
    }

    context = m_ref_index_to_id.memory_source_context();
    if ((source != nullptr) && (context != nullptr) && (context != source))
    {
        return false;
    }
    if (source == nullptr)
    {
        source = context;
    }

    context = m_id_to_ref_index.memory_source_context();
    if ((source != nullptr) && (context != nullptr) && (context != source))
    {
        return false;
    }
    if (source == nullptr)
    {
        source = context;
    }

    context = m_sorted_ref_indices.memory_source_context();
    if ((source != nullptr) && (context != nullptr) && (context != source))
    {
        return false;
    }
    if (source == nullptr)
    {
        source = context;
    }
    return true;
}

inline void CStableStrings::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    m_string_buffer.unsafe_replace_memory_context_without_accounting(expected_source, target);
    m_string_refs.unsafe_replace_memory_context_without_accounting(expected_source, target);
    m_ref_index_to_id.unsafe_replace_memory_context_without_accounting(expected_source, target);
    m_id_to_ref_index.unsafe_replace_memory_context_without_accounting(expected_source, target);
    m_sorted_ref_indices.unsafe_replace_memory_context_without_accounting(expected_source, target);
}

inline std::size_t CStableStrings::private_find_ref_index(const std::uint8_t* const string, const std::size_t length, std::size_t& insert_at) noexcept
{
    insert_at = 0u;

    const CStringView view{ string, length };
    std::size_t lower = 1u;
    std::size_t upper = m_sorted_ref_indices.size();

    while (lower < upper)
    {
        const std::size_t pivot = (lower + upper) >> 1u;
        const std::size_t ref_index = m_sorted_ref_indices[pivot];
        const StringRef& ref = m_string_refs[ref_index];
        const std::int32_t relationship = view.relationship(CStringView{ m_string_buffer.string(ref.offset), ref.length });

        if (relationship == 0)
        {
            return ref_index;
        }
        if (relationship < 0)
        {
            upper = pivot;
        }
        else
        {
            lower = pivot + 1u;
        }
    }

    insert_at = lower;
    return k_invalid_ref_index;
}

inline std::size_t CStableStrings::private_find_id(const std::uint8_t* const string, const std::size_t length) noexcept
{
    std::size_t id = k_invalid_id;

    if (m_string_refs.size() > 1u)
    {
        std::size_t insert_at = 0u;
        const std::size_t ref_index = private_find_ref_index(string, length, insert_at);
        if (ref_index != k_invalid_ref_index)
        {
            id = m_ref_index_to_id[ref_index];
        }
    }

    return id;
}

inline std::size_t CStableStrings::private_append(const std::uint8_t* const string, const std::size_t length) noexcept
{
    std::size_t id = k_invalid_id;

    if (m_string_refs.size() != 0u)
    {
        std::size_t insert_at = 0u;
        const std::size_t ref_index = private_find_ref_index(string, length, insert_at);
        if (ref_index != k_invalid_ref_index)
        {
            id = m_ref_index_to_id[ref_index];
        }
        else if (ensure_free(length))
        {   //  Guaranteed-commit path:
            MV_ASSERT((insert_at != k_invalid_ref_index) && (insert_at <= m_sorted_ref_indices.size()));

            id = m_string_refs.size();

            const std::size_t offset = m_string_buffer.append(string, length);
            MV_ASSERT(offset != CStringBuffer::k_invalid_offset);

            const bool ok_ref = m_string_refs.push_back(StringRef{ offset, length });
            const bool ok_ref_to_id = m_ref_index_to_id.push_back(id);
            const bool ok_id_to_ref = m_id_to_ref_index.push_back(id);
            const bool ok_sorted = m_sorted_ref_indices.insert(insert_at, id);

            MV_ASSERT(ok_ref && ok_ref_to_id && ok_id_to_ref && ok_sorted);
            MV_ASSERT(check_integrity());
        }
    }
    else if (initialise(
        memory::vector_growth_policy(1u, k_max_elements),
        memory::buffer_growth_policy(length + 2u, memory::k_byte_size_ceiling)))
    {
        id = 1u;

        const std::size_t offset = m_string_buffer.append(string, length);
        MV_ASSERT(offset != CStringBuffer::k_invalid_offset);

        const bool ok_ref = m_string_refs.push_back(StringRef{ offset, length });
        const bool ok_ref_to_id = m_ref_index_to_id.push_back(id);
        const bool ok_id_to_ref = m_id_to_ref_index.push_back(id);
        const bool ok_sorted = m_sorted_ref_indices.push_back(id);

        MV_ASSERT(ok_ref && ok_ref_to_id && ok_id_to_ref && ok_sorted);
        MV_ASSERT(check_integrity());
    }

    return id;
}

[[nodiscard]] inline bool CStableStrings::failed_integrity_check() noexcept
{
    MV_ERROR("CStableStrings integrity check failed");
    return false;
}

#endif  //  #ifndef STRING_BUFFERS_HPP_INCLUDED
