
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    system_ids.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Jul 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Fixed encoded system id spaces for closed-system use across DLLs.
//
//  Defines:
//  - 32-bit encoded type ids;
//  - 64-bit encoded runtime ids for mounting points, modules, threads,
//    and combined systems;
//  - explicit strong index and id wrappers for those runtime identities;
//  - generated numeric constants for those runtime identities.
//
//  Common helper surface by domain:
//
//  - type_ids:
//      is_valid_index(), encode_index(), encode_id(), decode_id(), is_valid_id()
//  - mount_point_ids:
//      make_index(), make_id(), decode_id(), is_valid_index(), is_valid_id()
//  - thread_ids:
//      make_index(), make_id(), decode_id(), is_valid_index(), is_valid_id()
//  - module_ids:
//      make_index(), make_id(mount_point_id, module_index), decode_id(),
//      get_mount_point_id(), get_mount_point_index(),
//      is_valid_index(), is_valid_id()
//  - system_ids:
//      make_system_id(module_id, thread_id), get_module_id(), get_thread_id(),
//      get_mount_point_id(), get_mount_point_index(), is_valid_id()
//
//  This file defines ids and id-handling helpers only. It does not
//  bind ids to C++ payload types or provide system-name authority.

#pragma once

#ifndef SYSTEM_IDS_HPP_INCLUDED
#define SYSTEM_IDS_HPP_INCLUDED

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::int32_t, std::uint32_t, std::uint64_t
#include <limits>       //  std::numeric_limits
#include <type_traits>  //  std::is_standard_layout_v, std::is_trivially_copyable_v

#include "bit_utils/bit_ops.hpp"

//==============================================================================
//  Helper utility for id handling
//==============================================================================

namespace system_id_util
{

template<typename Tag, typename Repr>
class TValue
{
public:
    using repr_type = Repr;

    constexpr TValue() noexcept = default;
    explicit constexpr TValue(const Repr value) noexcept : m_value(value) {}

    [[nodiscard]] constexpr Repr raw_value() const noexcept { return m_value; }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_value != Repr{ 0 }; }
    explicit constexpr operator bool() const noexcept { return is_valid(); }

private:
    Repr m_value{ 0 };
};

template<typename Tag, typename Repr>
class TIndexValue
{
public:
    using repr_type = Repr;
    static constexpr Repr k_invalid_value = std::numeric_limits<Repr>::max();

    constexpr TIndexValue() noexcept = default;
    explicit constexpr TIndexValue(const Repr value) noexcept : m_value(value) {}

    [[nodiscard]] constexpr Repr raw_value() const noexcept { return m_value; }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_value != k_invalid_value; }
    explicit constexpr operator bool() const noexcept { return is_valid(); }

private:
    Repr m_value{ k_invalid_value };
};

template<typename Tag, typename Repr>
constexpr bool operator==(const TValue<Tag, Repr> lhs, const TValue<Tag, Repr> rhs) noexcept
{
    return lhs.raw_value() == rhs.raw_value();
}

template<typename Tag, typename Repr>
constexpr bool operator!=(const TValue<Tag, Repr> lhs, const TValue<Tag, Repr> rhs) noexcept
{
    return !(lhs == rhs);
}

template<typename Tag, typename Repr>
constexpr bool operator==(const TIndexValue<Tag, Repr> lhs, const TIndexValue<Tag, Repr> rhs) noexcept
{
    return lhs.raw_value() == rhs.raw_value();
}

template<typename Tag, typename Repr>
constexpr bool operator!=(const TIndexValue<Tag, Repr> lhs, const TIndexValue<Tag, Repr> rhs) noexcept
{
    return !(lhs == rhs);
}

template<typename IdTag, typename IndexTag, typename Repr, Repr t_encoded_field_mask>
struct TEncodedField
{
    using id_type = TValue<IdTag, Repr>;
    using index_type = TIndexValue<IndexTag, Repr>;

    static constexpr Repr k_id_field_mask = t_encoded_field_mask;
    static constexpr Repr k_payload_mask = (Repr{ 1 } << bit_ops::count_set_bits(k_id_field_mask)) - 1u;
    static constexpr Repr k_capacity = k_payload_mask;
    static constexpr Repr k_invalid_id_mask = ~k_id_field_mask;
    static constexpr std::int32_t k_id_field_shift = bit_ops::lo_bit_index(k_id_field_mask);

    static constexpr index_type make_index(const Repr value) noexcept;

    static constexpr bool is_valid_index(const index_type index) noexcept;

    static constexpr bool is_valid_id(const id_type id) noexcept;

    static constexpr id_type make_id(const index_type index) noexcept;

    static constexpr index_type get_index(const id_type id) noexcept;
};

//==============================================================================
//  TEncodedField out of class function bodies
//==============================================================================

template<typename IdTag, typename IndexTag, typename Repr, Repr t_encoded_field_mask>
constexpr typename TEncodedField<IdTag, IndexTag, Repr, t_encoded_field_mask>::index_type
TEncodedField<IdTag, IndexTag, Repr, t_encoded_field_mask>::make_index(const Repr value) noexcept
{
    return (value < k_payload_mask) ? index_type(value) : index_type{};
}

template<typename IdTag, typename IndexTag, typename Repr, Repr t_encoded_field_mask>
constexpr bool TEncodedField<IdTag, IndexTag, Repr, t_encoded_field_mask>::is_valid_index(
    const index_type index) noexcept
{
    return index.is_valid() && (index.raw_value() < k_payload_mask);
}

template<typename IdTag, typename IndexTag, typename Repr, Repr t_encoded_field_mask>
constexpr bool TEncodedField<IdTag, IndexTag, Repr, t_encoded_field_mask>::is_valid_id(
    const id_type id) noexcept
{
    return id.is_valid() && ((id.raw_value() & k_invalid_id_mask) == 0u);
}

template<typename IdTag, typename IndexTag, typename Repr, Repr t_encoded_field_mask>
constexpr typename TEncodedField<IdTag, IndexTag, Repr, t_encoded_field_mask>::id_type
TEncodedField<IdTag, IndexTag, Repr, t_encoded_field_mask>::make_id(const index_type index) noexcept
{
    return is_valid_index(index)
        ? id_type(static_cast<Repr>(bit_ops::spread_to_even_bits(k_payload_mask - index.raw_value()) << k_id_field_shift))
        : id_type{ Repr{ 0 } };
}

template<typename IdTag, typename IndexTag, typename Repr, Repr t_encoded_field_mask>
constexpr typename TEncodedField<IdTag, IndexTag, Repr, t_encoded_field_mask>::index_type
TEncodedField<IdTag, IndexTag, Repr, t_encoded_field_mask>::get_index(const id_type id) noexcept
{
    return is_valid_id(id)
        ? index_type(static_cast<Repr>((k_payload_mask - bit_ops::pack_from_even_bits(id.raw_value() >> k_id_field_shift))))
        : index_type{};
}

template<std::uint64_t t_payload_mask, std::uint32_t t_payload_offset>
struct TEvenMask
{
    static constexpr std::uint64_t value = (bit_ops::spread_to_even_bits(t_payload_mask) << (t_payload_offset * 2u));
};

struct CSystemTypeIdTag {};
struct CLocalTypeIdTag {};
struct CMountPointIdTag {};
struct CMountPointIndexTag {};
struct CModuleIdTag {};
struct CModuleIndexTag {};
struct CThreadIdTag {};
struct CThreadIndexTag {};
struct CSystemIdTag {};

}   //  namespace system_id_util

//==============================================================================
//  ID field definitions
//==============================================================================

using system_type_id = system_id_util::TValue<system_id_util::CSystemTypeIdTag, std::uint32_t>;

namespace system_type_ids
{
using index_type = std::uint32_t;
static constexpr system_type_id undefined{ 0u };

namespace ops
{
static constexpr std::uint32_t k_encoded_payload_mask = 0x15555555u; //  15 alternating payload bits
static constexpr std::uint32_t k_system_type_flag = 0x40000000u;
static constexpr std::uint32_t k_id_field_mask = k_encoded_payload_mask | k_system_type_flag;
static constexpr std::uint32_t k_invalid_id_mask = ~k_id_field_mask;
static constexpr index_type k_category_bit = 0x00008000u;
static constexpr index_type k_ordinal_mask = 0x00007fffu;
static constexpr index_type k_max_ordinal = k_ordinal_mask - 1u;
static constexpr index_type k_capacity = k_ordinal_mask;
static constexpr index_type k_system_index_min = k_category_bit;
static constexpr index_type k_system_index_max = k_category_bit | k_max_ordinal;
static constexpr index_type k_tagged_index_mask = k_category_bit | k_ordinal_mask;
static constexpr index_type k_invalid_index = k_tagged_index_mask;
}   //  namespace ops
}

using local_type_id = system_id_util::TValue<system_id_util::CLocalTypeIdTag, std::uint32_t>;

namespace local_type_ids
{
using index_type = std::uint32_t;
static constexpr local_type_id undefined{ 0u };

namespace ops
{
static constexpr index_type k_ordinal_mask = system_type_ids::ops::k_ordinal_mask;
static constexpr index_type k_max_ordinal = system_type_ids::ops::k_max_ordinal;
static constexpr index_type k_capacity = system_type_ids::ops::k_capacity;
static constexpr index_type k_invalid_index = system_type_ids::ops::k_invalid_index;
static constexpr index_type k_local_index_min = 0u;
static constexpr index_type k_local_index_max = k_max_ordinal;
}   //  namespace ops
}

enum class ETypeIdCategory : std::uint8_t
{
    undefined = 0u,
    system,
    local
};

class type_id
{
public:
    constexpr type_id() noexcept = default;
    explicit constexpr type_id(const system_type_id id) noexcept;
    explicit constexpr type_id(const local_type_id id) noexcept;

    [[nodiscard]] constexpr std::uint32_t raw_value() const noexcept { return m_value; }
    [[nodiscard]] constexpr ETypeIdCategory category() const noexcept;
    [[nodiscard]] constexpr bool is_defined() const noexcept { return m_value != 0u; }
    [[nodiscard]] constexpr bool is_valid() const noexcept;
    [[nodiscard]] constexpr bool is_system() const noexcept;
    [[nodiscard]] constexpr bool is_local() const noexcept;
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }

    [[nodiscard]] constexpr bool try_system_type_id(system_type_id& out) const noexcept;
    [[nodiscard]] constexpr bool try_local_type_id(local_type_id& out) const noexcept;

private:
    std::uint32_t m_value{ 0u };
};

[[nodiscard]] constexpr bool operator==(const type_id lhs, const type_id rhs) noexcept
{
    return lhs.raw_value() == rhs.raw_value();
}

[[nodiscard]] constexpr bool operator!=(const type_id lhs, const type_id rhs) noexcept
{
    return !(lhs == rhs);
}

static_assert((sizeof(type_id) == sizeof(std::uint32_t)), "type_id must occupy exactly four bytes.");
static_assert((alignof(type_id) == alignof(std::uint32_t)), "type_id must retain four-byte alignment.");
static_assert(std::is_standard_layout_v<type_id>, "type_id must retain standard layout.");
static_assert(std::is_trivially_copyable_v<type_id>, "type_id must remain trivially copyable.");

namespace runtime_id_layout
{
static constexpr std::uint32_t k_thread_payload_bits = 16u;
static constexpr std::uint32_t k_mount_point_payload_bits = 6u;
static constexpr std::uint32_t k_module_payload_bits = 10u;

static constexpr std::uint32_t k_thread_payload_offset = 0u;
static constexpr std::uint32_t k_mount_point_payload_offset = k_thread_payload_offset + k_thread_payload_bits;
static constexpr std::uint32_t k_module_payload_offset = k_mount_point_payload_offset + k_mount_point_payload_bits;

static constexpr std::uint64_t k_thread_field_mask =
    system_id_util::TEvenMask<((std::uint64_t{ 1 } << k_thread_payload_bits) - 1u), k_thread_payload_offset>::value;
static constexpr std::uint64_t k_mount_point_field_mask =
    system_id_util::TEvenMask<((std::uint64_t{ 1 } << k_mount_point_payload_bits) - 1u), k_mount_point_payload_offset>::value;
static constexpr std::uint64_t k_module_field_mask =
    system_id_util::TEvenMask<((std::uint64_t{ 1 } << k_module_payload_bits) - 1u), k_module_payload_offset>::value;
}

namespace mount_point_ids
{
namespace ops
{
using field = system_id_util::TEncodedField<
    system_id_util::CMountPointIdTag,
    system_id_util::CMountPointIndexTag,
    std::uint64_t,
    runtime_id_layout::k_mount_point_field_mask>;
static constexpr std::uint64_t k_capacity = field::k_capacity;
}   //  namespace ops

using id_type = ops::field::id_type;
using index_type = ops::field::index_type;
}

namespace thread_ids
{
namespace ops
{
using field = system_id_util::TEncodedField<
    system_id_util::CThreadIdTag,
    system_id_util::CThreadIndexTag,
    std::uint64_t,
    runtime_id_layout::k_thread_field_mask>;
static constexpr std::uint64_t k_capacity = field::k_capacity;
}   //  namespace ops

using id_type = ops::field::id_type;
using index_type = ops::field::index_type;
}

namespace module_ids
{
using id_type = system_id_util::TValue<system_id_util::CModuleIdTag, std::uint64_t>;
using index_type = system_id_util::TIndexValue<system_id_util::CModuleIndexTag, std::uint64_t>;

namespace ops
{
static constexpr std::uint64_t k_mount_point_id_mask = runtime_id_layout::k_mount_point_field_mask;
static constexpr std::uint64_t k_module_id_mask = runtime_id_layout::k_module_field_mask;
static constexpr std::uint64_t k_id_field_mask = k_mount_point_id_mask | k_module_id_mask;
static constexpr std::uint64_t k_invalid_id_mask = ~k_id_field_mask;
static constexpr std::uint64_t k_payload_mask = (std::uint64_t{ 1 } << runtime_id_layout::k_module_payload_bits) - 1u;
static constexpr std::uint64_t k_capacity = k_payload_mask;
static constexpr std::int32_t k_id_field_shift = bit_ops::lo_bit_index(k_module_id_mask);
}   //  namespace ops
}

namespace system_ids
{
using id_type = system_id_util::TValue<system_id_util::CSystemIdTag, std::uint64_t>;

namespace ops
{
static constexpr std::uint64_t k_module_id_mask = module_ids::ops::k_id_field_mask;
static constexpr std::uint64_t k_thread_id_mask = runtime_id_layout::k_thread_field_mask;
static constexpr std::uint64_t k_invalid_id_mask = ~(k_module_id_mask | k_thread_id_mask);
}   //  namespace ops
}

//==============================================================================
//  Type id helpers
//==============================================================================

namespace system_type_ids
{
namespace ops
{

//  Type-id states:
//  - undefined is the canonical zero value and has no type attribution;
//  - a valid id has a structurally valid non-zero encoding;
//  - a registered id additionally resolves through system_id_registry::find_type().
constexpr bool is_defined(const system_type_id id) noexcept
{
    return id != undefined;
}

constexpr bool is_valid_index(const index_type value) noexcept
{
    return
        ((value & ~k_tagged_index_mask) == 0u) &&
        ((value & k_category_bit) != 0u) &&
        ((value & k_ordinal_mask) <= k_max_ordinal);
}

constexpr bool is_valid_id(const system_type_id id) noexcept
{
    const std::uint32_t raw = id.raw_value();
    return is_defined(id) &&
        ((raw & k_invalid_id_mask) == 0u) &&
        ((raw & k_system_type_flag) != 0u) &&
        ((raw & k_encoded_payload_mask) != 0u);
}

constexpr index_type encode_index(const index_type value) noexcept
{
    return (value <= k_max_ordinal) ? (value | k_category_bit) : k_invalid_index;
}

constexpr index_type decode_index(const index_type value) noexcept
{
    return is_valid_index(value) ? (value & k_ordinal_mask) : k_invalid_index;
}

constexpr system_type_id encode_id(const index_type value) noexcept
{
    return is_valid_index(value)
        ? system_type_id(static_cast<std::uint32_t>(bit_ops::spread_to_even_bits(k_ordinal_mask - decode_index(value))) | k_system_type_flag)
        : undefined;
}

constexpr index_type decode_id(const system_type_id id) noexcept
{
    return is_valid_id(id)
        ? encode_index(static_cast<index_type>(k_ordinal_mask - bit_ops::pack_from_even_bits(id.raw_value() & k_encoded_payload_mask)))
        : k_invalid_index;
}

}   //  namespace ops
}   //  namespace system_type_ids

//==============================================================================
//  Local type id helpers
//==============================================================================

namespace local_type_ids
{
namespace ops
{

constexpr bool is_defined(const local_type_id id) noexcept
{
    return id != undefined;
}

constexpr bool is_valid_index(const index_type value) noexcept
{
    return
        ((value & ~system_type_ids::ops::k_tagged_index_mask) == 0u) &&
        ((value & system_type_ids::ops::k_category_bit) == 0u) &&
        (value <= k_max_ordinal);
}

constexpr bool is_valid_id(const local_type_id id) noexcept
{
    const std::uint32_t raw = id.raw_value();
    return is_defined(id) &&
        ((raw & system_type_ids::ops::k_invalid_id_mask) == 0u) &&
        ((raw & system_type_ids::ops::k_system_type_flag) == 0u) &&
        ((raw & system_type_ids::ops::k_encoded_payload_mask) != 0u);
}

constexpr index_type encode_index(const index_type value) noexcept
{
    return is_valid_index(value) ? value : k_invalid_index;
}

constexpr index_type decode_index(const index_type value) noexcept
{
    return is_valid_index(value) ? value : k_invalid_index;
}

constexpr local_type_id encode_id(const index_type value) noexcept
{
    return is_valid_index(value)
        ? local_type_id(static_cast<std::uint32_t>(bit_ops::spread_to_even_bits(k_ordinal_mask - decode_index(value))))
        : undefined;
}

constexpr index_type decode_id(const local_type_id id) noexcept
{
    return is_valid_id(id)
        ? encode_index(static_cast<index_type>(k_ordinal_mask - bit_ops::pack_from_even_bits(id.raw_value() & system_type_ids::ops::k_encoded_payload_mask)))
        : k_invalid_index;
}

}   //  namespace ops
}   //  namespace local_type_ids

//==============================================================================
//  Category-bearing type identity
//==============================================================================

constexpr type_id::type_id(const system_type_id id) noexcept : m_value(system_type_ids::ops::is_valid_id(id) ? id.raw_value() : 0u)
{
}

constexpr type_id::type_id(const local_type_id id) noexcept : m_value(local_type_ids::ops::is_valid_id(id) ? id.raw_value() : 0u)
{
}

constexpr ETypeIdCategory type_id::category() const noexcept
{
    if (system_type_ids::ops::is_valid_id(system_type_id{ m_value }))
    {
        return ETypeIdCategory::system;
    }
    if (local_type_ids::ops::is_valid_id(local_type_id{ m_value }))
    {
        return ETypeIdCategory::local;
    }
    return ETypeIdCategory::undefined;
}

constexpr bool type_id::is_valid() const noexcept
{
    return category() != ETypeIdCategory::undefined;
}

constexpr bool type_id::is_system() const noexcept
{
    return category() == ETypeIdCategory::system;
}

constexpr bool type_id::is_local() const noexcept
{
    return category() == ETypeIdCategory::local;
}

constexpr bool type_id::try_system_type_id(system_type_id& out) const noexcept
{
    out = system_type_ids::undefined;
    if (!is_system())
    {
        return false;
    }
    out = system_type_id{ m_value };
    return true;
}

constexpr bool type_id::try_local_type_id(local_type_id& out) const noexcept
{
    out = local_type_ids::undefined;
    if (!is_local())
    {
        return false;
    }
    out = local_type_id{ m_value };
    return true;
}

namespace type_ids
{

inline constexpr type_id undefined{};

[[nodiscard]] constexpr bool is_defined(const type_id id) noexcept { return id.is_defined(); }
[[nodiscard]] constexpr bool is_valid_id(const type_id id) noexcept { return id.is_valid(); }
[[nodiscard]] constexpr bool is_system(const type_id id) noexcept { return id.is_system(); }
[[nodiscard]] constexpr bool is_local(const type_id id) noexcept { return id.is_local(); }

}   //  namespace type_ids

//==============================================================================
//  Mount point id helpers
//==============================================================================

namespace mount_point_ids
{
namespace ops
{

constexpr index_type make_index(const std::uint64_t value) noexcept { return field::make_index(value); }
constexpr bool is_valid_index(const index_type index) noexcept { return field::is_valid_index(index); }
constexpr bool is_valid_id(const id_type id) noexcept { return field::is_valid_id(id); }
constexpr id_type make_id(const index_type index) noexcept { return field::make_id(index); }
constexpr index_type decode_id(const id_type id) noexcept { return field::get_index(id); }

}   //  namespace ops
}   //  namespace mount_point_ids

//==============================================================================
//  Thread id helpers
//==============================================================================

namespace thread_ids
{
namespace ops
{

constexpr index_type make_index(const std::uint64_t value) noexcept { return field::make_index(value); }
constexpr bool is_valid_index(const index_type index) noexcept { return field::is_valid_index(index); }
constexpr bool is_valid_id(const id_type id) noexcept { return field::is_valid_id(id); }
constexpr id_type make_id(const index_type index) noexcept { return field::make_id(index); }
constexpr index_type decode_id(const id_type id) noexcept { return field::get_index(id); }

}   //  namespace ops
}   //  namespace thread_ids

//==============================================================================
//  Module id helpers
//==============================================================================

namespace module_ids
{
namespace ops
{

constexpr index_type make_index(const std::uint64_t value) noexcept
{
    return (value < k_payload_mask) ? index_type(value) : index_type{};
}

constexpr bool is_valid_index(const index_type index) noexcept
{
    return index.is_valid() && (index.raw_value() < k_payload_mask);
}

constexpr bool is_valid_id(const id_type id) noexcept
{
    return id.is_valid() &&
        ((id.raw_value() & k_invalid_id_mask) == 0u) &&
        ((id.raw_value() & k_mount_point_id_mask) != 0u) &&
        ((id.raw_value() & k_module_id_mask) != 0u);
}

constexpr id_type make_id(const mount_point_ids::id_type mount_point_id, const index_type module_index) noexcept
{
    return (mount_point_ids::ops::is_valid_id(mount_point_id) && is_valid_index(module_index))
        ? id_type(mount_point_id.raw_value() | (bit_ops::spread_to_even_bits(k_payload_mask - module_index.raw_value()) << k_id_field_shift))
        : id_type{ 0u };
}

constexpr index_type decode_id(const id_type id) noexcept
{
    return is_valid_id(id)
        ? index_type(static_cast<std::uint64_t>((k_payload_mask - bit_ops::pack_from_even_bits(id.raw_value() >> k_id_field_shift))))
        : index_type{};
}

constexpr mount_point_ids::id_type get_mount_point_id(const id_type id) noexcept
{
    return is_valid_id(id)
        ? mount_point_ids::id_type(id.raw_value() & k_mount_point_id_mask)
        : mount_point_ids::id_type{ 0u };
}

constexpr mount_point_ids::index_type get_mount_point_index(const id_type id) noexcept
{
    return mount_point_ids::ops::decode_id(get_mount_point_id(id));
}

}   //  namespace ops
}   //  namespace module_ids

//==============================================================================
//  Combined system ids (module + thread) helpers
//==============================================================================

namespace system_ids
{
namespace ops
{

constexpr bool is_valid_id(const id_type system_id) noexcept
{
    return system_id.is_valid() &&
        ((system_id.raw_value() & k_invalid_id_mask) == 0u) &&
        ((system_id.raw_value() & k_module_id_mask) != 0u) &&
        ((system_id.raw_value() & k_thread_id_mask) != 0u);
}

constexpr id_type make_system_id(const module_ids::id_type module_id, const thread_ids::id_type thread_id) noexcept
{
    return (module_ids::ops::is_valid_id(module_id) && thread_ids::ops::is_valid_id(thread_id))
        ? id_type(module_id.raw_value() | thread_id.raw_value())
        : id_type{ 0u };
}

constexpr module_ids::id_type get_module_id(const id_type system_id) noexcept
{
    return is_valid_id(system_id)
        ? module_ids::id_type(system_id.raw_value() & k_module_id_mask)
        : module_ids::id_type{ 0u };
}

constexpr thread_ids::id_type get_thread_id(const id_type system_id) noexcept
{
    return is_valid_id(system_id)
        ? thread_ids::id_type(system_id.raw_value() & k_thread_id_mask)
        : thread_ids::id_type{ 0u };
}

constexpr mount_point_ids::id_type get_mount_point_id(const id_type system_id) noexcept
{
    return module_ids::ops::get_mount_point_id(get_module_id(system_id));
}

constexpr mount_point_ids::index_type get_mount_point_index(const id_type system_id) noexcept
{
    return module_ids::ops::get_mount_point_index(get_module_id(system_id));
}

}   //  namespace ops
}   //  namespace system_ids

//==============================================================================
//  Type ids
//==============================================================================

namespace system_type_ids
{

#include "system/system_type_ids.def"

#define MV_SYSTEM_TYPE(name) name##_index_value,
enum : index_type
{
    MV_SYSTEM_TYPES(MV_SYSTEM_TYPE)
    k_count
};
#undef MV_SYSTEM_TYPE

static_assert((k_count <= ops::k_capacity), "The system type definition count exceeds the encoded system-type capacity.");

#define MV_SYSTEM_TYPE(name) \
constexpr index_type name##_index = ops::encode_index(name##_index_value); \
constexpr system_type_id name = ops::encode_id(name##_index);
MV_SYSTEM_TYPES(MV_SYSTEM_TYPE)
#undef MV_SYSTEM_TYPE
#undef MV_SYSTEM_TYPES

}   //  namespace system_type_ids

//==============================================================================
//  Mount point ids
//==============================================================================

namespace mount_point_ids
{

#define MV_SYSTEM_MOUNT_POINT(name) name##_index_value,
#define MV_SYSTEM_MODULE(name, mount_point_name)
enum : index_type::repr_type
{
#include "system/runtime_ids.def"
    k_count
};
#undef MV_SYSTEM_MODULE
#undef MV_SYSTEM_MOUNT_POINT

static_assert((k_count <= ops::k_capacity), "The mount-point definition count exceeds the encoded mount-point capacity.");

#define MV_SYSTEM_MOUNT_POINT(name) \
constexpr index_type name##_index = ops::make_index(name##_index_value); \
constexpr id_type name = ops::make_id(name##_index);
#define MV_SYSTEM_MODULE(name, mount_point_name)
#include "system/runtime_ids.def"
#undef MV_SYSTEM_MODULE
#undef MV_SYSTEM_MOUNT_POINT

}   //  namespace mount_point_ids

//==============================================================================
//  Module ids
//==============================================================================

namespace module_ids
{

#define MV_SYSTEM_MOUNT_POINT(name)
#define MV_SYSTEM_MODULE(name, mount_point_name) name##_index_value,
enum : index_type::repr_type
{
#include "system/runtime_ids.def"
    k_count
};
#undef MV_SYSTEM_MODULE
#undef MV_SYSTEM_MOUNT_POINT

static_assert((k_count <= ops::k_capacity), "The module definition count exceeds the encoded module capacity.");

#define MV_SYSTEM_MOUNT_POINT(name)
#define MV_SYSTEM_MODULE(name, mount_point_name) \
constexpr index_type name##_index = ops::make_index(name##_index_value); \
constexpr id_type name = ops::make_id(mount_point_ids::mount_point_name, name##_index);
#include "system/runtime_ids.def"
#undef MV_SYSTEM_MODULE
#undef MV_SYSTEM_MOUNT_POINT

}   //  namespace module_ids

//==============================================================================
//  Thread ids
//==============================================================================

namespace thread_ids
{

#define MV_SYSTEM_THREAD(name) name##_index_value,
enum : index_type::repr_type
{
#include "system/thread_ids.def"
    k_count
};
#undef MV_SYSTEM_THREAD

static_assert((k_count <= ops::k_capacity), "The thread definition count exceeds the encoded thread capacity.");

#define MV_SYSTEM_THREAD(name) \
constexpr index_type name##_index = ops::make_index(name##_index_value); \
constexpr id_type name = ops::make_id(name##_index);
#include "system/thread_ids.def"
#undef MV_SYSTEM_THREAD

}   //  namespace thread_ids

//==============================================================================
//  System ids
//==============================================================================

namespace system_ids
{

constexpr id_type host = ops::make_system_id(module_ids::executable, thread_ids::host);
constexpr id_type bg_file_io = ops::make_system_id(module_ids::executable, thread_ids::bg_file_io);
constexpr id_type bg_conditioning = ops::make_system_id(module_ids::executable, thread_ids::bg_conditioning);
constexpr id_type executive = ops::make_system_id(module_ids::executive, thread_ids::executive);

}   //  namespace system_ids

#endif  //  #ifndef SYSTEM_IDS_HPP_INCLUDED
