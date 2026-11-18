
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_validation.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Validates the complete portable report against the independent version-1 contract.

#pragma once

#include "framework/platform/report_messages.hpp"
#include "schema/baked_data_block.hpp"
#include "schema/data_operation.hpp"

namespace platform::reports
{

[[nodiscard]] SFailure validate_report(const schema::CBakedDataBlock& block, const SCaptureIdentity capture,
    const schema::SDataOperationLimits limits) noexcept;

}   //  namespace platform::reports
