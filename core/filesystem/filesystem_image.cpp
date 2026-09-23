
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    filesystem_image.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Sep 26
//
//  Directory-backed filesystem image construction and Host-owned reconciliation.

#include <cstring>
#include "filesystem/filesystem_image.hpp"
#include "data_model/baked_document.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "platform/filesystem/directory.hpp"
#include "platform/filesystem/file.hpp"

namespace filesystem_image
{

static CNodeKey child(const CLiveDocument& doc, const CNodeKey parent, const char* const name) noexcept
{
    return doc.object_child(parent, CStringView{ name });
}

static CStringView text(const CLiveDocument& doc, const CNodeKey parent, const char* const name) noexcept
{
    return doc.string_value(child(doc, parent, name));
}

static bool flag(const CLiveDocument& doc, const CNodeKey parent, const char* const name, const bool fallback = false) noexcept
{
    bool value = fallback;
    (void)doc.boolean_value(child(doc, parent, name), value);
    return value;
}

static std::uint64_t number(const CLiveDocument& doc, const CNodeKey parent, const char* const name) noexcept
{
    std::uint64_t value = 0u;
    (void)doc.unsigned_integer_value(child(doc, parent, name), value);
    return value;
}

static bool add(CLiveDocument& doc, const CNodeKey parent, const CNodeKey value) noexcept
{
    return value.is_valid() && doc.append_child(parent, value).succeeded();
}

static bool string(CLiveDocument& doc, const CNodeKey parent, const char* const name, const char* const value) noexcept
{
    return add(doc, parent, doc.create_string(CStringView{ value }, CStringView{ name }));
}

//  Allocate before mutation; attaching a detached payload does not allocate.
static bool replace(CLiveDocument& doc, const CNodeKey parent, const char* const name, const CNodeKey value) noexcept
{
    if (!value.is_valid())
    {
        return false;
    }
    const CNodeKey previous = child(doc, parent, name);
    if (previous.is_valid())
    {
        return doc.erase_payload(previous) && doc.attach_payload(previous, value).is_valid();
    }
    return doc.set_name(value, CStringView{ name }) && add(doc, parent, value);
}

static bool set_number(CLiveDocument& doc, const CNodeKey parent, const char* const name, const std::uint64_t value) noexcept
{
    return replace(doc, parent, name, doc.create_unsigned_integer(value));
}

static bool set_flag(CLiveDocument& doc, const CNodeKey parent, const char* const name, const bool value) noexcept
{
    return replace(doc, parent, name, doc.create_boolean(value));
}

static bool asset_id(CLiveDocument& doc, const CNodeKey entry, const std::uint64_t asset) noexcept
{
    const CNodeKey previous = child(doc, entry, "asset");
    return asset ? set_number(doc, entry, "asset", asset) : (!previous.is_valid() || doc.erase(previous));
}

static bool attach(CLiveDocument& doc, const CNodeKey target, const CNodeKey payload) noexcept
{
    return payload.is_valid() && doc.set_name(payload, {}) && doc.erase_payload(target) &&
        doc.attach_payload(target, payload).is_valid();
}

static bool normalized(const char* const path, CSimpleString& result) noexcept
{
    if (path == nullptr)
    {
        return false;
    }
    const std::size_t size = std::strlen(path);
    TPodVector<char> buffer;
    if (!buffer.resize(size + 1u))
    {
        return false;
    }
    for (std::size_t i = 0u; i <= size; ++i)
    {
        buffer[i] = (path[i] == '\\') ? '/' : path[i];
    }
    return result.set(buffer.data());
}

static bool join(const char* const prefix, const char* const suffix, CSimpleString& result) noexcept
{
    if ((prefix == nullptr) || (suffix == nullptr))
    {
        return false;
    }
    const std::size_t a = std::strlen(prefix);
    const std::size_t b = std::strlen(suffix);
    const bool separator = (a != 0u) && (prefix[a - 1u] != '/') && (prefix[a - 1u] != '\\');
    TPodVector<char> buffer;
    if (!buffer.resize(a + b + 2u))
    {
        return false;
    }
    std::memcpy(buffer.data(), prefix, a);
    if (separator) { buffer[a] = '/'; }
    std::memcpy(buffer.data() + a + (separator ? 1u : 0u), suffix, b + 1u);
    return normalized(buffer.data(), result);
}

static bool relative_file(const char* const path) noexcept
{
    if ((path == nullptr) || (*path == '\0'))
    {
        return false;
    }
    const char* component = path;
    for (const char* next = path;; ++next)
    {
        if ((*next == '\\') || (*next == ':') || (*next == '*') || (*next == '?'))
        {
            return false;
        }
        if ((*next == '/') || (*next == '\0'))
        {
            const auto size = static_cast<std::size_t>(next - component);
            if ((size == 0u) || ((size == 1u) && (*component == '.')) ||
                ((size == 2u) && (component[0] == '.') && (component[1] == '.')))
            {
                return false;
            }
            if (*next == '\0') { return true; }
            component = next + 1;
        }
    }
}

static bool equal_path(const CStringView a, const char* const b) noexcept
{
    return (b != nullptr) && (a == CStringView{ b });
}

static bool case_alias(const CStringView a, const char* const b) noexcept
{
    if ((b == nullptr) || (a.length() != std::strlen(b)))
    {
        return false;
    }
    for (std::size_t i = 0u; i < a.length(); ++i)
    {
        const char x = a.cstring()[i];
        const char y = b[i];
        const char folded_x = ((x >= 'A') && (x <= 'Z')) ? static_cast<char>(x + ('a' - 'A')) : x;
        const char folded_y = ((y >= 'A') && (y <= 'Z')) ? static_cast<char>(y + ('a' - 'A')) : y;
        if (folded_x != folded_y) { return false; }
    }
    return true;
}

static bool collision(const CLiveDocument& doc, const CNodeKey content, const char* const name) noexcept
{
    for (CNodeKey entry = doc.first_child(content); entry.is_valid(); entry = doc.next_sibling(entry))
    {
        if (case_alias(doc.name(entry), name)) { return true; }
    }
    return false;
}

static CNodeKey find_root(const CLiveDocument& doc, const char* const logical) noexcept
{
    return child(doc, child(doc, doc.root(), "roots"), logical);
}

static CNodeKey file_root(const CLiveDocument& doc, const char* const file, const char*& relative) noexcept
{
    relative = nullptr;
    if (file == nullptr) { return {}; }
    const char* colon = std::strchr(file, ':');
    if ((colon == nullptr) || (colon[1] != '/') || !relative_file(colon + 2)) { return {}; }
    const CNodeKey root = doc.object_child(child(doc, doc.root(), "roots"),
        CStringView{ file, static_cast<std::size_t>(colon - file) + 1u });
    if (root.is_valid()) { relative = colon + 2; }
    return root;
}

static bool is_file(const CLiveDocument& doc, const CNodeKey node) noexcept
{
    return (doc.value_type(node) == ELiveValueType::object) &&
        !child(doc, node, "content").is_valid() && !child(doc, node, "kind").is_valid();
}

static CNodeKey file_entry(const CLiveDocument& doc, const char* const file) noexcept
{
    const char* relative = nullptr;
    CNodeKey node = file_root(doc, file, relative);
    while (node.is_valid() && (relative != nullptr))
    {
        const char* slash = std::strchr(relative, '/');
        node = doc.object_child(child(doc, node, "content"),
            CStringView{ relative, slash ? static_cast<std::size_t>(slash - relative) : std::strlen(relative) });
        relative = slash ? slash + 1 : nullptr;
    }
    return node;
}

//  Walk the named hierarchy, inheriting bindings and permissions.
static bool resolve_file(const CLiveDocument& doc, const char* const file, const bool writing, CSimpleString& physical) noexcept
{
    const char* relative = nullptr;
    CNodeKey node = file_root(doc, file, relative);
    if (!node.is_valid()) { return false; }
    bool writable = flag(doc, node, "writable");
    if (!flag(doc, node, "inventory", true))
    {
        return writing && writable && join(text(doc, node, "source").cstring(), relative, physical);
    }
    CSimpleString path;
    if (!path.set(text(doc, node, "source").cstring())) { return false; }
    for (;;)
    {
        const char* slash = std::strchr(relative, '/');
        CSimpleString component;
        if (!component.set(relative, slash ? static_cast<std::size_t>(slash - relative) : std::strlen(relative)))
        {
            return false;
        }
        const CNodeKey content = child(doc, node, "content");
        node = child(doc, content, component.cstring());
        if (!join(path.cstring(), component.cstring(), path)) { return false; }
        if (!node.is_valid())
        {
            return writing && writable && (slash == nullptr) &&
                !collision(doc, content, component.cstring()) && physical.set(path.cstring());
        }
        writable = flag(doc, node, "writable", writable);
        const CStringView source = text(doc, node, "source");
        if ((source.length() != 0u) && !path.set(source.cstring())) { return false; }
        if (slash == nullptr)
        {
            return is_file(doc, node) && (!writing || writable) && physical.set(path.cstring());
        }
        if (doc.value_type(child(doc, node, "content")) != ELiveValueType::object) { return false; }
        relative = slash + 1;
    }
}

//  Copy only schema values, between different documents. No string view into
//  the destination survives an allocation.
static CNodeKey copy_node(CLiveDocument& destination, const CLiveDocument& source, const CNodeKey node,
    const bool metadata_only = false, const std::uint32_t depth = 0u) noexcept
{
    if (depth > 512u) { return {}; }
    const CStringView name = source.name(node);
    const ELiveValueType type = source.value_type(node);
    if (type == ELiveValueType::string) { return destination.create_string(source.string_value(node), name); }
    if (type == ELiveValueType::boolean)
    {
        bool value = false;
        (void)source.boolean_value(node, value);
        return destination.create_boolean(value, name);
    }
    if (type == ELiveValueType::integer)
    {
        std::uint64_t value = 0u;
        if (!source.unsigned_integer_value(node, value)) { return {}; }
        return destination.create_unsigned_integer(value, name);
    }
    if ((type != ELiveValueType::object) && (type != ELiveValueType::array)) { return {}; }
    const CNodeKey result = (type == ELiveValueType::object) ? destination.create_object(name) : destination.create_array(name);
    if (!result.is_valid()) { return {}; }
    for (CNodeKey item = source.first_child(node); item.is_valid(); item = source.next_sibling(item))
    {
        if (metadata_only && equal_path(source.name(item), "content")) { continue; }
        if (!add(destination, result, copy_node(destination, source, item, false, depth + 1u))) { return {}; }
    }
    return result;
}

struct SCrawl
{
    CLiveDocument& document;
    CNodeKey content;
    const char* physical;
    bool dlls_only;
    std::uint32_t depth;
    EScanStatus status{ EScanStatus::success };
};

static bool crawl(void* const opaque, const char* const name, const platform::filesystem::EDirectoryEntry kind) noexcept
{
    SCrawl& work = *static_cast<SCrawl*>(opaque);
    if (std::strcmp(name, ".gitkeep") == 0) { return true; }
    if (work.dlls_only)
    {
        const char* extension = std::strrchr(name, '.');
        if ((kind != platform::filesystem::EDirectoryEntry::file) ||
            (extension == nullptr) || !case_alias(CStringView{ extension }, ".dll")) { return true; }
    }
    if (!relative_file(name) || (std::strchr(name, '/') != nullptr)) { return false; }
    if (collision(work.document, work.content, name))
    {
        work.status = EScanStatus::name_collision;
        return false;
    }
    CLiveDocument& doc = work.document;
    const CNodeKey node = doc.create_object(CStringView{ name });
    if (!add(doc, work.content, node)) { return false; }
    if (kind == platform::filesystem::EDirectoryEntry::other) { return string(doc, node, "kind", "other"); }
    if (kind == platform::filesystem::EDirectoryEntry::directory)
    {
        const CNodeKey content = doc.create_object(CStringView{ "content" });
        CSimpleString physical;
        if ((work.depth >= 128u) || !add(doc, node, content) || !join(work.physical, name, physical)) { return false; }
        SCrawl sub{ doc, content, physical.cstring(), false, work.depth + 1u };
        const bool complete = platform::filesystem::query_directory(physical.cstring(), &crawl, &sub) ==
            platform::filesystem::EDirectoryQuery::complete;
        work.status = sub.status;
        return complete;
    }
    return true;
}

static EScanStatus query(SCrawl& crawl_work) noexcept
{
    if (platform::filesystem::query_directory(crawl_work.physical, &crawl, &crawl_work) ==
        platform::filesystem::EDirectoryQuery::complete) { return EScanStatus::success; }
    return (crawl_work.status == EScanStatus::success) ? EScanStatus::scan_failed : crawl_work.status;
}

static EScanStatus populate(CLiveDocument& doc, const CNodeKey roots, const SRootScan& request) noexcept
{
    CSimpleString physical;
    CSimpleString redirected;
    if (!normalized(request.physical_path.cstring(), physical) ||
        ((request.redirect_directory.length() != 0u) && !normalized(request.redirect_directory.cstring(), redirected)))
    {
        return EScanStatus::allocation_failed;
    }
    const CNodeKey root = doc.create_object(request.logical_root.view());
    if (!add(doc, roots, root) || !string(doc, root, "source", physical.cstring()) ||
        !set_flag(doc, root, "writable", request.writable))
    {
        return EScanStatus::allocation_failed;
    }
    if (!request.inventory)
    {
        return set_flag(doc, root, "inventory", false) ? EScanStatus::success : EScanStatus::allocation_failed;
    }
    const CNodeKey content = doc.create_object(CStringView{ "content" });
    if (!add(doc, root, content)) { return EScanStatus::allocation_failed; }
    SCrawl work{ doc, content, physical.cstring(), false, 0u };
    EScanStatus status = query(work);
    if (status != EScanStatus::success) { return status; }
    if (redirected.length() == 0u) { return EScanStatus::success; }

    CNodeKey bin = child(doc, content, "bin");
    if (bin.is_valid())
    {
        if (!child(doc, bin, "content").is_valid()) { return EScanStatus::name_collision; }
        //  Preserve any physical package/bin contributions across the overlay.
        //  Only these exceptional branches need an explicit source override.
        CSimpleString physical_bin;
        if (!join(physical.cstring(), "bin", physical_bin)) { return EScanStatus::allocation_failed; }
        for (CNodeKey entry = doc.first_child(child(doc, bin, "content")); entry.is_valid(); entry = doc.next_sibling(entry))
        {
            CSimpleString source;
            if (!join(physical_bin.cstring(), doc.name(entry).cstring(), source) ||
                !string(doc, entry, "source", source.cstring())) { return EScanStatus::allocation_failed; }
        }
    }
    else
    {
        if (collision(doc, content, "bin")) { return EScanStatus::name_collision; }
        bin = doc.create_object(CStringView{ "bin" });
        if (!add(doc, content, bin) || !add(doc, bin, doc.create_object(CStringView{ "content" })))
        {
            return EScanStatus::allocation_failed;
        }
    }
    const CNodeKey extensions = doc.create_array(CStringView{ "extensions" });
    if (!string(doc, bin, "source", redirected.cstring()) || !set_flag(doc, bin, "writable", false) ||
        !add(doc, bin, extensions) || !add(doc, extensions, doc.create_string(CStringView{ "dll" })))
    {
        return EScanStatus::allocation_failed;
    }
    SCrawl binaries{ doc, child(doc, bin, "content"), redirected.cstring(), true, 0u };
    return query(binaries);
}

//  A successful write may finish after refresh removed its ancestors. Restore
//  just the necessary branch, retaining old binding metadata when available.
static CNodeKey ensure_file(CLiveDocument& doc, const char* const file, const CLiveDocument* const previous = nullptr) noexcept
{
    const char* relative = nullptr;
    CNodeKey node = file_root(doc, file, relative);
    const char* ignored = nullptr;
    CNodeKey old = previous ? file_root(*previous, file, ignored) : CNodeKey{};
    while (node.is_valid())
    {
        const char* slash = std::strchr(relative, '/');
        CSimpleString name;
        if (!name.set(relative, slash ? static_cast<std::size_t>(slash - relative) : std::strlen(relative))) { return {}; }
        CNodeKey content = child(doc, node, "content");
        if (!content.is_valid())
        {
            if ((doc.value_type(node) != ELiveValueType::object) && !attach(doc, node, doc.create_object())) { return {}; }
            const CNodeKey kind = child(doc, node, "kind");
            if ((kind.is_valid() && !doc.erase(kind)) || !asset_id(doc, node, 0u)) { return {}; }
            content = doc.create_object(CStringView{ "content" });
            if (!add(doc, node, content)) { return {}; }
        }
        old = previous ? child(*previous, child(*previous, old, "content"), name.cstring()) : CNodeKey{};
        CNodeKey entry = child(doc, content, name.cstring());
        if (!entry.is_valid())
        {
            if (collision(doc, content, name.cstring())) { return {}; }
            entry = old.is_valid() ? copy_node(doc, *previous, old, true) : doc.create_object(name.view());
            if (!add(doc, content, entry)) { return {}; }
        }
        if (slash == nullptr)
        {
            if (!is_file(doc, entry) && !attach(doc, entry, doc.create_object())) { return {}; }
            return entry;
        }
        node = entry;
        relative = slash + 1;
    }
    return {};
}

EScanStatus scan_manifest(const char* const manifest, CLiveDocument& result) noexcept
{
    CByteBuffer bytes = platform::filesystem::loadFile(manifest);
    CLiveDocument configuration;
    if (!bytes.is_ready() || !document_parser::parse(bytes.const_view(), configuration).accepted() ||
        (number(configuration, configuration.root(), "schemaVersion") != 2u))
    {
        return EScanStatus::invalid_manifest;
    }
    CSimpleString path;
    CSimpleString base;
    if (!normalized(manifest, path)) { return EScanStatus::allocation_failed; }
    const char* slash = std::strrchr(path.cstring(), '/');
    if ((slash == nullptr) || !base.set(path.cstring(), static_cast<std::size_t>(slash - path.cstring())))
    {
        return EScanStatus::invalid_manifest;
    }
    const CNodeKey configured_roots = child(configuration, configuration.root(), "roots");
    if ((configuration.value_type(configured_roots) != ELiveValueType::object) ||
        (configuration.child_count(configured_roots) == 0u)) { return EScanStatus::invalid_manifest; }
    CLiveDocument candidate;
    if (!candidate.initialise() || !candidate.set_root_type(ELiveValueType::object) ||
        !set_number(candidate, candidate.root(), "schemaVersion", 2u)) { return EScanStatus::allocation_failed; }
    const CStringView profile = text(configuration, configuration.root(), "profile");
    if ((profile.length() != 0u) && !string(candidate, candidate.root(), "profile", profile.cstring()))
    {
        return EScanStatus::allocation_failed;
    }
    const CNodeKey roots = candidate.create_object(CStringView{ "roots" });
    if (!add(candidate, candidate.root(), roots)) { return EScanStatus::allocation_failed; }
    for (CNodeKey root = configuration.first_child(configured_roots); root.is_valid(); root = configuration.next_sibling(root))
    {
        const CStringView logical = configuration.name(root);
        CSimpleString identifier;
        const CStringView source = text(configuration, root, "source");
        SRootScan request;
        if ((logical.length() < 2u) || (logical.cstring()[logical.length() - 1u] != ':') ||
            !identifier.set(logical.cstring(), logical.length() - 1u) || !relative_file(identifier.cstring()) ||
            (std::strchr(identifier.cstring(), '/') != nullptr) || !relative_file(source.cstring()) ||
            !configuration.boolean_value(child(configuration, root, "writable"), request.writable) ||
            (child(configuration, root, "inventory").is_valid() &&
                !configuration.boolean_value(child(configuration, root, "inventory"), request.inventory)) ||
            collision(candidate, roots, logical.cstring()))
        {
            return EScanStatus::invalid_manifest;
        }
        if (!request.logical_root.set(logical.cstring()) || !join(base.cstring(), source.cstring(), request.physical_path))
        {
            return EScanStatus::allocation_failed;
        }
        const CNodeKey bindings = child(configuration, root, "content");
        if (bindings.is_valid())
        {
            const CNodeKey bin = child(configuration, bindings, "bin");
            const CNodeKey extensions = child(configuration, bin, "extensions");
            if (!equal_path(logical, "package:") || !request.inventory ||
                (configuration.value_type(bindings) != ELiveValueType::object) ||
                (configuration.child_count(bindings) != 1u) ||
                !equal_path(text(configuration, bin, "source"), "executable-directory") ||
                (configuration.value_type(extensions) != ELiveValueType::array) ||
                (configuration.child_count(extensions) != 1u) ||
                !equal_path(configuration.string_value(configuration.first_child(extensions)), "dll") ||
                flag(configuration, bin, "writable"))
            {
                return EScanStatus::invalid_manifest;
            }
            if (!platform::filesystem::executable_directory(request.redirect_directory)) { return EScanStatus::scan_failed; }
        }
        const EScanStatus status = populate(candidate, roots, request);
        if (status != EScanStatus::success) { return status; }
    }
    result = std::move(candidate);
    return EScanStatus::success;
}

EScanStatus scan_root(const SRootScan& request, CLiveDocument& result) noexcept
{
    CLiveDocument candidate;
    if (!candidate.initialise() || !candidate.set_root_type(ELiveValueType::object))
    {
        return EScanStatus::allocation_failed;
    }
    const CNodeKey roots = candidate.create_object(CStringView{ "roots" });
    if (!add(candidate, candidate.root(), roots)) { return EScanStatus::allocation_failed; }
    const EScanStatus status = populate(candidate, roots, request);
    if (status != EScanStatus::success) { return status; }
    result = std::move(candidate);
    return EScanStatus::success;
}

bool CImage::prepare_scan(const char* const logical_root, SRootScan& request) const noexcept
{
    const CNodeKey root = find_root(m_document, logical_root);
    if (!root.is_valid()) { return false; }
    request.writable = flag(m_document, root, "writable");
    request.inventory = flag(m_document, root, "inventory", true);
    const CNodeKey bin = child(m_document, child(m_document, root, "content"), "bin");
    const char* redirect = child(m_document, bin, "extensions").is_valid() ? text(m_document, bin, "source").cstring() : "";
    return request.logical_root.set(logical_root) && request.physical_path.set(text(m_document, root, "source").cstring()) &&
        request.redirect_directory.set(redirect);
}

bool CImage::resolve(const char* const logical_file, const bool writing, CSimpleString& physical) const noexcept
{
    return resolve_file(m_document, logical_file, writing, physical);
}

const CImage::SFileState* CImage::state(const char* const file) const noexcept
{
    for (std::int32_t slot = m_files.first_live(); slot >= 0; slot = m_files.next_live(slot))
    {
        const SFileState* value = m_files.get_object(slot);
        if (equal_path(value->file.view(), file)) { return value; }
    }
    return nullptr;
}

CImage::SFileState* CImage::ensure_state(const char* const file) noexcept
{
    for (std::int32_t slot = m_files.first_live(); slot >= 0; slot = m_files.next_live(slot))
    {
        SFileState* value = m_files.get_object(slot);
        if (equal_path(value->file.view(), file)) { return value; }
    }
    if (!m_files.is_ready() && !m_files.initialise()) { return nullptr; }
    const std::int32_t slot = m_files.emplace();
    SFileState* value = m_files.get_object(slot);
    if ((value == nullptr) || !value->file.set(file))
    {
        if (value != nullptr) { (void)m_files.erase(slot); }
        return nullptr;
    }
    return value;
}

std::uint64_t CImage::cached_asset(const char* const logical_file, const std::uint64_t format) const noexcept
{
    const SFileState* value = state(logical_file);
    const CNodeKey entry = file_entry(m_document, logical_file);
    return value && (value->format == format) && is_file(m_document, entry) &&
        (number(m_document, entry, "asset") == value->asset) ? value->asset : 0u;
}

std::uint32_t CImage::cached_findings(const char* const logical_file) const noexcept
{
    const SFileState* value = state(logical_file);
    return value ? value->findings : UINT32_MAX;
}

bool CImage::loaded(const char* const logical_file, const std::uint64_t asset, const std::uint64_t format,
    const std::uint32_t findings, const std::uint64_t admission_serial) noexcept
{
    const CNodeKey entry = file_entry(m_document, logical_file);
    const SFileState* previous = state(logical_file);

    //  Removed/retyped files are no longer cache destinations. The completed
    //  load remains retained in the asset repository until normal disposal.
    if (!is_file(m_document, entry) || (previous && (previous->write_serial > admission_serial))) { return true; }
    SFileState* value = ensure_state(logical_file);
    if ((value == nullptr) || !asset_id(m_document, entry, asset)) { return false; }
    value->asset = asset;
    value->format = format;
    value->findings = findings;
    return true;
}

bool CImage::written(const char* const logical_file, const char* const physical, const std::uint64_t asset, const std::uint64_t format) noexcept
{
    const char* relative = nullptr;
    const CNodeKey root = file_root(m_document, logical_file, relative);
    if (!root.is_valid()) { return false; }
    if (!flag(m_document, root, "inventory", true)) { return true; }
    const CNodeKey entry = ensure_file(m_document, logical_file);
    SFileState* value = ensure_state(logical_file);
    if (!entry.is_valid() || (value == nullptr)) { return false; }

    //  Usually the inherited source is sufficient. An in-flight write can
    //  exceptionally outlive a binding change, so preserve its actual target.
    CSimpleString expected, actual;
    if (!normalized(physical, actual)) { return false; }
    if (!resolve_file(m_document, logical_file, false, expected) || !(expected.view() == actual.view()))
    {
        if (!replace(m_document, entry, "source", m_document.create_string(actual.view()))) { return false; }
    }
    if (!asset_id(m_document, entry, asset)) { return false; }
    value->asset = asset;
    value->format = format;
    value->findings = UINT32_MAX;
    value->write_serial = ++m_write_serial;
    return true;
}

bool CImage::forget_asset(const std::uint64_t asset) noexcept
{
    for (std::int32_t slot = m_files.first_live(); slot >= 0; slot = m_files.next_live(slot))
    {
        SFileState* value = m_files.get_object(slot);
        if ((asset == 0u) || (value->asset == asset))
        {
            const CNodeKey entry = file_entry(m_document, value->file.cstring());
            if (entry.is_valid() && !asset_id(m_document, entry, 0u)) { return false; }
            value->asset = 0u;
            value->findings = UINT32_MAX;
        }
    }
    return true;
}

bool CImage::integrate(const CLiveDocument& observation, const std::uint64_t scan_serial) noexcept
{
    const CNodeKey incoming_roots = child(observation, observation.root(), "roots");
    const CNodeKey incoming = observation.first_child(incoming_roots);
    const CStringView logical = observation.name(incoming);
    const CNodeKey old_root = find_root(m_document, logical.cstring());
    if ((observation.child_count(incoming_roots) != 1u) || !old_root.is_valid()) { return false; }

    //  Transactional: build privately, then publish. Side-state is not changed
    //  until the replacement document is complete.
    CBakedDocumentBlock snapshot;
    CLiveDocument candidate;
    if (!document_translation::bake(m_document, snapshot) || !document_translation::promote(snapshot.document(), candidate))
    {
        return false;
    }
    const CNodeKey root = find_root(candidate, logical.cstring());
    const CNodeKey replacement = copy_node(candidate, observation, incoming);
    if (!attach(candidate, root, replacement))
    {
        return false;
    }
    for (std::int32_t slot = m_files.first_live(); slot >= 0; slot = m_files.next_live(slot))
    {
        const SFileState* value = m_files.get_object(slot);
        const char* relative = nullptr;
        if (file_root(m_document, value->file.cstring(), relative) != old_root) { continue; }
        const CNodeKey old = file_entry(m_document, value->file.cstring());
        CNodeKey entry = file_entry(candidate, value->file.cstring());
        if ((value->write_serial > scan_serial) && is_file(m_document, old))
        {
            entry = ensure_file(candidate, value->file.cstring(), &m_document);
            const CNodeKey copy = copy_node(candidate, m_document, old);
            if (!entry.is_valid() || !attach(candidate, entry, copy)) { return false; }
        }
        else if (value->asset && is_file(m_document, old) && is_file(candidate, entry))
        {
            CSimpleString old_path;
            CSimpleString new_path;
            if (!resolve_file(m_document, value->file.cstring(), false, old_path) ||
                !resolve_file(candidate, value->file.cstring(), false, new_path)) { return false; }
            if ((old_path.view() == new_path.view()) && !asset_id(candidate, entry, value->asset)) { return false; }
        }
    }
    m_document = std::move(candidate);
    for (std::int32_t slot = m_files.first_live(); slot >= 0; slot = m_files.next_live(slot))
    {
        SFileState* value = m_files.get_object(slot);
        if (number(m_document, file_entry(m_document, value->file.cstring()), "asset") != value->asset)
        {
            value->asset = 0u;
            value->findings = UINT32_MAX;
        }
    }
    return true;
}

}   //  namespace filesystem_image
