//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    erased_transport_admission.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    15 Aug 26
//
//  Destination-aware identity admission and rejection diagnostics.

#include "debug/macros.hpp"
#include "system/erased_transport_admission.hpp"
#include "system/local_type_registry.hpp"
#include "system/system_context.hpp"
#include "system/system_id_registry.hpp"

namespace erased_transport_admission
{

SDecision classify(const type_id identity, const module_ids::id_type destination_module_id) noexcept
{
    SDecision decision{
        identity,
        system_context::get_ambient_module_id(),
        destination_module_id,
        ERejection::none
    };

    if (!module_ids::ops::is_valid_id(decision.source_module_id))
    {
        decision.rejection = ERejection::invalid_source_module;
        return decision;
    }
    if (!module_ids::ops::is_valid_id(destination_module_id))
    {
        decision.rejection = ERejection::invalid_destination_module;
        return decision;
    }
    if (!identity.is_valid())
    {
        decision.rejection = ERejection::invalid_type_identity;
        return decision;
    }

    system_type_id system_identity;
    if (identity.try_system_type_id(system_identity))
    {
        if (system_id_registry::find_type(system_identity) == nullptr)
        {
            decision.rejection = ERejection::unregistered_system_identity;
        }
        return decision;
    }

    local_type_id local_identity;
    if (!identity.try_local_type_id(local_identity))
    {
        decision.rejection = ERejection::invalid_type_identity;
    }
    else if (decision.source_module_id != destination_module_id)
    {
        decision.rejection = ERejection::cross_component_local_identity;
    }
    else if (local_type_registry::find_type(local_identity) == nullptr)
    {
        decision.rejection = ERejection::unregistered_local_identity;
    }
    return decision;
}

bool is_admissible(
    const type_id identity,
    const module_ids::id_type destination_module_id) noexcept
{
    return classify(identity, destination_module_id).is_admitted();
}

namespace util
{

[[nodiscard]] debug_system::CInlineText16 identity_role_text(const EIdentityRole role) noexcept
{
    switch (role)
    {
        case EIdentityRole::pod_message:
        {
            return debug_system::CInlineText16{ "POD message" };
        }
        case EIdentityRole::owner:
        {
            return debug_system::CInlineText16{ "owner" };
        }
        case EIdentityRole::owner_message:
        {
            return debug_system::CInlineText16{ "owner message" };
        }
        case EIdentityRole::owned_payload:
        {
            return debug_system::CInlineText16{ "owned payload" };
        }
        default:
        {
            return debug_system::CInlineText16{ "unknown" };
        }
    }
}

}   //  namespace util

void report_rejection(const SDecision& decision, const EIdentityRole role) noexcept
{
    const debug_system::CInlineText16 role_text = util::identity_role_text(role);
    switch (decision.rejection)
    {
        case ERejection::none:
        {
            return;
        }

        case ERejection::invalid_source_module:
        {
            MV_ASSERT_REPORT("Erased transport rejected {} identity {}: source {} is invalid for destination {}",
                role_text, decision.identity, decision.source_module_id,
                decision.destination_module_id);
            return;
        }

        case ERejection::invalid_destination_module:
        {
            MV_ASSERT_REPORT("Erased transport rejected {} identity {} from {}: destination {} is invalid",
                role_text, decision.identity, decision.source_module_id,
                decision.destination_module_id);
            return;
        }

        case ERejection::invalid_type_identity:
        {
            MV_ASSERT_REPORT("Erased transport rejected {} identity {} from {} to {}: identity is invalid",
                role_text, decision.identity, decision.source_module_id,
                decision.destination_module_id);
            return;
        }

        case ERejection::unregistered_system_identity:
        {
            MV_ASSERT_REPORT("Erased transport rejected {} identity {} from {} to {}: SYSTEM identity is unregistered",
                role_text, decision.identity, decision.source_module_id,
                decision.destination_module_id);
            return;
        }

        case ERejection::cross_component_local_identity:
        {
            MV_ASSERT_REPORT("Erased transport rejected {} identity {} from {} to {}: LOCAL identity cannot cross components",
                role_text, decision.identity, decision.source_module_id,
                decision.destination_module_id);
            return;
        }

        case ERejection::unregistered_local_identity:
        {
            MV_ASSERT_REPORT("Erased transport rejected {} identity {} from {} to {}: LOCAL identity is unregistered",
                role_text, decision.identity, decision.source_module_id,
                decision.destination_module_id);
            return;
        }
        default:
        {
            MV_ASSERT_REPORT("Erased transport rejected {} identity {} from {} to {}: rejection reason is invalid",
                role_text, decision.identity, decision.source_module_id,
                decision.destination_module_id);
            return;
        }
    }
}

}   //  namespace erased_transport_admission
