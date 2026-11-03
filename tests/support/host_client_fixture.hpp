
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_client_fixture.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  An actual test thread and its separate Host client identity.

#pragma once

#include "host/runtime/client_record.hpp"

namespace tests
{

class CHostClientFixture : public threading::CThreadPackage
{
public:
    CHostClientFixture(const threading::ThreadConfig& config,
        const platform::system::CPerfCountConversion& conversion) noexcept
        : threading::CThreadPackage{ config, conversion }
        , client{ config.worker_module_id, ++s_next_identity, this }
    {}

    operator host::SClientRecord&() noexcept { return client; }
    operator const host::SClientRecord&() const noexcept { return client; }

    host::SClientRecord client;

private:
    static inline std::uint64_t s_next_identity{ 0u };
};

}   //  namespace tests
