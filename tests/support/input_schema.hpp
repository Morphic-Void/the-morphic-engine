
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_schema.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    06 Nov 26
//
//  Synthetic complete v1 configuration, authored independently of runtime tables.

#pragma once

#include <string>
#include <initializer_list>
#include <utility>

namespace tests
{

inline std::string input_schema(const unsigned extra_bindings = 0u)
{
    std::string result = R"({"types":{"enumerations":{)";
    bool first = true;
    const auto enumeration = [&](const char* const name, const std::initializer_list<const char*> labels)
    {
        if (!first) result += ',';
        first = false;
        result += '"' + std::string(name) + R"(":{"storage":"u32","values":{)";
        unsigned value = 0u;
        for (const char* const label : labels)
        {
            if (value) result += ',';
            result += '"' + std::string(label) + "\":" + std::to_string(value++);
        }
        result += "}}";
    };
    enumeration("ActionKind", { "digital_state", "pulse", "scalar_state", "vector_state", "relative_delta" });
    enumeration("ConnectionKind", { "desktop", "controller" });
    enumeration("ActivationKind", { "regular", "start_press", "release_press", "direct", "threshold", "chord" });
    enumeration("SourceKind", { "physical_key", "mouse_button", "mouse_position", "mouse_wheel", "gamepad_button", "gamepad_stick", "gamepad_trigger" });
    enumeration("Direction", { "whole", "positive_x", "negative_x", "positive_y", "negative_y" });
    enumeration("ShortcutOperation", { "toggle_windowed", "toggle_exclusive" });
    result += R"(,"ControlCode":{"storage":"u32","values":{"none":0)";
    for (unsigned index = 0u; index < 26u; ++index)
    {
        result += ",\"key_" + std::string(1u, static_cast<char>('a' + index)) + "\":" + std::to_string(index + 1u);
    }
    for (unsigned index = 0u; index < 10u; ++index)
    {
        result += ",\"digit_" + std::to_string((index + 1u) % 10u) + "\":" + std::to_string(index + 27u);
    }
    const auto codes = [&](unsigned value, const std::initializer_list<const char*> labels)
    {
        for (const char* const label : labels)
        {
            result += ",\"" + std::string(label) + "\":" + std::to_string(value++);
        }
    };
    codes(37u, { "enter", "escape", "backspace", "tab", "space", "minus", "equal", "left_bracket", "right_bracket",
        "backslash", "semicolon", "apostrophe", "grave", "comma", "period", "slash", "caps_lock", "f1", "f2", "f3", "f4", "f5",
        "f6", "f7", "f8", "f9", "f10", "f11", "f12", "print_screen", "scroll_lock", "pause", "insert", "home", "page_up",
        "key_delete", "end", "page_down", "right", "left", "down", "up", "num_lock", "keypad_divide", "keypad_multiply", "keypad_subtract",
        "keypad_add", "keypad_enter", "keypad_1", "keypad_2", "keypad_3", "keypad_4", "keypad_5", "keypad_6", "keypad_7", "keypad_8",
        "keypad_9", "keypad_0", "keypad_decimal", "non_us_backslash", "left_ctrl", "left_shift", "left_alt", "left_super",
        "right_ctrl", "right_shift", "right_alt", "right_super", "application_menu", "either_shift", "either_ctrl", "either_alt", "either_super", "alt_graph" });
    codes(128u, { "mouse_primary", "mouse_secondary", "mouse_middle", "mouse_x1", "mouse_x2", "mouse_position", "mouse_wheel" });
    codes(256u, { "pad_south", "pad_east", "pad_west", "pad_north", "pad_left_shoulder", "pad_right_shoulder", "pad_left_click",
        "pad_right_click", "pad_up", "pad_down", "pad_left", "pad_right", "pad_menu", "pad_view", "pad_left_stick", "pad_right_stick", "pad_left_trigger", "pad_right_trigger" });
    result += R"(}}},"structures":{)";
    first = true;
    const auto structure = [&](const char* const name, const std::initializer_list<std::pair<const char*, const char*>> fields)
    {
        if (!first) result += ',';
        first = false;
        result += '"' + std::string(name) + R"(":{"members":[)";
        bool initial = true;
        for (const std::pair<const char*, const char*>& field : fields)
        {
            if (!initial) result += ',';
            initial = false;
            result += "{\"" + std::string(field.first) + "\":{\"type\":\"" + field.second + "\"}}";
        }
        result += "]}";
    };
    structure("ConfigurationHeader", {{ "format_version", "u32" }});
    structure("ActionDefinition", {{ "id", "u32" }, { "kind", "ActionKind" }});
    structure("ContextDefinition", {{ "id", "u32" }});
    structure("ContextAction", {{ "context", "u32" }, { "action", "u32" }});
    structure("BindingProfile", {{ "id", "u32" }, { "connection_kind", "ConnectionKind" }});
    structure("BindingDefinition", {{ "id", "u32" }, { "profile", "u32" }, { "context", "u32" }, { "action", "u32" },
        { "source", "u32" }, { "parameters", "u32" }, { "activation", "ActivationKind" }, { "allow_during_text", "b8" },
        { "native_repeat", "b8" }, { "reserved", "u16" }});
    structure("InputSourceDefinition", {{ "id", "u32" }, { "kind", "SourceKind" }, { "control", "ControlCode" }, { "direction", "Direction" }});
    structure("BindingModifier", {{ "binding", "u32" }, { "source", "u32" }});
    structure("AnalogParameters", {{ "id", "u32" }, { "dead_zone", "f32" }, { "neutral_enter", "f32" },
        { "neutral_leave", "f32" }, { "press", "f32" }, { "release", "f32" }, { "scale", "f32" }});
    structure("WindowPolicy", {{ "id", "u32" }});
    structure("ShortcutDefinition", {{ "id", "u32" }, { "policy", "u32" }, { "primary_source", "u32" }, { "operation", "ShortcutOperation" }});
    structure("ShortcutModifier", {{ "shortcut", "u32" }, { "source", "u32" }});
    result += R"(}},"instances":{
        "ConfigurationHeader":{"configuration":{"declaration":{"format_version":1}}},
        "ActionDefinition":{"held":{"declaration":{"id":7,"kind":"digital_state"}},"move":{"declaration":{"id":9,"kind":"vector_state"}}},
        "ContextDefinition":{"scene":{"declaration":{"id":4}}},
        "BindingProfile":{"desktop":{"declaration":{"id":3,"connection_kind":"desktop"}},"pad":{"declaration":{"id":8,"connection_kind":"controller"}}},
        "InputSourceDefinition":{"key":{"declaration":{"id":2,"kind":"physical_key","control":"key_a","direction":"whole"}},
            "stick":{"declaration":{"id":6,"kind":"gamepad_stick","control":"pad_left_stick","direction":"whole"}}},
        "BindingDefinition":{"key_binding":{"declaration":{"id":5,"profile":3,"context":4,"action":7,"source":2,"parameters":0,
            "activation":"regular","allow_during_text":false,"native_repeat":false,"reserved":0}},
            "stick_binding":{"declaration":{"id":11,"profile":8,"context":4,"action":9,"source":6,"parameters":0,
            "activation":"direct","allow_during_text":false,"native_repeat":false,"reserved":0}})";
    for (unsigned index = 0u; index < extra_bindings; ++index)
    {
        //  Reverse IDs exercise incremental sorting independently of record order.
        result += ",\"extra_" + std::to_string(index) + "\":{\"declaration\":{\"id\":" + std::to_string(100u + extra_bindings - index) +
            R"(,"profile":3,"context":4,"action":7,"source":2,"parameters":0,"activation":"regular","allow_during_text":false,"native_repeat":false,"reserved":0}})";
    }
    result += R"(}},"data":{"ContextAction":{"members":{"data":[{"context":4,"action":7},{"context":4,"action":9}]}}}})";
    return result;
}

//  Native acceptance binds every standard control in two independently selectable
//  contexts. Action IDs 100..117 follow the public portable gamepad control order.
inline std::string controller_schema()
{
    std::string result = input_schema();
    result.resize(result.find("\"instances\":"));
    result += R"("instances":{"ConfigurationHeader":{"configuration":{"declaration":{"format_version":1}}},
        "ContextDefinition":{"first":{"declaration":{"id":4}},"second":{"declaration":{"id":5}}},
        "BindingProfile":{"pad":{"declaration":{"id":8,"connection_kind":"controller"}}},"ActionDefinition":{)";
    constexpr const char* controls[]{ "pad_south", "pad_east", "pad_west", "pad_north", "pad_left_shoulder", "pad_right_shoulder",
        "pad_left_click", "pad_right_click", "pad_up", "pad_down", "pad_left", "pad_right", "pad_menu", "pad_view",
        "pad_left_stick", "pad_right_stick", "pad_left_trigger", "pad_right_trigger" };
    constexpr unsigned count = sizeof(controls) / sizeof(controls[0]);
    for (unsigned index = 0u; index < count; ++index)
    {
        if (index) result += ',';
        const char* const kind = (index < 14u) ? "digital_state" : ((index < 16u) ? "vector_state" : "scalar_state");
        result += '"' + std::to_string(index) + R"(":{"declaration":{"id":)" + std::to_string(100u + index) +
            R"(,"kind":")" + kind + "\"}}";
    }
    result += R"(},"InputSourceDefinition":{)";
    for (unsigned index = 0u; index < count; ++index)
    {
        if (index) result += ',';
        const char* const kind = (index < 14u) ? "gamepad_button" : ((index < 16u) ? "gamepad_stick" : "gamepad_trigger");
        result += '"' + std::to_string(index) + R"(":{"declaration":{"id":)" + std::to_string(100u + index) +
            R"(,"kind":")" + kind + R"(","control":")" + controls[index] + R"(","direction":"whole"}})";
    }
    result += R"(},"BindingDefinition":{)";
    for (unsigned index = 0u; index < (count << 1u); ++index)
    {
        if (index) result += ',';
        const unsigned control = index % count;
        result += '"' + std::to_string(index) + R"(":{"declaration":{"id":)" + std::to_string(200u + index) +
            R"(,"profile":8,"context":)" + std::to_string(4u + (index / count)) + R"(,"action":)" + std::to_string(100u + control) +
            R"(,"source":)" + std::to_string(100u + control) + R"(,"parameters":0,"activation":")" +
            ((control < 14u) ? "regular" : "direct") + R"(","allow_during_text":false,"native_repeat":false,"reserved":0}})";
    }
    result += R"(}},"data":{"ContextAction":{"members":{"data":[)";
    for (unsigned index = 0u; index < (count << 1u); ++index)
    {
        if (index) result += ',';
        result += R"({"context":)" + std::to_string(4u + (index / count)) + R"(,"action":)" + std::to_string(100u + (index % count)) + '}';
    }
    result += "]}}}}";
    return result;
}

}   // namespace tests
