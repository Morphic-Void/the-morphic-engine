
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    bundle_builder.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Oct 26
//
//  Private live-document construction followed by one bake and direct reads.

#include "assets/internal/bundle_builder_io.hpp"

#include <algorithm>
#include <utility>

#include "data_model/document_translation.hpp"
#include "filesystem/logical_path.hpp"
#include "platform/filesystem/file.hpp"

namespace bundle
{

enum class ESourceKind : std::uint8_t { file = 0, directory, other };

struct SEntry
{
    CNodeKey source;
    CNodeKey value;
    CNodeKey content;
    const char* path{ nullptr };
    std::size_t size{ 0u };
    ESourceKind kind{ ESourceKind::file };
};

struct SFrame { CNodeKey next; CNodeKey content; };
struct SRead { CNodeKey source; const char* path; std::size_t offset; std::size_t size; };

template<class TDocument, class TKey>
[[nodiscard]] static TKey child(const TDocument& doc, const TKey parent, const char* const name) noexcept
{
    return doc.object_child(parent, CStringView{ name });
}

template<class TDocument, class TKey>
[[nodiscard]] static TKey next_entry(const TDocument& doc, const TKey root_content, TKey entry) noexcept
{
    const TKey content = child(doc, entry, "content");
    if (doc.first_child(content).is_valid()) { return doc.first_child(content); }
    while (entry.is_valid() && !doc.next_sibling(entry).is_valid())
    {
        const TKey parent = doc.parent(entry);
        entry = (parent == root_content) ? TKey{} : doc.parent(parent);
    }
    return entry.is_valid() ? doc.next_sibling(entry) : TKey{};
}

static bool add(CLiveDocument& doc, const CNodeKey parent, const CNodeKey value) noexcept
{
    return value.is_valid() && doc.append_child(parent, value).succeeded();
}

static const SBuildSource* find_source(const TPodVector<SBuildSource>& sources, const CNodeKey entry) noexcept
{
    if (sources.size() == 0u) { return nullptr; }
    const auto* end = sources.data() + sources.size();
    const auto* found = std::lower_bound(sources.data(), end, entry,
        [](const SBuildSource& source, const CNodeKey key) { return source.entry.query_value() < key.query_value(); });
    return ((found != end) && (found->entry == entry)) ? found : nullptr;
}

static SBuildResult copy_inventory(const CLiveDocument& source, const CNodeKey source_content,
    const TPodVector<SBuildSource>& paths, CLiveDocument& doc, const CNodeKey root_content, TPodVector<SEntry>& entries) noexcept
{
    TPodVector<SFrame> stack;
    if (!stack.push_back(SFrame{ source.first_child(source_content), root_content })) { return { EBuildStatus::allocation_failed, {} }; }
    std::size_t files = 0u;
    while (stack.size() != 0u)
    {
        const SFrame frame = stack[stack.size() - 1u];
        if (!frame.next.is_valid()) { (void)stack.discard_back(); continue; }
        const CNodeKey entry = frame.next;
        stack[stack.size() - 1u].next = source.next_sibling(entry);
        const CStringView name = source.name(entry);
        const CNodeKey spelling = child(source, entry, "physicalName");
        const CStringView physical = spelling.is_valid() ? source.string_value(spelling) : name;
        const CNodeKey content = child(source, entry, "content");
        const CNodeKey kind = child(source, entry, "kind");
        if ((source.value_type(entry) != ELiveValueType::object) || !logical_path::valid_component(name) ||
            !logical_path::is_folded(name) || !logical_path::valid_component(physical) || !logical_path::case_equal(name, physical) ||
            (content.is_valid() && ((source.value_type(content) != ELiveValueType::object) || kind.is_valid())) ||
            (kind.is_valid() && !(source.string_value(kind) == CStringView{ "other" })))
        {
            return { EBuildStatus::invalid_input, entry };
        }
        SEntry copied;
        copied.source = entry;
        copied.kind = content.is_valid() ? ESourceKind::directory : (kind.is_valid() ? ESourceKind::other : ESourceKind::file);
        copied.value = doc.create_object(name);
        if (!add(doc, frame.content, copied.value) ||
            (!(physical == name) && !add(doc, copied.value, doc.create_string(physical, CStringView{ "physicalName" }))))
        {
            return { EBuildStatus::allocation_failed, entry };
        }
        if (copied.kind == ESourceKind::directory)
        {
            copied.content = doc.create_object(CStringView{ "content" });
            if (!add(doc, copied.value, copied.content) || !stack.push_back(SFrame{ source.first_child(content), copied.content }))
            {
                return { EBuildStatus::allocation_failed, entry };
            }
        }
        else if (copied.kind == ESourceKind::file)
        {
            const SBuildSource* const path = find_source(paths, entry);
            if (path == nullptr) { return { EBuildStatus::invalid_input, entry }; }
            copied.path = path->physical_path;
            ++files;
            if (!add(doc, copied.value, doc.create_unsigned_integer(0u, CStringView{ "offset" })) ||
                !add(doc, copied.value, doc.create_unsigned_integer(0u, CStringView{ "size" })))
            {
                return { EBuildStatus::allocation_failed, entry };
            }
        }
        if (!entries.push_back(copied)) { return { EBuildStatus::allocation_failed, entry }; }
    }
    return (files == paths.size()) ? SBuildResult{} : SBuildResult{ EBuildStatus::invalid_input, source_content };
}

static SBuildResult populate_and_prune(CLiveDocument& doc, TPodVector<SEntry>& entries, const detail::SBuildIo& io) noexcept
{
    for (std::size_t i = 0u; i < entries.size(); ++i)
    {
        SEntry& entry = entries[i];
        if (entry.kind != ESourceKind::file) { continue; }
        if (!io.query_size(io.state, entry.path, entry.size)) { return { EBuildStatus::size_query_failed, entry.source }; }
        if (entry.size >= memory::k_byte_size_ceiling) { return { EBuildStatus::too_large, entry.source }; }
        const CNodeKey size = child(doc, entry.value, "size");
        const CNodeKey replacement = doc.create_unsigned_integer(entry.size);
        if (!replacement.is_valid() || !doc.erase_payload(size) || !doc.attach_payload(size, replacement).is_valid())
        {
            return { EBuildStatus::allocation_failed, entry.source };
        }
    }

    //  The records are in input preorder, so descendants are pruned first.
    for (std::size_t i = entries.size(); i != 0u; --i)
    {
        const SEntry& entry = entries[i - 1u];
        if ((entry.kind == ESourceKind::other) || ((entry.kind == ESourceKind::file) && (entry.size == 0u)) ||
            ((entry.kind == ESourceKind::directory) && (doc.child_count(entry.content) == 0u)))
        {
            if (!doc.erase(entry.value)) { return { EBuildStatus::invalid_input, entry.source }; }
        }
    }
    return {};
}

static bool sort_children(CLiveDocument& doc, const CNodeKey content, TPodVector<CNodeKey>& scratch) noexcept
{
    scratch.clear();
    for (CNodeKey entry = doc.first_child(content); entry.is_valid(); entry = doc.next_sibling(entry))
    {
        if (!scratch.push_back(entry)) { return false; }
    }
    if (scratch.size() < 2u) { return true; }

    //  CStringView compares unsigned bytes and then length, without locale.
    std::sort(scratch.data(), scratch.data() + scratch.size(),
        [&doc](const CNodeKey a, const CNodeKey b) { return doc.name(a) < doc.name(b); });
    for (std::size_t i = 0u; i < scratch.size(); ++i) { if (!doc.detach(scratch[i])) { return false; } }
    for (std::size_t i = 0u; i < scratch.size(); ++i) { if (!add(doc, content, scratch[i])) { return false; } }
    return true;
}

static SBuildResult plan_payload(const CLiveDocument& live, const CNodeKey live_content, TPodVector<SEntry>& entries,
    CBakedDocumentBlock& block, TPodVector<SRead>& reads, std::size_t& total) noexcept
{
    std::sort(entries.data(), entries.data() + entries.size(),
        [](const SEntry& a, const SEntry& b) { return a.value.query_value() < b.value.query_value(); });
    CMutableBakedDocument editor{ block };
    const CBakedDocument& doc = editor.baked();
    const CBakedValueIndex content = child(doc, doc.root(), "content");
    CNodeKey live_entry = live.first_child(live_content);
    std::uint64_t end = (static_cast<std::uint64_t>(doc.byte_count()) + 127u) & ~std::uint64_t{ 127u };
    for (CBakedValueIndex entry = doc.first_child(content); entry.is_valid();
        entry = next_entry(doc, content, entry), live_entry = next_entry(live, live_content, live_entry))
    {
        if (child(doc, entry, "content").is_valid()) { continue; }
        const auto* record = std::lower_bound(entries.data(), entries.data() + entries.size(), live_entry,
            [](const SEntry& value, const CNodeKey key) { return value.value.query_value() < key.query_value(); });
        if ((record == entries.data() + entries.size()) || (record->value != live_entry)) { return { EBuildStatus::bake_failed, {} }; }
        const std::uint64_t reserved = (static_cast<std::uint64_t>(record->size) + 128u) & ~std::uint64_t{ 127u };
        if ((end > memory::k_byte_size_ceiling) || (reserved > memory::k_byte_size_ceiling - end))
        {
            return { EBuildStatus::too_large, record->source };
        }
        if (!editor.set_unsigned_integer_value(child(doc, entry, "offset"), end)) { return { EBuildStatus::bake_failed, record->source }; }
        if (!reads.push_back(SRead{ record->source, record->path, static_cast<std::size_t>(end), record->size }))
        {
            return { EBuildStatus::allocation_failed, record->source };
        }
        end += reserved;
    }
    if (!editor.set_unsigned_integer_value(child(doc, doc.root(), "size"), end)) { return { EBuildStatus::bake_failed, {} }; }
    total = static_cast<std::size_t>(end);
    return {};
}

SBuildResult detail::build(const CLiveDocument& inventory, const CNodeKey content,
    const TPodConstView<SBuildSource> sources, CBakedDocumentBlock& destination, CBundleView& view, const SBuildIo& io) noexcept
{
    if (!inventory.is_ready() || (inventory.value_type(content) != ELiveValueType::object) ||
        (io.query_size == nullptr) || (io.read == nullptr)) { return { EBuildStatus::invalid_input, content }; }
    TPodVector<SBuildSource> paths;
    if ((sources.size() != 0u) && !paths.push_back(sources)) { return { EBuildStatus::allocation_failed, {} }; }
    if (paths.size() > 1u)
    {
        std::sort(paths.data(), paths.data() + paths.size(),
            [](const SBuildSource& a, const SBuildSource& b) { return a.entry.query_value() < b.entry.query_value(); });
    }
    for (std::size_t i = 0u; i < paths.size(); ++i)
    {
        if (!inventory.contains(paths[i].entry) || (paths[i].physical_path == nullptr) || (*paths[i].physical_path == '\0') ||
            ((i != 0u) && (paths[i - 1u].entry == paths[i].entry))) { return { EBuildStatus::invalid_input, paths[i].entry }; }
    }
    CLiveDocument doc;
    if (!doc.initialise() || !add(doc, doc.root(), doc.create_string(CStringView{ "morphic-bundle" }, CStringView{ "fileType" })) ||
        !add(doc, doc.root(), doc.create_unsigned_integer(k_version, CStringView{ "version" })) ||
        !add(doc, doc.root(), doc.create_unsigned_integer(0u, CStringView{ "size" }))) { return { EBuildStatus::allocation_failed, {} }; }
    const CNodeKey root_content = doc.create_object(CStringView{ "content" });
    if (!add(doc, doc.root(), root_content)) { return { EBuildStatus::allocation_failed, {} }; }
    TPodVector<SEntry> entries;
    SBuildResult result = copy_inventory(inventory, content, paths, doc, root_content, entries);
    if (!result.succeeded()) { return result; }
    result = populate_and_prune(doc, entries, io);
    if (!result.succeeded()) { return result; }
    if (doc.child_count(root_content) == 0u) { return { EBuildStatus::no_content, content }; }
    TPodVector<CNodeKey> scratch;
    if (!sort_children(doc, root_content, scratch)) { return { EBuildStatus::allocation_failed, content }; }
    for (std::size_t i = 0u; i < entries.size(); ++i)
    {
        const SEntry& entry = entries[i];
        if ((entry.kind == ESourceKind::directory) && doc.contains(entry.value) && !sort_children(doc, entry.content, scratch))
        {
            return { EBuildStatus::allocation_failed, entry.source };
        }
    }
    CBakedDocumentBlock block;
    if (!document_translation::bake(doc, block)) { return { EBuildStatus::bake_failed, {} }; }
    TPodVector<SRead> reads;
    std::size_t total = 0u;
    result = plan_payload(doc, root_content, entries, block, reads, total);
    if (!result.succeeded()) { return result; }

    //  plan_payload's baked views are gone before this potentially relocating call.
    if (!block.resize_buffer(total, k_alignment)) { return { EBuildStatus::allocation_failed, {} }; }
    for (std::size_t i = 0u; i < reads.size(); ++i)
    {
        const SRead& range = reads[i];
        if (!io.read(io.state, range.path, CByteView{ block.buffer_bytes().data() + range.offset, range.size, k_alignment }))
        {
            return { EBuildStatus::read_failed, range.source };
        }
    }
    CBundleView checked;
    const SValidationResult validation = checked.reset(block.buffer_bytes().data(), block.buffer_bytes().size());
    if (!validation.succeeded()) { return { EBuildStatus::validation_failed, {}, validation.status }; }
    destination = std::move(block);
    view = checked;
    return {};
}

static bool query_size(void*, const char* const path, std::size_t& size) noexcept
{
    return platform::filesystem::queryFileSize(path, size);
}

static bool read_file(void*, const char* const path, const CByteView& bytes) noexcept
{
    return platform::filesystem::loadFileInto(path, bytes);
}

SBuildResult build(const CLiveDocument& inventory, const CNodeKey content, const TPodConstView<SBuildSource> sources,
    CBakedDocumentBlock& destination, CBundleView& view) noexcept
{
    return detail::build(inventory, content, sources, destination, view, detail::SBuildIo{ nullptr, &query_size, &read_file });
}

}   // namespace bundle
