
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    bundle.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Oct 26
//
//  Bundle validation, checked adoption and strict immutable lookup.

#include "assets/bundle.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include "containers/TPodVector.hpp"
#include "filesystem/logical_path.hpp"

namespace bundle
{

static CBakedValueIndex child(const CBakedDocument& doc, const CBakedValueIndex parent, const char* name) noexcept
{
    return doc.object_child(parent, CStringView{ name });
}

static bool unsigned_value(const CBakedDocument& doc, const CBakedValueIndex parent, const char* name, std::uint64_t& value) noexcept
{
    return doc.unsigned_integer_value(child(doc, parent, name), value);
}

struct SExtent
{
    std::uint32_t offset;
    std::uint32_t end;
    CBakedValueIndex entry;
};

static SValidationResult validate(const CBakedDocument& doc, const std::uint8_t* bytes, const std::size_t size) noexcept
{
    const CBakedValueIndex root = doc.root();
    std::uint64_t version = 0u;
    std::uint64_t declared_size = 0u;
    const CBakedValueIndex root_content = child(doc, root, "content");
    if ((doc.value_type(root) != EBakedValueType::object) || (doc.child_count(root) != 4u) ||
        !(doc.string_value(child(doc, root, "fileType")) == CStringView{ "morphic-bundle" }) ||
        !unsigned_value(doc, root, "version", version) || !unsigned_value(doc, root, "size", declared_size) ||
        (doc.value_type(root_content) != EBakedValueType::object) || (doc.child_count(root_content) == 0u))
    {
        return { EValidationStatus::invalid_schema, root };
    }
    if (version != k_version) { return { EValidationStatus::unsupported_version, child(doc, root, "version") }; }
    if (declared_size != size) { return { EValidationStatus::invalid_extent, child(doc, root, "size") }; }

    const std::uint64_t directory_end = (static_cast<std::uint64_t>(doc.byte_count()) + 127u) & ~std::uint64_t{ 127u };
    TPodVector<SExtent> extents;

    //  Walk using checked parent/sibling links rather than a recursion depth or
    //  a stack proportional to nesting. No bundle-specific depth limit is needed.
    CBakedValueIndex entry = doc.first_child(root_content);
    while (entry.is_valid())
    {
        const CStringView name = doc.name(entry);
        if (!logical_path::valid_component(name) || !logical_path::is_folded(name))
        {
            return { EValidationStatus::invalid_name, entry };
        }
        const CBakedValueIndex spelling = child(doc, entry, "physicalName");
        if (spelling.is_valid())
        {
            const CStringView physical = doc.string_value(spelling);
            if (!logical_path::valid_component(physical) || !logical_path::case_equal(name, physical) || (name == physical))
            {
                return { EValidationStatus::invalid_name, spelling };
            }
        }
        const CBakedValueIndex content = child(doc, entry, "content");
        const bool directory = content.is_valid();
        const std::uint32_t expected = (directory ? 1u : 2u) + (spelling.is_valid() ? 1u : 0u);
        if ((doc.value_type(entry) != EBakedValueType::object) || (doc.child_count(entry) != expected))
        {
            return { EValidationStatus::invalid_schema, entry };
        }
        if (directory)
        {
            if ((doc.value_type(content) != EBakedValueType::object) || (doc.child_count(content) == 0u))
            {
                return { EValidationStatus::invalid_schema, content };
            }
            entry = doc.first_child(content);
            continue;
        }

        std::uint64_t offset = 0u;
        std::uint64_t length = 0u;
        if (!unsigned_value(doc, entry, "offset", offset) || !unsigned_value(doc, entry, "size", length))
        {
            return { EValidationStatus::invalid_schema, entry };
        }

        //  Bound values before adding/rounding. These operations then fit uint64
        //  even on Win32, without allocating buffers near the allocation ceiling.
        if ((length == 0u) || (length > size) || (offset < directory_end) || (offset >= size) ||
            ((offset % k_alignment) != 0u)) { return { EValidationStatus::invalid_extent, entry }; }
        const std::uint64_t end = offset + ((length + 128u) & ~std::uint64_t{ 127u });
        if (end > size) { return { EValidationStatus::invalid_extent, entry }; }
        if (bytes[static_cast<std::size_t>(offset + length)] != 0u)
        {
            return { EValidationStatus::missing_terminator, entry };
        }
        if (!extents.push_back(SExtent{ static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(end), entry }))
        {
            return { EValidationStatus::allocation_failed, entry };
        }
        while (entry.is_valid() && !doc.next_sibling(entry).is_valid())
        {
            const CBakedValueIndex parent_content = doc.parent(entry);
            entry = (parent_content == root_content) ? CBakedValueIndex{} : doc.parent(parent_content);
        }
        if (entry.is_valid()) { entry = doc.next_sibling(entry); }
    }
    std::sort(extents.data(), extents.data() + extents.size(), [](const SExtent& a, const SExtent& b) { return a.offset < b.offset; });
    std::uint64_t next = directory_end;
    for (std::size_t i = 0u; i < extents.size(); ++i)
    {
        if (extents[i].offset != next) { return { EValidationStatus::invalid_extent, extents[i].entry }; }
        next = extents[i].end;
    }
    return (next == size) ? SValidationResult{} : SValidationResult{ EValidationStatus::invalid_extent, root };
}

SValidationResult CBundleView::reset(const void* const bytes, const std::size_t size) noexcept
{
    clear();
    if ((bytes == nullptr) || (size < sizeof(SBakedDocumentHeader)) || (size > memory::k_byte_size_ceiling) ||
        ((size % k_alignment) != 0u) || ((reinterpret_cast<std::uintptr_t>(bytes) % k_alignment) != 0u))
    {
        return { EValidationStatus::invalid_buffer, {} };
    }
    SBakedDocumentHeader header{};
    std::memcpy(&header, bytes, sizeof(header));
    CBakedDocument document;
    if ((header.total_size > size) || !document.reset(bytes, header.total_size))
    {
        return { EValidationStatus::document_validation_failed, {} };
    }
    const SValidationResult result = validate(document, static_cast<const std::uint8_t*>(bytes), size);
    if (result.succeeded())
    {
        m_document = document;
        (void)m_bytes.set(bytes, size, 1u, k_alignment);
    }
    return result;
}

SLookupResult CBundleView::lookup(const CStringView path) const noexcept
{
    if (!is_ready()) { return {}; }

    //  Validate the whole path before traversal so a missing prefix cannot hide
    //  invalid syntax in a later component.
    if (!logical_path::valid_relative_file(path)) { return { ELookupStatus::invalid_path, {}, {} }; }
    CBakedValueIndex content = child(m_document, m_document.root(), "content");
    std::size_t start = 0u;
    for (std::size_t i = 0u; i <= path.length(); ++i)
    {
        if ((i != path.length()) && (path.cstring()[i] != '/')) { continue; }
        const CStringView component{ path.cstring() + start, i - start };
        CBakedValueIndex entry = m_document.first_child(content);
        while (entry.is_valid() && !logical_path::case_equal(m_document.name(entry), component))
        {
            entry = m_document.next_sibling(entry);
        }
        if (!entry.is_valid()) { return { ELookupStatus::not_found, {}, {} }; }
        content = child(m_document, entry, "content");
        if (i == path.length())
        {
            if (content.is_valid()) { return { ELookupStatus::not_file, {}, entry }; }
            std::uint64_t offset = 0u;
            std::uint64_t size = 0u;
            (void)unsigned_value(m_document, entry, "offset", offset);
            (void)unsigned_value(m_document, entry, "size", size);
            return { ELookupStatus::success, m_bytes.subview(static_cast<std::size_t>(offset), static_cast<std::size_t>(size)), entry };
        }
        if (!content.is_valid()) { return { ELookupStatus::not_found, {}, {} }; }
        start = i + 1u;
    }
    return { ELookupStatus::not_found, {}, {} };
}

SValidationResult adopt(CByteBuffer&& source, CBakedDocumentBlock& destination, CBundleView& view) noexcept
{
    CBundleView candidate;
    const SValidationResult result = candidate.reset(source.data(), source.size());
    if (!result.succeeded()) { return result; }
    if (!destination.adopt_buffer(std::move(source)))
    {
        return { EValidationStatus::document_validation_failed, {} };
    }
    view = candidate;
    return {};
}

}   // namespace bundle
