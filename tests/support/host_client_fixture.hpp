
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
#include "tests/support/thread_helpers.hpp"

namespace tests
{

class CHostClientFixture : public threading::CThreadPackage
{
public:
    CHostClientFixture(const threading::ThreadConfig& config,
        const platform::system::CPerfCountConversion& conversion) noexcept
        : threading::CThreadPackage{ config, conversion }
        , client{ config.worker_module_id, ++s_next_identity, this, { s_next_identity + UINT64_C(0x100000000) } }
        , m_conversion{ conversion }
    {
        const bool initialised = client.initialise();
        MV_ASSERT(initialised);
        (void)initialised;
    }

    // Standalone fixtures own their echo worker; they have no Host progress loop.
    [[nodiscard]] bool shutdown() noexcept
    {
        request_exit();
        if (retains_execution() && !tests::wait_until(m_conversion, [&] { return execution_ended(); }))
        {
            return false;
        }
        return threading::CThreadPackage::shutdown();
    }

    operator host::SClientRecord&() noexcept { return client; }
    operator const host::SClientRecord&() const noexcept { return client; }

    host::SClientRecord client;

private:
    const platform::system::CPerfCountConversion& m_conversion;
    static inline std::uint64_t s_next_identity{ 0u };
};

}   //  namespace tests
