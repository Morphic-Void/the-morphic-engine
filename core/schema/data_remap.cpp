
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_remap.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Oct 26
//
//  Build compatible member-copy plans and execute them over bounded byte views.

#include "schema/data_remap.hpp"
#include "schema/type_compatibility.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace schema
{

[[nodiscard]] static bool view_capacity(const CByteConstView view, const std::uint32_t size,
    const std::uint32_t stride, const std::uint32_t alignment, std::size_t& capacity) noexcept
{
    if ((alignment == 0u) || ((size != 0u) && (stride < size)))
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

[[nodiscard]] static std::size_t active_span(const std::uint32_t size, const std::uint32_t stride, const std::size_t count) noexcept
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
    //  Resolved layouts already satisfy the single-allocation size ceiling.
    m_destination = { static_cast<std::uint32_t>(destination.size), static_cast<std::uint32_t>(destination.stride),
        static_cast<std::uint32_t>(destination.alignment) };
    //  Resolved direct members cannot overlap. Only another source claiming
    //  the same nonempty destination member can create an overlapping write.
    TPodVector<std::uint8_t> claimed;
    if (destination.count && (!claimed.allocate(destination.count) || !claimed.set_size(destination.count)))
    {
        diagnostic.reason = ERemapReason::allocation_failed;
        clear();
        return false;
    }
    if (claimed.size())
    {
        std::fill_n(claimed.data(), claimed.size(), std::uint8_t{});
    }
    struct SNamedMember
    {
        CSchemaIndex index;
        CPropertyNameId name;
    };
    TPodVector<SNamedMember> source_members;
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
        if (!m_sources.push_back({ static_cast<std::uint32_t>(source.size), static_cast<std::uint32_t>(source.stride),
            static_cast<std::uint32_t>(source.alignment) }))
        {
            diagnostic.reason = ERemapReason::allocation_failed;
            clear();
            return false;
        }
        source_members.clear();
        if (!source_members.reserve(source.count))
        {
            diagnostic.reason = ERemapReason::allocation_failed;
            clear();
            return false;
        }
        for (std::uint32_t i = 0u; i < source.count; ++i)
        {
            const CSchemaIndex index = input.schema->member_at(input.type, i);
            SMember member;
            if (!input.schema->member(index, member))
            {
                diagnostic.reason = ERemapReason::invalid_input;
                clear();
                return false;
            }
            if (!source_members.push_back({ index, member.name }))
            {
                diagnostic.reason = ERemapReason::allocation_failed;
                clear();
                return false;
            }
        }
        if (source_members.size() > 1u)
        {
            std::sort(source_members.data(), (source_members.data() + source_members.size()),
                [&](const SNamedMember& a, const SNamedMember& b) noexcept
                { return input.schema->name(a.name) < input.schema->name(b.name); });
        }
        CTypeCompatibility compatibility{ *input.schema, destination_schema, ETypeMatch::definition };
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
            const CStringView name = destination_schema.name(dm.name);
            std::size_t begin = 0u, end = source_members.size();
            while (begin < end)
            {
                const std::size_t middle = begin + ((end - begin) / 2u);
                if (input.schema->name(source_members[middle].name) < name)
                {
                    begin = middle + 1u;
                }
                else
                {
                    end = middle;
                }
            }
            if ((begin == source_members.size()) || !(input.schema->name(source_members[begin].name) == name))
            {
                continue;
            }
            const CSchemaIndex source_index = source_members[begin].index;
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
            const auto comparison = compatibility.compare(sm.type, dm.type);
            const bool allocation_failed = (comparison == ETypeMatchResult::allocation_failed);
            const bool invalid_input = (comparison == ETypeMatchResult::invalid_input);
            const bool match = comparison == ETypeMatchResult::match;
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
            if (claimed[i])
            {
                diagnostic = { ERemapReason::overlap, slot, source_index, destination_index };
                clear();
                return false;
            }
            claimed[i] = 1u;
            if (!m_ranges.push_back({ slot, static_cast<std::uint32_t>(sm.offset), static_cast<std::uint32_t>(dm.offset),
                static_cast<std::uint32_t>(sm.size) }))
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
