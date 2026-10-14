
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_instances_block.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    14 Oct 26
//
//  Completed instances file with a contained specialised role.

#pragma once

#ifndef SCHEMA_BAKED_INSTANCES_BLOCK_HPP_INCLUDED
#define SCHEMA_BAKED_INSTANCES_BLOCK_HPP_INCLUDED

#include "schema/data_block.hpp"
#include "assets/asset_repository.hpp"

namespace schema
{

class CBakedInstancesBlock
{
public:
    CBakedInstancesBlock() noexcept = default;
    CBakedInstancesBlock(const CBakedInstancesBlock&) = delete;
    CBakedInstancesBlock& operator=(const CBakedInstancesBlock&) = delete;
    CBakedInstancesBlock(CBakedInstancesBlock&&) = delete;
    CBakedInstancesBlock& operator=(CBakedInstancesBlock&&) = delete;
    ~CBakedInstancesBlock() noexcept { clear(); }

    //  Validate before taking ownership. Failure preserves source and destination.
    [[nodiscard]] bool adopt_local(CBakedDocumentBlock&& block, CBakedSchema& schema,
        const SDataBlockLoadOptions& options, SInstancesBlockDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool adopt_local(CBakedDocumentBlock&& block, CLiveSchema& schema,
        const SDataBlockLoadOptions& options, SInstancesBlockDiagnostic& diagnostic) noexcept;

    //  Borrow a retained asset's complete range. The caller protects its lifetime;
    //  this block neither acquires a reference nor sends disposal requests.
    [[nodiscard]] bool attach_asset(CAssetId asset, CByteConstView bytes, CBakedSchema& schema,
        const SDataBlockLoadOptions& options, SInstancesBlockDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool attach_asset(CAssetId asset, CByteConstView bytes, CLiveSchema& schema,
        const SDataBlockLoadOptions& options, SInstancesBlockDiagnostic& diagnostic) noexcept;

    //  Clear the role before releasing/forgetting backing. All users must be quiescent.
    void clear() noexcept;
    [[nodiscard]] bool release_local(CBakedDocumentBlock& output) noexcept;

    [[nodiscard]] const CBakedInstances& instances() const noexcept { return m_role; }
    [[nodiscard]] EEntityBacking backing() const noexcept { return m_backing; }
    [[nodiscard]] bool loaded_ready() const noexcept { return m_role.loaded_ready(); }
    [[nodiscard]] CAssetId asset_id() const noexcept { return m_asset; }

    //  Remains available after schema invalidation; excludes spare capacity.
    [[nodiscard]] CByteConstView file_bytes() const noexcept { return m_bytes; }

private:
    template<class TSchema>
    [[nodiscard]] bool adopt(CBakedDocumentBlock&& block, TSchema& schema,
        const SDataBlockLoadOptions& options, SInstancesBlockDiagnostic& diagnostic) noexcept;
    template<class TSchema>
    [[nodiscard]] bool attach(CAssetId asset, CByteConstView bytes, TSchema& schema,
        const SDataBlockLoadOptions& options, SInstancesBlockDiagnostic& diagnostic) noexcept;

    CBakedDocumentBlock m_local;
    CByteConstView m_bytes;
    CAssetId m_asset;
    EEntityBacking m_backing{ EEntityBacking::none };
    CBakedInstances m_role;
};

}   // namespace schema

#endif // SCHEMA_BAKED_INSTANCES_BLOCK_HPP_INCLUDED
