
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    10 Nov 26
//
//  Finite independent outputs force continuation across the normal mapper budget.

#pragma once

#include "tests/support/input_schema.hpp"

namespace input_host
{
inline constexpr std::uint32_t k_expansion_actions = 64u;
inline std::string desktop_schema()
{
    std::string schema = tests::input_schema();
    const std::size_t instances = schema.find("\"instances\":");
    const auto insert = [&](const char* const marker, const std::string& text)
    {
        const std::string key = marker;
        schema.insert((schema.find(key, instances) + key.size()), text);
    };
    std::string actions, sources, bindings, contexts;
    const char* const keys[]{ "key_c", "key_r", "key_u" };
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        const std::string id = std::to_string(20u + index);
        actions += "\"command_" + id + "\":{\"declaration\":{\"id\":" + id + ",\"kind\":\"digital_state\"}},";
        sources += "\"command_" + id + "\":{\"declaration\":{\"id\":" + id + ",\"kind\":\"physical_key\",\"control\":\"" + keys[index] + "\",\"direction\":\"whole\"}},";
        bindings += "\"command_" + id + "\":{\"declaration\":{\"id\":" + id + ",\"profile\":3,\"context\":4,\"action\":" + id +
            ",\"source\":" + id + R"(,"parameters":0,"activation":"regular","allow_during_text":false,"native_repeat":false,"reserved":0}},)";
        contexts += "{\"context\":4,\"action\":" + id + "},";
    }
    insert("\"ActionDefinition\":{", actions);
    insert("\"InputSourceDefinition\":{", sources);
    insert("\"BindingDefinition\":{", bindings);
    insert("\"ContextAction\":{\"members\":{\"data\":[", contexts);
    return schema;
}
inline std::string expanded_schema()
{
    std::string schema = tests::controller_schema();
    std::string actions, bindings, contexts;
    for (std::uint32_t index = 0u; index < k_expansion_actions; ++index)
    {
        const std::string id = std::to_string(150u + index);
        actions += "\"expanded_" + id + "\":{\"declaration\":{\"id\":" + id + ",\"kind\":\"digital_state\"}},";
        for (std::uint32_t context = 4u; context <= 5u; ++context)
        {
            const std::string binding = std::to_string(1000u + (index << 1u) + (context - 4u));
            bindings += "\"expanded_" + binding + "\":{\"declaration\":{\"id\":" + binding +
                ",\"profile\":8,\"context\":" + std::to_string(context) + ",\"action\":" + id +
                R"(,"source":100,"parameters":0,"activation":"regular","allow_during_text":false,"native_repeat":false,"reserved":0}},)";
            contexts += "{\"context\":" + std::to_string(context) + ",\"action\":" + id + "},";
        }
    }
    const auto insert = [&](const char* const marker, const std::string& text)
    {
        const std::string key = marker;
        schema.insert((schema.find(key, schema.find("\"instances\":")) + key.size()), text);
    };
    insert("\"ActionDefinition\":{", actions);
    insert("\"BindingDefinition\":{", bindings);
    insert("\"ContextAction\":{\"members\":{\"data\":[", contexts);
    return schema;
}
}
