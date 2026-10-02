
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_remap.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Oct 26

#include "schema/data_remap.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace schema
{

[[nodiscard]] static bool same_scalar(const SScalar& a, const SScalar& b) noexcept
{
    if (a.kind != b.kind)
    {
        return false;
    }
    if (a.kind == EScalar::signed_integer)
    {
        return a.value.signed_value == b.value.signed_value;
    }
    if (a.kind == EScalar::floating_point)
    {
        std::uint64_t left{}, right{};
        std::memcpy(&left, &a.value.floating_value, sizeof(left));
        std::memcpy(&right, &b.value.floating_value, sizeof(right));
        return left == right;
    }
    return a.value.unsigned_value == b.value.unsigned_value;
}

struct SComparePair
{
    CSchemaIndex source, destination, source_default, destination_default;
};

[[nodiscard]] static bool add_pair(TPodVector<SComparePair>& pending, const SComparePair pair) noexcept
{
    for (std::size_t i = 0u; i < pending.size(); ++i)
    {
        const SComparePair& found = pending[i];
        if ((found.source == pair.source) && (found.destination == pair.destination) &&
            (found.source_default == pair.source_default) &&
            (found.destination_default == pair.destination_default))
        {
            return true;
        }
    }
    return pending.push_back(pair);
}

[[nodiscard]] static bool same_type_header(const CResolvedSchema& source, const SType& a,
    const CResolvedSchema& destination, const SType& b) noexcept
{
    return (a.category == b.category) && (a.primitive == b.primitive) &&
        (a.size == b.size) && (a.alignment == b.alignment) && (a.stride == b.stride) &&
        (a.count == b.count) && (a.gaps == b.gaps) && (a.internal == b.internal) &&
        (a.named_components == b.named_components) &&
        (source.name(a.name) == destination.name(b.name)) &&
        (a.element_or_storage.is_valid() == b.element_or_storage.is_valid());
}

[[nodiscard]] static bool exact_type(const CResolvedSchema& source, const CSchemaIndex source_type,
    const CResolvedSchema& destination, const CSchemaIndex destination_type,
    bool& allocation_failed, bool& invalid_input) noexcept
{
    if ((&source == &destination) && (source_type == destination_type))
    {
        return true;
    }
    SType root_source, root_destination;
    if (!source.type(source_type, root_source) || !destination.type(destination_type, root_destination))
    {
        invalid_input = true;
        return false;
    }
    if (!same_type_header(source, root_source, destination, root_destination))
    {
        return false;
    }
    TPodVector<SComparePair> pending;
    if (!add_pair(pending, { source_type, destination_type, {}, {} }))
    {
        allocation_failed = true;
        return false;
    }
    for (std::size_t next = 0u; next < pending.size(); ++next)
    {
        const SComparePair pair = pending[next];
        if ((&source == &destination) && (pair.source == pair.destination) &&
            (pair.source_default == pair.destination_default))
        {
            continue;
        }
        SType a, b;
        SDefault da, db;
        if (!source.type(pair.source, a) || !destination.type(pair.destination, b) ||
            !source.default_value(pair.source, pair.source_default, da) ||
            !destination.default_value(pair.destination, pair.destination_default, db))
        {
            invalid_input = true;
            return false;
        }
        if (!same_type_header(source, a, destination, b) || (da.kind != db.kind) ||
            ((da.kind == EDefault::scalar) && !same_scalar(da.scalar, db.scalar)))
        {
            return false;
        }
        if (a.element_or_storage &&
            (!add_pair(pending, { a.element_or_storage, b.element_or_storage, {}, {} })))
        {
            allocation_failed = true;
            return false;
        }
        if (a.category == ECategory::array)
        {
            const std::uint32_t supplied = (da.supplied_count > db.supplied_count) ? da.supplied_count : db.supplied_count;
            for (std::uint32_t i = 0u; i < supplied; ++i)
            {
                CSchemaIndex ad, bd;
                if (!source.default_element(pair.source, pair.source_default, i, ad) ||
                    !destination.default_element(pair.destination, pair.destination_default, i, bd))
                {
                    invalid_input = true;
                    return false;
                }
                if (!add_pair(pending, { a.element_or_storage, b.element_or_storage, ad, bd }))
                {
                    allocation_failed = true;
                    return false;
                }
            }
        }
        const bool has_children =
            (a.category == ECategory::structure) ||
            (a.category == ECategory::enumeration) ||
            (a.category == ECategory::bit_structure);
        for (std::uint32_t i = 0u; i < (has_children ? a.count : 0u); ++i)
        {
            if (a.category == ECategory::structure)
            {
                SMember am, bm;
                if (!source.member(source.member_at(pair.source, i), am) ||
                    !destination.member(destination.member_at(pair.destination, i), bm))
                {
                    invalid_input = true;
                    return false;
                }
                if (!(source.name(am.name) == destination.name(bm.name)) ||
                    (am.offset != bm.offset) || (am.size != bm.size))
                {
                    return false;
                }
                if (!add_pair(pending, { am.type, bm.type, am.default_description, bm.default_description }))
                {
                    allocation_failed = true;
                    return false;
                }
            }
            else if (a.category == ECategory::enumeration)
            {
                SLabel al, bl;
                if (!source.label(source.label_at(pair.source, i), al) ||
                    !destination.label(destination.label_at(pair.destination, i), bl))
                {
                    invalid_input = true;
                    return false;
                }
                if (!(source.name(al.name) == destination.name(bl.name)) || !same_scalar(al.value, bl.value))
                {
                    return false;
                }
            }
            else
            {
                SField af, bf;
                if (!source.field(source.field_at(pair.source, i), af) ||
                    !destination.field(destination.field_at(pair.destination, i), bf))
                {
                    invalid_input = true;
                    return false;
                }
                if (!(source.name(af.name) == destination.name(bf.name)) || (af.mask != bf.mask) ||
                    (af.shift != bf.shift) || (af.width != bf.width) ||
                    (af.interpretation != bf.interpretation) ||
                    (af.signed_value != bf.signed_value) || (af.primitive != bf.primitive))
                {
                    return false;
                }
                if (!add_pair(pending, { af.type, bf.type, af.default_description, bf.default_description }))
                {
                    allocation_failed = true;
                    return false;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] static bool view_capacity(const CByteConstView view, const std::uint64_t size,
    const std::uint64_t stride, const std::uint64_t alignment, std::size_t& capacity) noexcept
{
    constexpr std::uint64_t ceiling = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
    if ((alignment == 0u) || (alignment > ceiling) || ((size != 0u) && ((size > ceiling) || (stride < size) || (stride > ceiling))))
    {
        return false;
    }
    if (view.is_empty())
    {
        capacity = size == 0u ? std::numeric_limits<std::size_t>::max() : 0u;
        return true;
    }
    if ((!view.is_valid()) || (!view.is_ready()) || (view.data() == nullptr) ||
        ((reinterpret_cast<std::uintptr_t>(view.data()) % alignment) != 0u))
    {
        return false;
    }
    if (size == 0u)
    {
        capacity = std::numeric_limits<std::size_t>::max();
        return true;
    }
    if ((view.size() % static_cast<std::size_t>(stride)) != 0u)
    {
        return false;
    }
    capacity = view.size() < size ? 0u : (1u + ((view.size() - static_cast<std::size_t>(size)) / static_cast<std::size_t>(stride)));
    return true;
}

[[nodiscard]] static std::size_t active_span(const std::uint64_t size, const std::uint64_t stride, const std::size_t count) noexcept
{
    return ((size == 0u) || (count == 0u)) ? 0u : (static_cast<std::size_t>(size) + ((count - 1u) * static_cast<std::size_t>(stride)));
}

[[nodiscard]] static bool overlaps(const void* const a, const std::size_t a_size, const void* const b, const std::size_t b_size) noexcept
{
    if ((a_size == 0u) || (b_size == 0u))
    {
        return false;
    }
    const auto aa = reinterpret_cast<std::uintptr_t>(a);
    const auto bb = reinterpret_cast<std::uintptr_t>(b);
    return (aa <= bb) ? (bb - aa < a_size) : (aa - bb < b_size);
}

template<std::size_t Width>
static void copy_records(const std::uint8_t* source, std::uint8_t* destination,
    const std::size_t source_step, const std::size_t destination_step,
    const std::size_t count, const std::size_t width) noexcept
{
    for (std::size_t i = 0u; i < count; ++i)
    {
        if constexpr (Width == 0u)
        {
            std::memcpy((destination + (static_cast<std::size_t>(i) * destination_step)),
                (source + (static_cast<std::size_t>(i) * source_step)), width);
        }
        else
        {
            std::memcpy((destination + (static_cast<std::size_t>(i) * destination_step)),
                (source + (static_cast<std::size_t>(i) * source_step)), Width);
        }
    }
}

CDataRemapPlan::CDataRemapPlan(CDataRemapPlan&& source) noexcept
    : m_sources(std::move(source.m_sources)), m_ranges(std::move(source.m_ranges)),
      m_destination(source.m_destination), m_matched_members(source.m_matched_members), m_ready(source.m_ready)
{
    source.clear();
}

CDataRemapPlan& CDataRemapPlan::operator=(CDataRemapPlan&& source) noexcept
{
    if (this != &source)
    {
        m_sources = std::move(source.m_sources);
        m_ranges = std::move(source.m_ranges);
        m_destination = source.m_destination;
        m_matched_members = source.m_matched_members;
        m_ready = source.m_ready;
        source.clear();
    }
    return *this;
}

void CDataRemapPlan::clear() noexcept
{
    m_sources.clear();
    m_ranges.clear();
    m_destination = {};
    m_matched_members = 0u;
    m_ready = false;
}

bool CDataRemapPlan::initialise(const SRemapSourceType* const sources, const std::size_t source_count,
    const CResolvedSchema& destination_schema, const CSchemaIndex destination_type, SRemapDiagnostic& diagnostic) noexcept
{
    clear();
    diagnostic = {};
    if ((sources == nullptr) || (source_count == 0u) || (!destination_schema.is_ready()))
    {
        diagnostic.reason = ERemapReason::invalid_input;
        return false;
    }
    SType destination;
    if (!destination_schema.type(destination_type, destination) || (destination.category != ECategory::structure))
    {
        diagnostic.reason = ERemapReason::unknown_type;
        return false;
    }
    m_destination = { destination.size, destination.stride, destination.alignment };
    for (std::size_t slot = 0u; slot < source_count; ++slot)
    {
        diagnostic.source = slot;
        const SRemapSourceType& input = sources[slot];
        if ((input.schema == nullptr) || !input.schema->is_ready())
        {
            diagnostic.reason = ERemapReason::invalid_input;
            clear();
            return false;
        }
        SType source;
        if (!input.schema->type(input.type, source) || (source.category != ECategory::structure))
        {
            diagnostic.reason = ERemapReason::unknown_type;
            clear();
            return false;
        }
        if (!m_sources.push_back({ source.size, source.stride, source.alignment }))
        {
            diagnostic.reason = ERemapReason::allocation_failed;
            clear();
            return false;
        }
        for (std::uint32_t i = 0u; i < destination.count; ++i)
        {
            const CSchemaIndex destination_index = destination_schema.member_at(destination_type, i);
            SMember dm;
            if (!destination_schema.member(destination_index, dm))
            {
                diagnostic.reason = ERemapReason::invalid_input;
                clear();
                return false;
            }
            const CSchemaIndex source_index = input.schema->find_member(input.type, destination_schema.name(dm.name));
            if (!source_index)
            {
                continue;
            }
            SMember sm;
            if (!input.schema->member(source_index, sm))
            {
                diagnostic.reason = ERemapReason::invalid_input;
                clear();
                return false;
            }
            if (sm.size != dm.size)
            {
                continue;
            }
            bool allocation_failed{};
            bool invalid_input{};
            const bool match = exact_type(*input.schema, sm.type, destination_schema, dm.type, allocation_failed, invalid_input);
            if (allocation_failed)
            {
                diagnostic = { ERemapReason::allocation_failed, slot, source_index, destination_index };
                clear();
                return false;
            }
            if (invalid_input)
            {
                diagnostic = { ERemapReason::invalid_input, slot, source_index, destination_index };
                clear();
                return false;
            }
            if (!match)
            {
                continue;
            }
            ++m_matched_members;
            if (sm.size == 0u)
            {
                continue;
            }
            if ((sm.offset > source.size) || (sm.size > (source.size - sm.offset)) ||
                (dm.offset > destination.size) || (dm.size > (destination.size - dm.offset)))
            {
                diagnostic = { ERemapReason::invalid_range, slot, source_index, destination_index };
                clear();
                return false;
            }
            for (std::size_t j = 0u; j < m_ranges.size(); ++j)
            {
                const SRange& prior = m_ranges[j];
                if ((dm.offset < (prior.destination_offset + prior.size)) &&
                    (prior.destination_offset < (dm.offset + dm.size)))
                {
                    diagnostic = { ERemapReason::overlap, slot, source_index, destination_index };
                    clear();
                    return false;
                }
            }
            if (!m_ranges.push_back({ slot, sm.offset, dm.offset, sm.size }))
            {
                diagnostic.reason = ERemapReason::allocation_failed;
                clear();
                return false;
            }
        }
    }
    if (m_ranges.size() > 1u)
    {
        std::sort(m_ranges.data(), (m_ranges.data() + m_ranges.size()),
            [](const SRange& a, const SRange& b) noexcept
            {
                return (a.source < b.source) || ((a.source == b.source) && (a.destination_offset < b.destination_offset));
            });
        std::size_t used = 1u;
        for (std::size_t i = 1u; i < m_ranges.size(); ++i)
        {
            SRange& previous = m_ranges[used - 1u];
            const SRange current = m_ranges[i];
            if ((previous.source == current.source) &&
                ((previous.source_offset + previous.size) == current.source_offset) &&
                ((previous.destination_offset + previous.size) == current.destination_offset))
            {
                previous.size += current.size;
            }
            else
            {
                m_ranges[used++] = current;
            }
        }
        (void)m_ranges.set_size(used);
    }
    for (std::size_t i = 0u; i < m_ranges.size(); ++i)
    {
        SRange& range = m_ranges[i];
        const bool source_contiguous = (m_sources[range.source].stride == range.size);
        const bool destination_contiguous = (m_destination.stride == range.size);
        range.kernel = source_contiguous ?
            (destination_contiguous ? EKernel::contiguous : EKernel::contiguous_source) :
            (destination_contiguous ? EKernel::contiguous_destination : EKernel::strided);
        range.fixed_size = ((range.size == 1u) || (range.size == 2u) || (range.size == 4u) ||
            (range.size == 8u) || (range.size == 16u)) ? static_cast<std::uint8_t>(range.size) : 0u;
    }
    diagnostic = {};
    m_ready = true;
    return true;
}

bool CDataRemapPlan::execute(const CByteConstView* const sources, const std::size_t source_count, const CByteView destination) const noexcept
{
    if (!m_ready || (sources == nullptr) || (source_count != m_sources.size()))
    {
        return false;
    }
    std::size_t count{};
    if (!view_capacity(destination.const_view(), m_destination.size, m_destination.stride,
        m_destination.alignment, count))
    {
        return false;
    }
    for (std::size_t slot = 0u; slot < source_count; ++slot)
    {
        std::size_t capacity{};
        if (!view_capacity(sources[slot], m_sources[slot].size, m_sources[slot].stride,
            m_sources[slot].alignment, capacity))
        {
            return false;
        }
        if (capacity < count)
        {
            count = capacity;
        }
    }
    if ((count == 0u) || (m_ranges.size() == 0u))
    {
        return true;
    }
    const std::size_t destination_span = active_span(m_destination.size, m_destination.stride, count);
    for (std::size_t slot = 0u; slot < source_count; ++slot)
    {
        const std::size_t source_span = active_span(m_sources[slot].size, m_sources[slot].stride, count);
        if (overlaps(sources[slot].data(), source_span, destination.data(), destination_span))
        {
            return false;
        }
    }
    for (std::size_t n = 0u; n < m_ranges.size(); ++n)
    {
        const SRange* const range = &m_ranges[n];
        const SLayout& layout = m_sources[range->source];
        const std::uint8_t* const source = sources[range->source].data() + range->source_offset;
        std::uint8_t* const target = destination.data() + range->destination_offset;
        if (range->kernel == EKernel::contiguous)
        {
            std::memcpy(target, source, (static_cast<std::size_t>(range->size) * count));
        }
        else
        {
            const std::size_t source_step = range->kernel == EKernel::contiguous_source ?
                static_cast<std::size_t>(range->size) : static_cast<std::size_t>(layout.stride);
            const std::size_t destination_step = range->kernel == EKernel::contiguous_destination ?
                static_cast<std::size_t>(range->size) : static_cast<std::size_t>(m_destination.stride);
            switch (range->fixed_size)
            {
                case 1u: copy_records<1u>(source, target, source_step, destination_step, count, 1u); break;
                case 2u: copy_records<2u>(source, target, source_step, destination_step, count, 2u); break;
                case 4u: copy_records<4u>(source, target, source_step, destination_step, count, 4u); break;
                case 8u: copy_records<8u>(source, target, source_step, destination_step, count, 8u); break;
                case 16u: copy_records<16u>(source, target, source_step, destination_step, count, 16u); break;
                default: copy_records<0u>(source, target, source_step, destination_step, count,
                    static_cast<std::size_t>(range->size)); break;
            }
        }
    }
    return true;
}

} // namespace schema
