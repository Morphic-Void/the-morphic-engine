
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    erased_owner_transfer.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Oct 26
//
//  Internal ownership transfer after concrete transport admission and capacity checks.

#pragma once

#ifndef ERASED_OWNER_TRANSFER_HPP_INCLUDED
#define ERASED_OWNER_TRANSFER_HPP_INCLUDED

#include <utility>      //  std::move

#include "debug/macros.hpp"
#include "system/erased_owner.hpp"
#include "threading/transports/TOwningTransport.hpp"

namespace erased_owner_transfer
{

//  owner is the value itself or its embedded owner. The caller has admitted all
//  identities and checked posting validity/capacity. Empty-message policy stays
//  with that caller; an absent owner requires no attribution.
template<typename T>
[[nodiscard]] static bool post(
    threading::transports::TOwning<T>& transport, T&& value, CErasedOwner& owner) noexcept
{
    memory::CMemoryContext* source_context = nullptr;
    if (owner.is_ready())
    {
        memory::CMemoryContext* const transport_context = transport.memory_context();
        if (!owner.can_reattribute_to(transport_context))
        {
            return false;
        }

        source_context = owner.memory_context();
        const bool reattributed = owner.reattribute(transport_context);
        //  Preflight established ownership validity; accounting diagnostics cannot fail this transfer.
        MV_CRITICAL_ASSERT(reattributed);
        if (!reattributed)
        {
            return false;
        }
    }

    const bool posted = transport.post(std::move(value));
    MV_CRITICAL_ASSERT(posted);
    if (!posted && owner.is_ready())
    {
        const bool restored = owner.reattribute(source_context);
        MV_CRITICAL_ASSERT(restored);
    }
    return posted;
}

}   //  namespace erased_owner_transfer

#endif  //  ERASED_OWNER_TRANSFER_HPP_INCLUDED
