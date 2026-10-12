
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    filesystem_image.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Sep 26
//
//  Logical-root discovery, path resolution and retained asset associations.

#pragma once

#ifndef FILESYSTEM_IMAGE_HPP_INCLUDED
#define FILESYSTEM_IMAGE_HPP_INCLUDED

#include "data_model/live_document.hpp"
#include "containers/TUnorderedCollection.hpp"
#include "assets/bundle_builder.hpp"

namespace filesystem_image
{

enum class EScanStatus : std::uint8_t { success = 0, invalid_manifest, scan_failed, allocation_failed, name_collision };

//  Immutable Host-owned backing for a worker borrow; never borrows image nodes.
struct SRootScan
{
    CSimpleString logical_root;
    CSimpleString physical_path;
    CSimpleString redirect_directory;
    bool writable{ false };
    bool inventory{ true };
};

[[nodiscard]] EScanStatus scan_manifest(const char* const manifest, CLiveDocument& result) noexcept;
[[nodiscard]] EScanStatus scan_root(const SRootScan& request, CLiveDocument& result) noexcept;

enum class ERootState : std::uint8_t { missing = 0, directory, reserved, prepared, open, closed };

//  Numeric bundle ranges do not borrow storage. The Host must protect the owner
//  before using them. binding identifies this image's particular reservation.
struct SResolvedSource
{
    CSimpleString physical_path;
    std::uint64_t binding{ 0u };
    std::uint64_t bundle{ 0u };
    std::size_t offset{ 0u };
    std::size_t size{ 0u };
};

//  Private, movable capture for synchronous builder input. Moving preserves
//  source path addresses; replacing/destroying it requires completed borrowers.
class CBakeInput
{
public:
    [[nodiscard]] const CLiveDocument& inventory() const noexcept { return m_inventory; }
    [[nodiscard]] CNodeKey content() const noexcept { return m_content; }
    [[nodiscard]] TPodConstView<bundle::SBuildSource> sources() const noexcept { return m_sources.const_view(); }

private:
    friend class CImage;
    CLiveDocument m_inventory;
    CNodeKey m_content;
    TUnorderedCollection<CSimpleString> m_paths;
    TPodVector<bundle::SBuildSource> m_sources;
};

//  Single-threaded Host facade over the authoritative document. Accesses copy
//  resolved filenames into stable Host-owned operations before worker dispatch.
class CImage
{
public:
    [[nodiscard]] bool adopt(CLiveDocument&& document) noexcept;
    [[nodiscard]] const CLiveDocument& document() const noexcept { return m_document; }
    [[nodiscard]] bool prepare_scan(const char* const logical_root, SRootScan& request) const noexcept;
    [[nodiscard]] bool integrate(const CLiveDocument& observation, const std::uint64_t scan_serial) noexcept;
    [[nodiscard]] std::uint64_t write_serial() const noexcept { return m_write_serial; }
    [[nodiscard]] bool resolve(const char* const logical_file, const bool writing, CSimpleString& physical) const noexcept;
    [[nodiscard]] bool resolve(const char* const logical_file, const bool writing, SResolvedSource& source) const noexcept;
    [[nodiscard]] ERootState root_state(const char* const logical_root) const noexcept;

    //  Reserve first; prepare copies only the validated directory. Publication
    //  is transactional and does not overwrite intervening image changes.
    //  The Host owns bundle storage and coordinates asset/binding publication.
    [[nodiscard]] bool reserve_bundle(const char* const logical_root, std::uint64_t& binding) noexcept;
    [[nodiscard]] bool prepare_bundle(const std::uint64_t binding, const std::uint64_t bundle, const bundle::CBundleView& view) noexcept;
    [[nodiscard]] bool publish_bundle(const std::uint64_t binding) noexcept;
    [[nodiscard]] bool close_bundle(const std::uint64_t binding) noexcept;

    //  Cancel reserved/prepared bindings, or remove closed bindings after the
    //  Host has drained all borrowers. Open bindings must first be closed.
    [[nodiscard]] bool remove_bundle(const std::uint64_t binding) noexcept;

    //  Empty subdirectory selects the whole root; otherwise a strict relative
    //  directory path. Directory-backed roots only. Failure preserves output.
    [[nodiscard]] bool capture_bake(const char* const logical_root, const char* const subdirectory, CBakeInput& result) const noexcept;

    [[nodiscard]] std::uint64_t cached_asset(const char* const logical_file, const std::uint64_t format) const noexcept;
    [[nodiscard]] std::uint32_t cached_findings(const char* const logical_file) const noexcept;
    [[nodiscard]] bool loaded(const char* const logical_file, const std::uint64_t asset, const std::uint64_t format,
        const std::uint32_t findings, const std::uint64_t admission_serial, const std::uint64_t binding = 0u) noexcept;
    [[nodiscard]] bool written(const char* const logical_file, const char* const physical,
        const std::uint64_t asset, const std::uint64_t format) noexcept;
    [[nodiscard]] bool forget_asset(const std::uint64_t asset) noexcept;
    [[nodiscard]] bool clear_cache() noexcept { return forget_asset(0u); }

private:
    //  Only accessed/cached files need runtime bookkeeping; this is not part
    //  of the discoverable document or borrowed by the I/O worker.
    struct SFileState
    {
        CSimpleString file;
        std::uint64_t asset{ 0u };
        std::uint64_t format{ 0u };
        std::uint64_t write_serial{ 0u };
        std::uint32_t findings{ UINT32_MAX };
        std::uint64_t binding{ 0u };
    };
    struct SBinding
    {
        CSimpleString root;
        CLiveDocument prepared;
        std::uint64_t identity{ 0u };
        std::uint64_t bundle{ 0u };
        ERootState state{ ERootState::reserved };
    };
    [[nodiscard]] const SBinding* binding_for_root(const CStringView root) const noexcept;
    [[nodiscard]] const SBinding* binding_for_file(const char* const file) const noexcept;
    [[nodiscard]] SBinding* binding_record(const std::uint64_t identity) noexcept;
    [[nodiscard]] const SFileState* state(const char* const file) const noexcept;
    [[nodiscard]] SFileState* ensure_state(const char* const file) noexcept;

    CLiveDocument m_document;
    TUnorderedCollection<SFileState> m_files;
    TUnorderedCollection<SBinding> m_bindings;
    std::uint64_t m_next_binding{ 0u };
    std::uint64_t m_write_serial{ 0u };
};

}   //  namespace filesystem_image

#endif  //  FILESYSTEM_IMAGE_HPP_INCLUDED
