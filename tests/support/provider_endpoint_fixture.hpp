
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    provider_endpoint_fixture.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    20 Nov 26
//
//  Drives real provider POD exchanges while tests inject domain and native outcomes.

#pragma once

#include "tests/support/endpoint_fixture.hpp"
#include "tests/support/test_context.hpp"

namespace tests
{

class CProviderEndpointFixture
{
public:
    template<typename Fixture>
    bool progress(Fixture& fixture) noexcept
    {
        bool work = false;
        host::SClientRecord& provider = fixture.provider;
        for (std::uint32_t count = 0u; count < 64u; ++count)
        {
            if (!m_pending && !provider.read(m_message))
            {
                break;
            }
            m_pending = true;
            host::CRetainedReplies::CDispatch dispatch{ *provider.pending_replies, 1u };
            if (!dispatch) break;
            services::ProviderOperationResult result;
            services::CompleteServiceRequest complete;
            services::CommitServiceResource commit;
            services::PrepareServiceBorrow prepare;
            services::BeginServiceBorrowRetirement begin;
            services::FinishServiceBorrowRetirement finish;
            services::BindServiceNotification bind;
            bool operation = true;
            if (m_message.copy_payload_to(complete)) result.status = Fixture::complete(&fixture, &complete.reply);
            else if (m_message.copy_payload_to(commit)) result.status = Fixture::commit(&fixture, &commit.reply);
            else if (m_message.copy_payload_to(prepare)) result.status = Fixture::prepare(&fixture, &prepare.offer, &result.borrow);
            else if (m_message.copy_payload_to(begin)) result.status = Fixture::begin(&fixture, begin.borrow);
            else if (m_message.copy_payload_to(finish)) result.status = Fixture::finish(&fixture, finish.borrow);
            else if (m_message.copy_payload_to(bind)) result.status = Fixture::bind(&fixture, bind.borrow, bind.registration, &result.notification);
            else
            {
                operation = false;
                if (Fixture::post(&fixture, &m_message) == services::EServiceStatus::busy) break;
            }
            //  Busy is an asynchronous result too. The provider decides whether
            //  to retry its retained operation on a later module invocation.
            if (operation)
            {
                TEST_EXPECT(fixture.tests, (provider.reserve_replies(1u) &&
                    provider.reply(threading::make_pod_message(m_message.query_async_slot(), result))));
            }
            provider.accounted_pod();
            m_pending = false;
            work = true;
        }
        return work;
    }
private:
    threading::CErasedPodMsg m_message;
    bool m_pending{ false };
};

}   //  namespace tests
