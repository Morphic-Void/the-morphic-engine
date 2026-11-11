
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema_messages.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Nominated Host schema conditioning, with an acquisition-scoped result.

#pragma once

#include "framework/system/system_ids.hpp"
#include "framework/assets/asset_messages.hpp"
#include "foundation/schema/prepared_schema.hpp"

struct SchemaPrepareRequest
{
    CAssetId source;
    schema::SPreparationLimits limits;
    CDocumentParseOptions policy{ document_policy::k_utf8 | document_findings::k_morphic };
};

//  A successful result has already acquired every descriptor reference. End all
//  reads before AssetReleaseRequest; release also retires this temporary result.
struct SchemaPrepareResult
{
    CAssetAcquisitionId acquisition;
    const schema::SPreparedSchemaV1* descriptor{ nullptr };
    schema::SPreparationDiagnostic diagnostic;
    EAssetStatus status{ EAssetStatus::invalid_request };
};

MV_REGISTER_SYSTEM_TYPE(SchemaPrepareRequest, system_type_ids::schema_prepare_request);
MV_REGISTER_SYSTEM_TYPE(SchemaPrepareResult, system_type_ids::schema_prepare_result);
