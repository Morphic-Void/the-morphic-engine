
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_candidate.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Private translated observations for one capture attempt. Range targets are assigned by schema baking.

#pragma once

#include "framework/platform/report.hpp"
#include "platform/threading/awake_clock.hpp"
#include "schema/schema.hpp"
#include "containers/StringBuffers.hpp"
#include "containers/TPodVector.hpp"

namespace platform::runtime
{

struct SReportBakeTiming
{
    platform::threading::SAwakeClock clock;
    std::uint64_t prepared{}, authored{}, baked{};
    bool clock_failed{};
    bool stamp(std::uint64_t& value) noexcept;
};

struct SReportCandidate
{
    platform_report_v1::PlatformReportHeader header{};
    TPodVector<platform_report_v1::PlatformAdapter> adapters;
    TPodVector<platform_report_v1::PlatformDisplaySource> sources;
    TPodVector<platform_report_v1::PlatformDisplay> displays;
    TPodVector<platform_report_v1::PlatformDisplayPath> paths;
    TPodVector<platform_report_v1::PlatformModeCatalogue> catalogues;
    TPodVector<platform_report_v1::PlatformDisplayMode> modes;
    TPodVector<platform_report_v1::PlatformNativeIdentity> identities;
    TPodVector<platform_report_v1::PlatformByte> bytes;
    TPodVector<platform_report_v1::PlatformQueryResult> queries;
    CStableStrings text;

    [[nodiscard]] bool bake(CBakedDocumentBlock& result, const schema::SDataOperationLimits limits,
        schema::SSchemaDiagnostic& diagnostic, SReportBakeTiming* const timing = nullptr) const noexcept;
};

}   //  namespace platform::runtime
