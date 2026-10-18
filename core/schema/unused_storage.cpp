
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    unused_storage.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Oct 26
//
//  Validate writable extents before clearing unaddressable bytes and optional bits.

#include "schema/unused_storage.hpp"
#include "schema/type_compatibility.hpp"
#include "memory/memory_policies.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace schema
{

static constexpr unsigned k_max_clear_depth = 256u;

//==============================================================================
//  Clearing extents and type validation
//==============================================================================

struct SClearExtent
{
    std::uint64_t offset{}, size{};
    CSchemaIndex type;
    std::uint32_t count{};
    bool has_work{};
};

[[nodiscard]] static bool valid_bits(const EUnusedBits bits) noexcept
{
    return (bits == EUnusedBits::preserve) || (bits == EUnusedBits::clear);
}

//  Cache completed type preflights for this operation. Clearing consults these
//  results without allocating or revalidating a nested type for every value.
class CClearTypes
{
public:
    CClearTypes(const CResolvedSchema& schema, const EUnusedBits bits) noexcept : m_schema(schema), m_bits(bits) {}
    [[nodiscard]] bool validate(const CSchemaIndex type, bool& has_work) noexcept
    {
        unsigned height{};
        return validate(type, 0u, has_work, height);
    }
    [[nodiscard]] bool has_work(const CSchemaIndex type) const noexcept
    {
        const auto* const found = find(type);
        return found && found->has_work;
    }

private:
    struct STypeWork
    {
        CSchemaIndex type;
        unsigned height{};
        bool has_work{};
    };
    [[nodiscard]] const STypeWork* find(const CSchemaIndex type) const noexcept
    {
        for (std::size_t i = 0u; i < m_inline_count; ++i)
            if (m_inline[i].type == type)
            {
                return &m_inline[i];
            }
        const auto found = m_index.find(COrdinalHashIndex::hash_key(type.query_value()),
            [this, type](const std::uint32_t i)
            {
                return m_types[i].type == type;
            });
        return found == COrdinalHashIndex::k_missing_record ? nullptr : &m_types[found];
    }
    [[nodiscard]] bool validate(const CSchemaIndex type, const unsigned depth, bool& has_work, unsigned& height) noexcept;
    [[nodiscard]] bool inspect(const CSchemaIndex type, const unsigned depth, bool& has_work, unsigned& height) noexcept;
    const CResolvedSchema& m_schema;
    const EUnusedBits m_bits;
    STypeWork m_inline[8];
    std::size_t m_inline_count{};
    TPodVector<STypeWork> m_types;
    COrdinalHashIndex m_index;
};

bool CClearTypes::validate(const CSchemaIndex type, const unsigned depth, bool& has_work, unsigned& height) noexcept
{
    if (depth >= k_max_clear_depth)
    {
        return false;
    }
    const STypeWork* const found = find(type);
    if (found)
    {
        height = found->height;
        has_work = found->has_work;
        return height < k_max_clear_depth - depth;
    }
    height = 0u;
    if (!inspect(type, depth, has_work, height))
    {
        return false;
    }
    if (!has_work && (height == 0u))
    {
        return true;
    }
    if (m_inline_count < sizeof(m_inline) / sizeof(m_inline[0]))
    {
        m_inline[m_inline_count++] = { type, height, has_work };
        return true;
    }
    const auto hash = [this](const std::size_t i)
    {
        return COrdinalHashIndex::hash_key(m_types[i].type.query_value());
    };
    if (!m_index.reserve((m_types.size() + 1u), m_types.size(), hash) || !m_types.push_back({ type, height, has_work }))
    {
        return false;
    }
    m_index.insert(static_cast<std::uint32_t>(m_types.size() - 1u), COrdinalHashIndex::hash_key(type.query_value()));
    return true;
}

bool CClearTypes::inspect(const CSchemaIndex type, const unsigned depth, bool& has_work, unsigned& height) noexcept
{
    const CResolvedSchema& schema = m_schema;
    const EUnusedBits bits = m_bits;
    SType layout;
    if ((depth >= k_max_clear_depth) || !schema.type(type, layout) ||
        (layout.alignment == 0u) || (layout.alignment > std::numeric_limits<std::size_t>::max()) ||
        (layout.size > std::numeric_limits<std::size_t>::max()))
    {
        return false;
    }
    has_work = false;
    if (layout.size == 0u)
    {
        return (layout.category != ECategory::bit_structure);
    }
    if (!layout.gaps)
    {
        return true;
    }
    if ((layout.category == ECategory::primitive) || (layout.category == ECategory::enumeration))
    {
        return true;
    }
    if (layout.category == ECategory::bit_structure)
    {
        if ((layout.size == 0u) || (layout.size > sizeof(std::uint64_t)))
        {
            return false;
        }
        const std::uint64_t valid_mask = layout.size == sizeof(std::uint64_t) ? UINT64_MAX :
            ((std::uint64_t{ 1u } << (layout.size * 8u)) - 1u);
        std::uint64_t occupied{};
        for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
        {
            SField field;
            if (!schema.field(schema.field_at(type, ordinal), field) ||
                (field.mask == 0u) || ((field.mask & ~valid_mask) != 0u) ||
                ((occupied & field.mask) != 0u))
            {
                return false;
            }
            occupied |= field.mask;
        }
        has_work = (bits == EUnusedBits::clear) && (occupied != valid_mask);
        return true;
    }
    if (layout.category == ECategory::array)
    {
        SType element;
        bool element_work{};
        unsigned child_height{};
        if (!schema.type(layout.element_or_storage, element) ||
            !validate(layout.element_or_storage, (depth + 1u), element_work, child_height) ||
            (layout.stride < element.size) ||
            (layout.stride > std::numeric_limits<std::size_t>::max()))
        {
            return false;
        }
        height = child_height + 1u;
        if ((layout.count == 0u) || (element.size == 0u))
        {
            has_work = layout.size != 0u;
            return true;
        }
        const std::uint64_t steps = static_cast<std::uint64_t>(layout.count - 1u);
        if ((steps > ((layout.size - std::min(layout.size, element.size)) / layout.stride)) ||
            (element.size > (layout.size - (steps * layout.stride))))
        {
            return false;
        }
        const std::uint64_t end = (steps * layout.stride) + element.size;
        has_work = element_work || ((layout.count > 1u) && (layout.stride > element.size)) ||
            (end < layout.size);
        return true;
    }
    if (layout.category != ECategory::structure)
    {
        return false;
    }
    std::uint64_t cursor{};
    for (std::uint32_t position = 0u; position < layout.count; ++position)
    {
        SMember member;
        if (!schema.member(schema.physical_member_at(type, position), member))
        {
            return false;
        }
        SType child;
        bool child_work{};
        unsigned child_height{};
        if (!schema.type(member.type, child) || (member.size != child.size) ||
            (member.offset > layout.size) || (member.size > (layout.size - member.offset)) ||
            !validate(member.type, (depth + 1u), child_work, child_height))
        {
            return false;
        }
        height = std::max(height, (child_height + 1u));
        if (member.size != 0u)
        {
            if (member.offset < cursor)
            {
                return false;
            }
            has_work = has_work || (member.offset > cursor);
            cursor = member.offset + member.size;
        }
        has_work = has_work || child_work;
    }
    has_work = has_work || (cursor < layout.size);
    return true;
}

//==============================================================================
//  Type storage clearing
//==============================================================================

static void clear_type(const CResolvedSchema& schema, const CSchemaIndex type, std::uint8_t* const bytes,
    const EUnusedBits bits, const CClearTypes& types) noexcept
{
    SType layout;
    (void)schema.type(type, layout); // The complete schema traversal was preflighted.
    if ((layout.size == 0u) || !layout.gaps || (layout.category == ECategory::primitive) ||
        (layout.category == ECategory::enumeration))
    {
        return;
    }
    if (layout.category == ECategory::bit_structure)
    {
        if (bits == EUnusedBits::clear)
        {
            std::uint64_t occupied{};
            for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
            {
                SField field;
                (void)schema.field(schema.field_at(type, ordinal), field);
                occupied |= field.mask;
            }
            for (std::size_t byte = 0u; byte < static_cast<std::size_t>(layout.size); ++byte)
            {
                bytes[byte] &= static_cast<std::uint8_t>(occupied >> (byte * 8u));
            }
        }
        return;
    }
    if (layout.category == ECategory::array)
    {
        SType element;
        (void)schema.type(layout.element_or_storage, element);
        if ((layout.count == 0u) || (element.size == 0u))
        {
            std::memset(bytes, 0, static_cast<std::size_t>(layout.size));
            return;
        }
        const bool element_work = types.has_work(layout.element_or_storage);
        std::size_t cursor{};
        for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
        {
            const std::size_t offset = static_cast<std::size_t>(layout.stride * ordinal);
            if (offset > cursor)
            {
                std::memset((bytes + cursor), 0, (offset - cursor));
            }
            if (element_work)
            {
                clear_type(schema, layout.element_or_storage, (bytes + offset), bits, types);
            }
            cursor = offset + static_cast<std::size_t>(element.size);
        }
        if (cursor < layout.size)
        {
            std::memset((bytes + cursor), 0, (static_cast<std::size_t>(layout.size) - cursor));
        }
        return;
    }
    std::uint64_t cursor{};
    for (std::uint32_t position = 0u; position < layout.count; ++position)
    {
        SMember member;
        (void)schema.member(schema.physical_member_at(type, position), member); // Preflighted.
        if (member.size != 0u)
        {
            if (member.offset > cursor)
            {
                std::memset((bytes + cursor), 0, static_cast<std::size_t>(member.offset - cursor));
            }
            if (types.has_work(member.type))
            {
                clear_type(schema, member.type, (bytes + member.offset), bits, types);
            }
            cursor = member.offset + member.size;
        }
    }
    if (cursor < layout.size)
    {
        std::memset((bytes + cursor), 0, static_cast<std::size_t>(layout.size - cursor));
    }
}

//==============================================================================
//  Document extent collection
//==============================================================================

template<class TQuery, class THandle>
[[nodiscard]] static bool locator(const TQuery& query, const THandle entry, THandle& count_node, THandle& size_node, std::uint64_t& offset) noexcept
{
    const THandle node = query.object_child(entry, CStringView{ "locator" });
    if (!node || (query.value_kind(node) != EDocumentValueKind::object))
    {
        return false;
    }
    for (THandle property = query.first_child(node); property; property = query.next_sibling(property))
    {
        const CStringView name = query.name(property);
        if (!(name == CStringView{ "offset" }) && !(name == CStringView{ "valid" }) &&
            !(name == CStringView{ "count" }) && !(name == CStringView{ "size" }))
        {
            return false;
        }
    }
    const THandle offset_node = query.object_child(node, CStringView{ "offset" });
    const THandle valid_node = query.object_child(node, CStringView{ "valid" });
    bool valid{};
    if (!offset_node || !valid_node || !query.unsigned_integer_value(offset_node, offset) ||
        (offset > UINT32_MAX) || !query.boolean_value(valid_node, valid) || !valid)
    {
        return false;
    }
    count_node = query.object_child(node, CStringView{ "count" });
    size_node = query.object_child(node, CStringView{ "size" });
    return true;
}

[[nodiscard]] static bool collect_instances(const CResolvedSchema& schema, const CInstanceDocumentQuery& query,
    CClearTypes& types, TPodVector<SClearExtent>& extents) noexcept
{
    const CInstanceHandle root = query.root();
    if (!query.is_ready() || (query.value_kind(root) != EDocumentValueKind::object))
    {
        return false;
    }
    const CInstanceHandle instances = query.object_child(root, CStringView{ "instances" });
    if (!instances)
    {
        return true;
    }
    if (query.value_kind(instances) != EDocumentValueKind::object)
    {
        return false;
    }
    for (CInstanceHandle group = query.first_child(instances); group; group = query.next_sibling(group))
    {
        const CSchemaIndex type = schema.find_type(query.name(group));
        SType layout;
        bool has_work{};
        if (!query.is_object_entry(group) || (query.value_kind(group) != EDocumentValueKind::object) ||
            !schema.type(type, layout) || !types.validate(type, has_work))
        {
            return false;
        }
        TPodVector<CInstanceHandle> pending;
        for (CInstanceHandle entry = query.first_child(group); entry; entry = query.next_sibling(entry))
        {
            if (!pending.push_back(entry))
            {
                return false;
            }
        }
        CInstanceHandle entry;
        while (pending.pop_back(entry))
        {
            if (!query.is_object_entry(entry) || (query.value_kind(entry) != EDocumentValueKind::object))
            {
                return false;
            }
            for (CInstanceHandle property = query.first_child(entry); property; property = query.next_sibling(property))
            {
                const CStringView name = query.name(property);
                if (!(name == CStringView{ "locator" }) && !(name == CStringView{ "declaration" }) &&
                    !(name == CStringView{ "specialisation" }))
                {
                    return false;
                }
            }
            CInstanceHandle count_node, size_node;
            std::uint64_t offset{}, count{}, size{};
            if (!locator(query, entry, count_node, size_node, offset) ||
                (count_node && (!query.unsigned_integer_value(count_node, count) || (count != 1u))) ||
                (size_node && (!query.unsigned_integer_value(size_node, size) || (size != layout.size))) ||
                !extents.push_back({ offset, layout.size, type, 1u, has_work }))
            {
                return false;
            }
            const CInstanceHandle children = query.object_child(entry, CStringView{ "specialisation" });
            if (children)
            {
                if (query.value_kind(children) != EDocumentValueKind::object)
                {
                    return false;
                }
                for (CInstanceHandle child = query.first_child(children); child; child = query.next_sibling(child))
                {
                    if (!pending.push_back(child))
                    {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

[[nodiscard]] static bool collect_bulk_data(const CResolvedSchema& schema, const CBulkDataDocumentQuery& query,
    CClearTypes& types, TPodVector<SClearExtent>& extents) noexcept
{
    const CBulkDataHandle root = query.root();
    if (!query.is_ready() || (query.value_kind(root) != EDocumentValueKind::object))
    {
        return false;
    }
    const CBulkDataHandle data = query.object_child(root, CStringView{ "data" });
    if (!data)
    {
        return true;
    }
    if (query.value_kind(data) != EDocumentValueKind::object)
    {
        return false;
    }
    for (CBulkDataHandle group = query.first_child(data); group; group = query.next_sibling(group))
    {
        const CSchemaIndex type = schema.find_type(query.name(group));
        SType layout;
        bool has_work{};
        if (!query.is_object_entry(group) || (query.value_kind(group) != EDocumentValueKind::object) ||
            !schema.type(type, layout) || !types.validate(type, has_work))
        {
            return false;
        }
        for (CBulkDataHandle entry = query.first_child(group); entry; entry = query.next_sibling(entry))
        {
            if (!query.is_object_entry(entry) || (query.value_kind(entry) != EDocumentValueKind::object))
            {
                return false;
            }
            for (CBulkDataHandle property = query.first_child(entry); property; property = query.next_sibling(property))
            {
                const CStringView name = query.name(property);
                if (!(name == CStringView{ "locator" }) && !(name == CStringView{ "data" }))
                {
                    return false;
                }
            }
            CBulkDataHandle count_node, size_node;
            std::uint64_t offset{}, stated_count{}, stated_size{};
            if (!locator(query, entry, count_node, size_node, offset) ||
                (count_node && (!query.unsigned_integer_value(count_node, stated_count) || (stated_count > UINT32_MAX))) ||
                (size_node && (!query.unsigned_integer_value(size_node, stated_size) || (stated_size > memory::k_byte_size_ceiling))))
            {
                return false;
            }
            const CBulkDataHandle embedded = query.object_child(entry, CStringView{ "data" });
            std::uint64_t count{};
            if (embedded)
            {
                if (query.value_kind(embedded) != EDocumentValueKind::array)
                {
                    return false;
                }
                count = query.child_count(embedded);
                if (((count == 0u) && !count_node) || (count_node && (stated_count != count)))
                {
                    return false;
                }
                for (CBulkDataHandle value = query.first_child(embedded); value; value = query.next_sibling(value))
                {
                    if (query.is_object_entry(value) &&
                        !((layout.category == ECategory::structure) ? schema.find_member(type, query.name(value)) :
                            ((layout.category == ECategory::bit_structure) ? schema.find_field(type, query.name(value)) : CSchemaIndex{})))
                    {
                        return false;
                    }
                }
            }
            else if (count_node)
            {
                count = stated_count;
            }
            else if (size_node && (layout.size != 0u) && ((stated_size % layout.size) == 0u))
            {
                count = stated_size / layout.size;
            }
            if (((count == 0u) && !count_node) || (count > UINT32_MAX) ||
                ((layout.size != 0u) && (count > (memory::k_byte_size_ceiling / layout.size))))
            {
                return false;
            }
            const std::uint64_t extent = layout.size * count;
            if ((size_node && (stated_size != extent)) ||
                !extents.push_back({ offset, extent, type, static_cast<std::uint32_t>(count), has_work }))
            {
                return false;
            }
        }
    }
    return true;
}

//==============================================================================
//  Extent preflight and publication
//==============================================================================

[[nodiscard]] static bool preflight_extents(const CResolvedSchema& schema, const CByteView payload, TPodVector<SClearExtent>& extents) noexcept
{
    if ((payload.size() != 0u) && (!payload.is_ready() || (payload.data() == nullptr)))
    {
        return false;
    }
    if (extents.size() > 1u)
    {
        std::sort(extents.data(), (extents.data() + extents.size()),
            [](const SClearExtent& a, const SClearExtent& b) noexcept
            {
                return (a.offset < b.offset) || ((a.offset == b.offset) && (a.size < b.size));
            });
    }
    std::uint64_t cursor{};
    for (std::size_t ordinal = 0u; ordinal < extents.size(); ++ordinal)
    {
        const SClearExtent& extent = extents[ordinal];
        SType layout;
        if (!schema.type(extent.type, layout) || (layout.alignment == 0u) ||
            (extent.offset > payload.size()) || (extent.size > (payload.size() - extent.offset)) ||
            ((extent.offset % layout.alignment) != 0u) ||
            ((extent.size != 0u) && ((reinterpret_cast<std::uintptr_t>(payload.data() + extent.offset) % layout.alignment) != 0u)))
        {
            return false;
        }
        if (extent.size != 0u)
        {
            if (extent.offset < cursor)
            {
                return false;
            }
            cursor = extent.offset + extent.size;
        }
    }
    return true;
}

static void clear_extents(const CResolvedSchema& schema, const CByteView payload, const TPodVector<SClearExtent>& extents,
    const EUnusedBits bits, const CClearTypes& types) noexcept
{
    std::size_t cursor{};
    for (std::size_t ordinal = 0u; ordinal < extents.size(); ++ordinal)
    {
        const SClearExtent& extent = extents[ordinal];
        if (extent.size == 0u)
        {
            continue;
        }
        const std::size_t offset = static_cast<std::size_t>(extent.offset);
        if (offset > cursor)
        {
            std::memset((payload.data() + cursor), 0, (offset - cursor));
        }
        if (extent.has_work)
        {
            SType layout;
            (void)schema.type(extent.type, layout);
            for (std::uint32_t record = 0u; record < extent.count; ++record)
            {
                clear_type(schema, extent.type, (payload.data() + offset + (static_cast<std::size_t>(layout.size) * record)), bits, types);
            }
        }
        cursor = offset + static_cast<std::size_t>(extent.size);
    }
    if (cursor < payload.size())
    {
        std::memset((payload.data() + cursor), 0, (payload.size() - cursor));
    }
}

bool clear_unused_storage(const CResolvedSchema& schema, const CSchemaIndex type, const CByteView values, const EUnusedBits bits) noexcept
{
    SType layout;
    bool has_work{};
    CClearTypes types{ schema, bits };
    if (!schema.is_ready() || !valid_bits(bits) || !schema.type(type, layout) || !types.validate(type, has_work))
    {
        return false;
    }
    if (layout.size == 0u)
    {
        return values.is_empty();
    }
    if ((layout.stride == 0u) || (layout.stride > std::numeric_limits<std::size_t>::max()) ||
        ((values.size() % static_cast<std::size_t>(layout.stride)) != 0u) ||
        ((values.size() % static_cast<std::size_t>(layout.size)) != 0u))
    {
        return false;
    }
    if (values.is_empty())
    {
        return true;
    }
    if (!values.is_ready() || (values.data() == nullptr) ||
        ((reinterpret_cast<std::uintptr_t>(values.data()) % layout.alignment) != 0u))
    {
        return false;
    }
    if (has_work)
    {
        const std::size_t count = values.size() / static_cast<std::size_t>(layout.size);
        for (std::size_t record = 0u; record < count; ++record)
        {
            clear_type(schema, type, (values.data() + (record * static_cast<std::size_t>(layout.size))), bits, types);
        }
    }
    return true;
}

bool clear_unused_storage(const CResolvedSchema& schema, const CInstanceDocumentQuery& document, const CByteView payload, const EUnusedBits bits) noexcept
{
    TPodVector<SClearExtent> extents;
    CClearTypes types{ schema, bits };
    if (!schema.is_ready() || !valid_bits(bits) || !collect_instances(schema, document, types, extents) || !preflight_extents(schema, payload, extents))
    {
        return false;
    }
    clear_extents(schema, payload, extents, bits, types);
    return true;
}

bool clear_unused_storage(const CResolvedSchema& schema, const CBulkDataDocumentQuery& document, const CByteView payload, const EUnusedBits bits) noexcept
{
    TPodVector<SClearExtent> extents;
    CClearTypes types{ schema, bits };
    if (!schema.is_ready() || !valid_bits(bits) || !collect_bulk_data(schema, document, types, extents) || !preflight_extents(schema, payload, extents))
    {
        return false;
    }
    clear_extents(schema, payload, extents, bits, types);
    return true;
}

}   // namespace schema
