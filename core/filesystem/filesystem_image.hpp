
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

//  Single-threaded Host facade over the authoritative document. Accesses copy
//  resolved filenames into stable Host-owned operations before worker dispatch.
class CImage
{
public:
    void adopt(CLiveDocument&& document) noexcept { m_document = std::move(document); m_files.deallocate(); m_write_serial = 0u; }
    [[nodiscard]] const CLiveDocument& document() const noexcept { return m_document; }
    [[nodiscard]] bool prepare_scan(const char* const logical_root, SRootScan& request) const noexcept;
    [[nodiscard]] bool integrate(const CLiveDocument& observation, const std::uint64_t scan_serial) noexcept;
    [[nodiscard]] std::uint64_t write_serial() const noexcept { return m_write_serial; }
    [[nodiscard]] bool resolve(const char* const logical_file, const bool writing, CSimpleString& physical) const noexcept;
    [[nodiscard]] std::uint64_t cached_asset(const char* const logical_file, const std::uint64_t format) const noexcept;
    [[nodiscard]] std::uint32_t cached_findings(const char* const logical_file) const noexcept;
    [[nodiscard]] bool loaded(const char* const logical_file, const std::uint64_t asset, const std::uint64_t format,
        const std::uint32_t findings, const std::uint64_t admission_serial) noexcept;
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
    };
    [[nodiscard]] const SFileState* state(const char* const file) const noexcept;
    [[nodiscard]] SFileState* ensure_state(const char* const file) noexcept;

    CLiveDocument m_document;
    TUnorderedCollection<SFileState> m_files;
    std::uint64_t m_write_serial{ 0u };
};

}   //  namespace filesystem_image

#endif  //  FILESYSTEM_IMAGE_HPP_INCLUDED
