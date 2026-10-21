
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
#include "filesystem/logical_path.hpp"
#include "data_model/baked_document.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_copy.hpp"
#include "platform/filesystem/directory.hpp"
#include "platform/filesystem/file.hpp"
#include "platform/module/binding.hpp"

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
    return logical_path::valid_relative_file(CStringView{ path });
}

static bool equal_path(const CStringView a, const char* const b) noexcept
{
    return (b != nullptr) && (a == CStringView{ b });
}

static bool case_alias(const CStringView a, const char* const b) noexcept
{
    return logical_path::case_equal(a, CStringView{ b });
}

static bool folded(const CStringView name, CSimpleString& result) noexcept
{
    if (name.string() == nullptr) { return false; }
    TPodVector<char> bytes;
    if (!bytes.resize(name.length() + 1u)) { return false; }
    for (std::size_t i = 0u; i < name.length(); ++i)
    {
        const char value = name.cstring()[i];
        bytes[i] = logical_path::fold_ascii(value);
    }
    bytes[name.length()] = '\0';
    return result.set(bytes.data(), name.length());
}

static bool folded_name(const CStringView name) noexcept
{
    return logical_path::is_folded(name);
}

static CNodeKey file_child(const CLiveDocument& doc, const CNodeKey parent, const CStringView name) noexcept
{
    if (folded_name(name)) { return doc.object_child(parent, name); }
    CSimpleString key;
    return folded(name, key) ? doc.object_child(parent, key.view()) : CNodeKey{};
}

static CStringView physical_name(const CLiveDocument& doc, const CNodeKey entry) noexcept
{
    const CNodeKey spelling = child(doc, entry, "physicalName");
    return spelling.is_valid() ? doc.string_value(spelling) : doc.name(entry);
}

static CNodeKey create_entry(CLiveDocument& doc, const CStringView name) noexcept
{
    CSimpleString key;
    if (!folded(name, key)) { return {}; }
    const CNodeKey entry = doc.create_object(key.view());
    if (!entry.is_valid() || (!(key.view() == name) && !string(doc, entry, "physicalName", name.cstring()))) { return {}; }
    return entry;
}

static bool root_name(const CStringView name) noexcept
{
    if (!folded_name(name) || (name.length() < 2u) || (name.cstring()[name.length() - 1u] != ':') ||
        logical_path::is_native_mount(name)) { return false; }
    CSimpleString identifier;
    return identifier.set(name.cstring(), (name.length() - 1u)) && relative_file(identifier.cstring()) &&
        (std::strchr(identifier.cstring(), '/') == nullptr);
}

//  Schema admission checks names, not the document's internal integrity.
static bool valid_content(const CLiveDocument& doc, const CNodeKey content, const std::uint32_t depth) noexcept
{
    if ((depth > 128u) || (doc.value_type(content) != ELiveValueType::object)) { return false; }
    for (CNodeKey entry = doc.first_child(content); entry.is_valid(); entry = doc.next_sibling(entry))
    {
        const CStringView key = doc.name(entry);
        const CStringView spelling = physical_name(doc, entry);
        if ((doc.value_type(entry) != ELiveValueType::object) || !folded_name(key) ||
            !relative_file(key.cstring()) || (std::strchr(key.cstring(), '/') != nullptr) ||
            !relative_file(spelling.cstring()) || (std::strchr(spelling.cstring(), '/') != nullptr) ||
            !case_alias(key, spelling.cstring())) { return false; }
        const CNodeKey nested = child(doc, entry, "content");
        if (nested.is_valid() && !valid_content(doc, nested, depth + 1u)) { return false; }
    }
    return true;
}

static bool valid_image(const CLiveDocument& doc) noexcept
{
    const CNodeKey roots = child(doc, doc.root(), "roots");
    if (!doc.is_ready() || (doc.value_type(roots) != ELiveValueType::object)) { return false; }
    for (CNodeKey root = doc.first_child(roots); root.is_valid(); root = doc.next_sibling(root))
    {
        if (!root_name(doc.name(root)) || (doc.value_type(root) != ELiveValueType::object)) { return false; }
        const CNodeKey content = child(doc, root, "content");
        if (content.is_valid() && !valid_content(doc, content, 0u)) { return false; }
    }
    return true;
}

static CNodeKey find_root(const CLiveDocument& doc, const char* const logical) noexcept
{
    return file_child(doc, child(doc, doc.root(), "roots"), CStringView{ logical });
}

static CNodeKey file_root(const CLiveDocument& doc, const char* const file, const char*& relative) noexcept
{
    relative = nullptr;
    if (file == nullptr) { return {}; }
    const char* colon = std::strchr(file, ':');
    if ((colon == nullptr) || (colon[1] != '/') || !relative_file(colon + 2)) { return {}; }
    const CNodeKey root = file_child(doc, child(doc, doc.root(), "roots"),
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
        node = file_child(doc, child(doc, node, "content"),
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
        CSimpleString key;
        if (!folded(component.view(), key)) { return false; }
        node = doc.object_child(content, key.view());
        if (!node.is_valid())
        {
            return writing && writable && (slash == nullptr) && join(path.cstring(), component.cstring(), physical);
        }
        if (!join(path.cstring(), physical_name(doc, node).cstring(), path)) { return false; }
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
    bool modules_only;
    std::uint32_t depth;
    EScanStatus status{ EScanStatus::success };
};

static bool crawl(void* const opaque, const char* const name, const platform::filesystem::EDirectoryEntry kind) noexcept
{
    SCrawl& work = *static_cast<SCrawl*>(opaque);
    if (std::strcmp(name, ".gitkeep") == 0) { return true; }
    if (work.modules_only)
    {
        const char* extension = std::strrchr(name, '.');
        if ((kind != platform::filesystem::EDirectoryEntry::file) ||
            (extension == nullptr) || !case_alias(CStringView{ extension + 1 }, platform::module::get_modules_std_ext())) { return true; }
    }
    if (!relative_file(name) || (std::strchr(name, '/') != nullptr)) { return false; }
    CLiveDocument& doc = work.document;
    const CNodeKey node = create_entry(doc, CStringView{ name });
    if (!node.is_valid()) { return false; }
    const CLiveAttachmentResult attached = doc.append_child(work.content, node);
    if (!attached.succeeded())
    {
        if (attached.rejection == ELiveAttachmentRejection::duplicate_object_name) { work.status = EScanStatus::name_collision; }
        return false;
    }
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
    if (!root_name(request.logical_root.view())) { return EScanStatus::invalid_manifest; }
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
        if (!join(physical.cstring(), physical_name(doc, bin).cstring(), physical_bin)) { return EScanStatus::allocation_failed; }
        for (CNodeKey entry = doc.first_child(child(doc, bin, "content")); entry.is_valid(); entry = doc.next_sibling(entry))
        {
            CSimpleString source;
            if (!join(physical_bin.cstring(), physical_name(doc, entry).cstring(), source) ||
                !string(doc, entry, "source", source.cstring())) { return EScanStatus::allocation_failed; }
        }
    }
    else
    {
        bin = doc.create_object(CStringView{ "bin" });
        if (!add(doc, content, bin) || !add(doc, bin, doc.create_object(CStringView{ "content" })))
        {
            return EScanStatus::allocation_failed;
        }
    }
    const CNodeKey extensions = doc.create_array(CStringView{ "extensions" });
    if (!string(doc, bin, "source", redirected.cstring()) || !set_flag(doc, bin, "writable", false) ||
        !add(doc, bin, extensions) || !add(doc, extensions, doc.create_string(CStringView{ platform::module::get_modules_std_ext() })))
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
        old = previous ? file_child(*previous, child(*previous, old, "content"), name.view()) : CNodeKey{};
        CNodeKey entry = file_child(doc, content, name.view());
        if (!entry.is_valid())
        {
            entry = ((previous != nullptr) && old.is_valid()) ?
                copy_node(doc, (*previous), old, true) : create_entry(doc, name.view());
            if (!add(doc, content, entry)) { return {}; }
        }
        if (slash == nullptr)
        {
            if (!is_file(doc, entry))
            {
                CSimpleString spelling;
                if (!spelling.set(physical_name(doc, entry).cstring()) ||
                    !attach(doc, entry, create_entry(doc, spelling.view()))) { return {}; }
            }
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
        const CStringView source = text(configuration, root, "source");
        SRootScan request;
        if (!root_name(logical) ||
            !relative_file(source.cstring()) ||
            !configuration.boolean_value(child(configuration, root, "writable"), request.writable) ||
            (child(configuration, root, "inventory").is_valid() &&
                !configuration.boolean_value(child(configuration, root, "inventory"), request.inventory)) ||
            candidate.object_child(roots, logical).is_valid())
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
            const CStringView configured_extension = configuration.string_value(configuration.first_child(extensions));
            char extension[32]{};
            if (!equal_path(logical, "package:") || !request.inventory ||
                (configuration.value_type(bindings) != ELiveValueType::object) ||
                (configuration.child_count(bindings) != 1u) ||
                !equal_path(text(configuration, bin, "source"), "executable-directory") ||
                (configuration.value_type(extensions) != ELiveValueType::array) ||
                (configuration.child_count(extensions) != 1u) ||
                configured_extension.empty() ||
                (std::strlen(configured_extension.cstring()) != configured_extension.length()) ||
                !platform::module::expand_module_extension(configured_extension.cstring(), extension, sizeof(extension)) ||
                !equal_path(CStringView{ extension }, platform::module::get_modules_std_ext()) ||
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
    if (binding_for_root(CStringView{ logical_root }) != nullptr) { return false; }
    const CNodeKey root = find_root(m_document, logical_root);
    if (!root.is_valid()) { return false; }
    request.writable = flag(m_document, root, "writable");
    request.inventory = flag(m_document, root, "inventory", true);
    const CNodeKey bin = child(m_document, child(m_document, root, "content"), "bin");
    const char* redirect = child(m_document, bin, "extensions").is_valid() ? text(m_document, bin, "source").cstring() : "";
    return
        request.logical_root.set(m_document.name(root).cstring()) &&
        request.physical_path.set(text(m_document, root, "source").cstring()) &&
        request.redirect_directory.set(redirect);
}

bool CImage::adopt(CLiveDocument&& document) noexcept
{
    if ((m_bindings.first_live() >= 0) || !valid_image(document)) { return false; }
    m_document = std::move(document);
    m_files.deallocate();
    m_write_serial = 0u;
    return true;
}

bool CImage::resolve(const char* const logical_file, const bool writing, CSimpleString& physical) const noexcept
{
    if (binding_for_file(logical_file) != nullptr) { return false; }
    return resolve_file(m_document, logical_file, writing, physical);
}

const CImage::SFileState* CImage::state(const char* const file) const noexcept
{
    for (std::int32_t slot = m_files.first_live(); slot >= 0; slot = m_files.next_live(slot))
    {
        const SFileState* value = m_files.get_object(slot);
        if (case_alias(value->file.view(), file)) { return value; }
    }
    return nullptr;
}

CImage::SFileState* CImage::ensure_state(const char* const file) noexcept
{
    for (std::int32_t slot = m_files.first_live(); slot >= 0; slot = m_files.next_live(slot))
    {
        SFileState* value = m_files.get_object(slot);
        if (case_alias(value->file.view(), file)) { return value; }
    }
    if (!m_files.is_ready() && !m_files.initialise()) { return nullptr; }
    const std::int32_t slot = m_files.emplace();
    SFileState* value = m_files.get_object(slot);
    if ((value == nullptr) || !folded(CStringView{ file }, value->file))
    {
        if (value != nullptr) { (void)m_files.erase(slot); }
        return nullptr;
    }
    return value;
}

std::uint64_t CImage::cached_asset(const char* const logical_file, const std::uint64_t format) const noexcept
{
    const SBinding* const binding = binding_for_file(logical_file);
    if (binding && (binding->state != ERootState::open)) { return 0u; }
    const SFileState* value = state(logical_file);
    const CNodeKey entry = file_entry(m_document, logical_file);
    return (value && (value->binding == (binding ? binding->identity : 0u)) &&
        (value->format == format) && is_file(m_document, entry) &&
        (number(m_document, entry, "asset") == value->asset)) ? value->asset : 0u;
}

std::uint32_t CImage::cached_findings(const char* const logical_file) const noexcept
{
    const SBinding* const binding = binding_for_file(logical_file);
    if (binding && (binding->state != ERootState::open)) { return UINT32_MAX; }
    const SFileState* value = state(logical_file);
    return value ? value->findings : UINT32_MAX;
}

bool CImage::loaded(const char* const logical_file, const std::uint64_t asset, const std::uint64_t format,
    const std::uint32_t findings, const std::uint64_t admission_serial, const std::uint64_t binding) noexcept
{
    const SBinding* const current = binding_for_file(logical_file);
    if ((binding != (current ? current->identity : 0u)) || (current && (current->state != ERootState::open))) { return true; }
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
    value->binding = binding;
    return true;
}

bool CImage::written(const char* const logical_file, const char* const physical, const std::uint64_t asset, const std::uint64_t format) noexcept
{
    if (binding_for_file(logical_file) != nullptr) { return false; }
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
            if (entry.is_valid() && (number(m_document, entry, "asset") == value->asset) &&
                !asset_id(m_document, entry, 0u)) { return false; }
            value->asset = 0u;
            value->findings = UINT32_MAX;
        }
    }
    return true;
}

bool CImage::integrate(const CLiveDocument& observation, const std::uint64_t scan_serial) noexcept
{
    if (!valid_image(observation)) { return false; }
    const CNodeKey incoming_roots = child(observation, observation.root(), "roots");
    const CNodeKey incoming = observation.first_child(incoming_roots);
    const CStringView logical = observation.name(incoming);
    if (binding_for_root(logical) != nullptr) { return false; }
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

//  Iterative copying keeps deeply nested validated bundles independent of the
//  directory scanner's depth bound. Failure erases the detached partial tree;
//  existing reachable content is untouched. Interned strings/capacity may remain.
template<class TDocument, class TKey>
static CNodeKey copy_tree(CLiveDocument& target, const TDocument& source, const TKey start) noexcept
{
    struct SFrame { TKey next; CNodeKey parent; };
    const CNodeKey root = document_translation::detail::create_value_copy(target, source, start, source.name(start));
    if (!root.is_valid()) { return {}; }
    TPodVector<SFrame> stack;
    if (!stack.push_back(SFrame{ source.first_child(start), root }))
    {
        (void)target.erase(root);
        return {};
    }
    while (stack.size() != 0u)
    {
        const SFrame frame = stack[stack.size() - 1u];
        if (!frame.next.is_valid()) { (void)stack.discard_back(); continue; }
        stack[stack.size() - 1u].next = source.next_sibling(frame.next);
        const CNodeKey value = document_translation::detail::create_value_copy(target, source, frame.next, source.name(frame.next));
        if (!add(target, frame.parent, value))
        {
            if (value.is_valid()) (void)target.erase(value);
            (void)target.erase(root);
            return {};
        }
        if (source.first_child(frame.next).is_valid() && !stack.push_back(SFrame{ source.first_child(frame.next), value }))
        {
            (void)target.erase(root);
            return {};
        }
    }
    return root;
}

const CImage::SBinding* CImage::binding_for_root(const CStringView root) const noexcept
{
    for (std::int32_t slot = m_bindings.first_live(); slot >= 0; slot = m_bindings.next_live(slot))
    {
        const SBinding* const value = m_bindings.get_object(slot);
        if (logical_path::case_equal(value->root.view(), root)) { return value; }
    }
    return nullptr;
}

const CImage::SBinding* CImage::binding_for_file(const char* const file) const noexcept
{
    if (file == nullptr) { return nullptr; }
    const char* const colon = std::strchr(file, ':');
    return colon ? binding_for_root(CStringView{ file, static_cast<std::size_t>(colon - file) + 1u }) : nullptr;
}

CImage::SBinding* CImage::binding_record(const std::uint64_t identity) noexcept
{
    for (std::int32_t slot = m_bindings.first_live(); slot >= 0; slot = m_bindings.next_live(slot))
    {
        SBinding* const value = m_bindings.get_object(slot);
        if (value->identity == identity) { return value; }
    }
    return nullptr;
}

ERootState CImage::root_state(const char* const logical_root) const noexcept
{
    if (const SBinding* const value = binding_for_root(CStringView{ logical_root })) { return value->state; }
    return find_root(m_document, logical_root).is_valid() ? ERootState::directory : ERootState::missing;
}

bool CImage::reserve_bundle(const char* const logical_root, std::uint64_t& binding) noexcept
{
    CSimpleString name;
    if (!m_document.is_ready() || !folded(CStringView{ logical_root }, name) || !root_name(name.view()) ||
        (root_state(name.cstring()) != ERootState::missing) || (m_next_binding == UINT64_MAX)) { return false; }
    if (!m_bindings.is_ready() && !m_bindings.initialise()) { return false; }
    const std::int32_t slot = m_bindings.emplace();
    SBinding* const value = m_bindings.get_object(slot);
    if (value == nullptr) { return false; }
    value->root = std::move(name);
    value->identity = ++m_next_binding;
    binding = value->identity;
    return true;
}

bool CBundleDirectory::prepare(const bundle::CBundleView& view) noexcept
{
    if (!view.is_ready()) { return false; }
    CLiveDocument candidate;
    const CBakedDocument doc = view.document();
    if (!candidate.initialise() || !set_flag(candidate, candidate.root(), "writable", false) ||
        !add(candidate, candidate.root(), copy_tree(candidate, doc, doc.object_child(doc.root(), CStringView{ "content" })))) { return false; }
    m_document = std::move(candidate);
    return true;
}

bool CImage::prepare_bundle(const std::uint64_t binding, const std::uint64_t bundle, CBundleDirectory& directory) noexcept
{
    SBinding* const value = binding_record(binding);
    if (!value || (value->state != ERootState::reserved) || (bundle == 0u) || !directory.is_ready())
    {
        return false;
    }
    for (std::int32_t slot = m_bindings.first_live(); slot >= 0; slot = m_bindings.next_live(slot))
    {
        if (m_bindings.get_object(slot)->bundle == bundle)
        {
            return false;
        }
    }
    value->prepared = std::move(directory.m_document);
    value->bundle = bundle;
    value->state = ERootState::prepared;
    return true;
}

bool CImage::publish_bundle(const std::uint64_t binding) noexcept
{
    SBinding* const value = binding_record(binding);
    if (!value || (value->state != ERootState::prepared)) { return false; }
    if (find_root(m_document, value->root.cstring()).is_valid()) { return false; }
    const CNodeKey root = copy_tree(m_document, value->prepared, value->prepared.root());
    if (!root.is_valid()) { return false; }
    if (!m_document.set_name(root, value->root.view()) || !add(m_document, child(m_document, m_document.root(), "roots"), root))
    {
        (void)m_document.erase(root);
        return false;
    }
    value->prepared = CLiveDocument{};
    value->state = ERootState::open;
    return true;
}

bool CImage::close_bundle(const std::uint64_t binding) noexcept
{
    SBinding* const value = binding_record(binding);
    if (!value || (value->state != ERootState::open)) { return false; }
    value->state = ERootState::closed;
    return true;
}

bool CImage::remove_bundle(const std::uint64_t binding) noexcept
{
    SBinding* const value = binding_record(binding);
    if (!value || (value->state == ERootState::open)) { return false; }
    if (value->state == ERootState::closed)
    {
        const CNodeKey root = find_root(m_document, value->root.cstring());
        if (!root.is_valid() || !m_document.erase(root)) { return false; }
    }
    for (std::int32_t slot = m_files.first_live(); slot >= 0; )
    {
        const std::int32_t next = m_files.next_live(slot);
        const SFileState* const file = m_files.get_object(slot);
        if (binding_for_file(file->file.cstring()) == value) { (void)m_files.erase(slot); }
        slot = next;
    }
    return m_bindings.erase(m_bindings.reverse_lookup_slot_index_scan(value));
}

bool CImage::resolve(const char* const logical_file, const bool writing, SResolvedSource& source) const noexcept
{
    const SBinding* const binding = binding_for_file(logical_file);
    SResolvedSource candidate;
    if (binding)
    {
        if (writing || (binding->state != ERootState::open)) { return false; }
        const CNodeKey entry = file_entry(m_document, logical_file);
        if (!is_file(m_document, entry)) { return false; }
        candidate.binding = binding->identity;
        candidate.bundle = binding->bundle;
        candidate.offset = static_cast<std::size_t>(number(m_document, entry, "offset"));
        candidate.size = static_cast<std::size_t>(number(m_document, entry, "size"));
    }
    else if (!resolve_file(m_document, logical_file, writing, candidate.physical_path)) { return false; }
    source = std::move(candidate);
    return true;
}

static CNodeKey next_inventory_entry(const CLiveDocument& doc, const CNodeKey content, CNodeKey entry) noexcept
{
    const CNodeKey first = doc.first_child(child(doc, entry, "content"));
    if (first.is_valid()) { return first; }
    while (entry.is_valid() && !doc.next_sibling(entry).is_valid())
    {
        const CNodeKey parent = doc.parent(entry);
        entry = (parent == content) ? CNodeKey{} : doc.parent(parent);
    }
    return entry.is_valid() ? doc.next_sibling(entry) : CNodeKey{};
}

bool CImage::capture_bake(const char* const logical_root, const char* const subdirectory, CBakeInput& result) const noexcept
{
    if ((root_state(logical_root) != ERootState::directory) || (subdirectory == nullptr) ||
        ((*subdirectory != '\0') && !relative_file(subdirectory))) { return false; }
    CSimpleString selected;
    if (!selected.set(logical_root) || ((*subdirectory != '\0') && !join(logical_root, subdirectory, selected))) { return false; }
    const CNodeKey directory = (*subdirectory == '\0') ? find_root(m_document, logical_root) : file_entry(m_document, selected.cstring());
    const CNodeKey content = child(m_document, directory, "content");
    if (m_document.value_type(content) != ELiveValueType::object) { return false; }
    CBakeInput candidate;
    if (!candidate.m_inventory.initialise()) { return false; }
    candidate.m_content = copy_tree(candidate.m_inventory, m_document, content);
    if (!add(candidate.m_inventory, candidate.m_inventory.root(), candidate.m_content)) { return false; }
    const CLiveDocument& doc = candidate.m_inventory;
    TPodVector<CNodeKey> ancestors;
    for (CNodeKey entry = doc.first_child(candidate.m_content); entry.is_valid(); entry = next_inventory_entry(doc, candidate.m_content, entry))
    {
        if (!is_file(doc, entry)) { continue; }
        ancestors.clear();
        for (CNodeKey node = entry; node.is_valid(); )
        {
            if (!ancestors.push_back(node)) { return false; }
            const CNodeKey parent = doc.parent(node);
            node = (parent == candidate.m_content) ? CNodeKey{} : doc.parent(parent);
        }
        CSimpleString logical;
        if (!logical.set(selected.cstring())) { return false; }
        for (std::size_t i = ancestors.size(); i != 0u; --i)
        {
            if (!join(logical.cstring(), doc.name(ancestors[i - 1u]).cstring(), logical)) { return false; }
        }
        if (!candidate.m_paths.is_ready() && !candidate.m_paths.initialise()) { return false; }
        CSimpleString* const path = candidate.m_paths.get_object(candidate.m_paths.emplace());
        if (!path || !resolve(logical.cstring(), false, *path) ||
            !candidate.m_sources.push_back(bundle::SBuildSource{ entry, path->cstring() })) { return false; }
    }
    result = std::move(candidate);
    return true;
}

}   //  namespace filesystem_image
