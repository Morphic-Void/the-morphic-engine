
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    erased_transport_admission.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Aug 26
//
//  Destination-aware identity admission for concrete erased transports.

#pragma once

#ifndef ERASED_TRANSPORT_ADMISSION_HPP_INCLUDED
#define ERASED_TRANSPORT_ADMISSION_HPP_INCLUDED

#include <cstdint>

#include "foundation/system/system_ids.hpp"

namespace erased_transport_admission
{

enum class ERejection : std::uint8_t
{
    none = 0u,
    invalid_source_module,
    invalid_destination_module,
    invalid_type_identity,
    unregistered_system_identity,
    cross_component_local_identity,
    unregistered_local_identity
};

enum class EIdentityRole : std::uint8_t
{
    pod_message = 0u,
    owner,
    owner_message,
    owned_payload
};

struct SDecision
{
    type_id identity{ type_ids::undefined };
    module_ids::id_type source_module_id{};
    module_ids::id_type destination_module_id{};
    ERejection rejection{ ERejection::invalid_type_identity };

    [[nodiscard]] constexpr bool is_admitted() const noexcept { return rejection == ERejection::none; }
};

[[nodiscard]] SDecision classify(const type_id identity, const module_ids::id_type destination_module_id) noexcept;

[[nodiscard]] bool is_admissible(const type_id identity, const module_ids::id_type destination_module_id) noexcept;

void report_rejection(const SDecision& decision, const EIdentityRole role) noexcept;

}   //  namespace erased_transport_admission

#endif  //  ERASED_TRANSPORT_ADMISSION_HPP_INCLUDED
