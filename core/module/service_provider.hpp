
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_provider.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Optional module service advertisement, independent of execution capability.

#pragma once

#include <type_traits>
#include "module/module_binding.hpp"
#include "services/service_protocol.hpp"

namespace modules
{

inline constexpr char k_service_provider_symbol_name[]{ "morphic_module_service_provider_v1" };

struct SServiceProviderV1
{
    [[nodiscard]] bool is_valid() const noexcept;

    std::uint32_t size{ sizeof(SServiceProviderV1) };
    std::uint32_t version{ 1u };
    std::uint32_t flags{ 0u };
    std::uint32_t contract_count{ 0u };
    //  Immutable storage remains valid until the module binding is released.
    const services::SServiceContract* contracts{ nullptr };
};
using FQueryServiceProvider = EBindingResult(MV_STD_ABI_CALL*)(std::uint32_t, SServiceProviderV1*) noexcept;

inline bool SServiceProviderV1::is_valid() const noexcept
{
    if ((size != sizeof(SServiceProviderV1)) || (version != 1u) || (flags != 0u) ||
        (contract_count > 16u) || ((contract_count != 0u) && (contracts == nullptr)))
    {
        return false;
    }
    for (std::uint32_t index = 0u; index < contract_count; ++index)
    {
        const auto& contract = contracts[index];
        if ((contract.number == 0u) || (contract.version == 0u)) return false;
        for (std::uint32_t previous = 0u; previous < index; ++previous)
        {
            if (contracts[previous].number == contract.number)
            {
                return false;
            }
        }
    }
    return true;
}

static_assert(std::is_standard_layout_v<SServiceProviderV1> && std::is_trivially_copyable_v<SServiceProviderV1>);

}   //  namespace modules
