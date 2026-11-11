
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_provider.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Execution-independent provider availability and advertised contracts.

#pragma once

#include "runtime/client_record.hpp"
#include "framework/services/service_protocol.hpp"

namespace host
{

struct SServiceProvider
{
    [[nodiscard]] bool available() const noexcept;

    SClientRecord* client{ nullptr };
    //  Borrowed from the installed module; cleared before client/binding retirement.
    const services::SServiceContract* contracts{ nullptr };
    std::uint32_t contract_count{ 0u };
};

inline bool SServiceProvider::available() const noexcept
{
    return (client != nullptr) && (client->phase == EClientPhase::ready) && !client->stop_requested;
}

}   //  namespace host
