
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    texture_flow_tests.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    27 Oct 26
//
//  Rendering-owned asynchronous acceptance tests over the Host asset service.

#pragma once

#ifndef RENDERING_TEXTURE_FLOW_TESTS_HPP_INCLUDED
#define RENDERING_TEXTURE_FLOW_TESTS_HPP_INCLUDED

#include "assets/asset_messages.hpp"
#include "system/service_messages.hpp"
#include "threading/CThreadPackage.hpp"

namespace rendering
{
namespace texture_acceptance { struct SScenario; enum class EScenario : std::uint8_t; }

class CTextureFlowTests final
{
public:
    CTextureFlowTests(threading::CThreadContext& context, const module_ids::id_type module) noexcept :
        m_context{ context }, m_module{ module } {}

    [[nodiscard]] bool pending() const noexcept { return m_active; }
    [[nodiscard]] bool start(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool complete(const threading::CErasedPodMsg& message) noexcept;

private:
    enum class EPhase : std::uint8_t { fixture = 0, scenarios, disposal };
    [[nodiscard]] bool prepare_scenario(threading::CErasedOwnerMsg& pending,
        const texture_acceptance::SScenario& scenario, const std::int32_t slot) noexcept;
    [[nodiscard]] bool prepare_texture_fixture(threading::CErasedOwnerMsg& pending,
        const std::int32_t slot, const texture_acceptance::EScenario scenario) noexcept;
    [[nodiscard]] bool check(const texture_acceptance::SScenario& scenario, const AssetResult& result) noexcept;
    [[nodiscard]] bool next() noexcept;
    [[nodiscard]] bool dispose_next() noexcept;
    [[nodiscard]] bool finish() noexcept;

    threading::CThreadContext& m_context;
    const module_ids::id_type m_module;
    RunTestsResult m_result;
    AssetResult m_raw;
    AssetResult m_ldr_texture;
    AssetResult m_hdr_texture;
    CAssetId m_assets[24]{};
    std::uint32_t m_asset_count{ 0u };
    std::uint32_t m_scenario_index{ 0u };
    std::int32_t m_request_slot{ -1 };
    std::int32_t m_pending_slot{ 0 };
    EPhase m_phase{ EPhase::fixture };
    bool m_active{ false };
};

}   //  namespace rendering

#endif  //  RENDERING_TEXTURE_FLOW_TESTS_HPP_INCLUDED
