
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    CErasedPodMsgTransport.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    15 Aug 26

#include "foundation/system/erased_transport_admission.hpp"
#include "framework/threading/messages/CErasedMessageTransports.hpp"

namespace threading::transports
{

bool CErasedPodMsgTransport::post(const threading::CErasedPodMsg& msg) noexcept
{
    const erased_transport_admission::SDecision decision =
        erased_transport_admission::classify(
            msg.query_message_type_id(), m_destination_module_id);
    if (!decision.is_admitted())
    {
        erased_transport_admission::report_rejection(
            decision, erased_transport_admission::EIdentityRole::pod_message);
        return false;
    }
    return m_transport.post(msg);
}

}   //  namespace threading::transports
