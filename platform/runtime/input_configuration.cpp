
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_configuration.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Incremental input-contract validation, ID indexing and independent runtime data.

#include "runtime/input_configuration.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace platform::runtime
{

using input::EConfigurationError;
using Table = EConfigurationTable;

struct SFieldContract { const char* name; const char* type; std::uint32_t offset; };
struct STableContract
{
    const char* name;
    std::uint32_t count, size, maximum;
    bool bulk;
    SFieldContract fields[10];
};

static const STableContract k_tables[]{
    { "ConfigurationHeader", 1u, 4u, 1u, false, {{"format_version", "u32", 0u}} },
    { "ActionDefinition", 2u, 8u, 1024u, false, {{"id", "u32", 0u}, {"kind", "ActionKind", 4u}} },
    { "ContextDefinition", 1u, 4u, 128u, false, {{"id", "u32", 0u}} },
    { "ContextAction", 2u, 8u, 32768u, true, {{"context", "u32", 0u}, {"action", "u32", 4u}} },
    { "BindingProfile", 2u, 8u, 32u, false, {{"id", "u32", 0u}, {"connection_kind", "ConnectionKind", 4u}} },
    { "BindingDefinition", 10u, 32u, 8192u, false, {{"id", "u32", 0u}, {"profile", "u32", 4u},
        {"context", "u32", 8u}, {"action", "u32", 12u}, {"source", "u32", 16u}, {"parameters", "u32", 20u},
        {"activation", "ActivationKind", 24u}, {"allow_during_text", "b8", 28u}, {"native_repeat", "b8", 29u}, {"reserved", "u16", 30u}} },
    { "InputSourceDefinition", 4u, 16u, 8192u, false, {{"id", "u32", 0u}, {"kind", "SourceKind", 4u},
        {"control", "ControlCode", 8u}, {"direction", "Direction", 12u}} },
    { "BindingModifier", 2u, 8u, 32768u, true, {{"binding", "u32", 0u}, {"source", "u32", 4u}} },
    { "AnalogParameters", 7u, 28u, 8192u, false, {{"id", "u32", 0u}, {"dead_zone", "f32", 4u},
        {"neutral_enter", "f32", 8u}, {"neutral_leave", "f32", 12u}, {"press", "f32", 16u},
        {"release", "f32", 20u}, {"scale", "f32", 24u}} },
    { "WindowPolicy", 1u, 4u, 32u, false, {{"id", "u32", 0u}} },
    { "ShortcutDefinition", 4u, 16u, 8192u, false, {{"id", "u32", 0u}, {"policy", "u32", 4u},
        {"primary_source", "u32", 8u}, {"operation", "ShortcutOperation", 12u}} },
    { "ShortcutModifier", 2u, 8u, 32768u, true, {{"shortcut", "u32", 0u}, {"source", "u32", 4u}} }
};

static bool equal(const CStringView value, const char* const expected) noexcept
{
    const std::size_t size = std::strlen(expected);
    return (value.length() == size) && (!size || (std::memcmp(value.cstring(), expected, size) == 0));
}

static bool enumeration(const schema::CResolvedTypes& types, const char* const name,
    const char* const* const labels, const std::uint32_t count) noexcept
{
    const schema::CTypesIndex type = types.find_type(CStringView{ name });
    schema::SType layout;
    if (!types.type(type, layout) || (layout.category != schema::ECategory::enumeration) ||
        (layout.size != 4u) || (layout.count != count) ||
        (layout.element_or_storage != types.find_type(CStringView{ "u32" })))
    {
        return false;
    }
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        schema::SLabel label;
        if (!types.label(types.find_label(type, CStringView{ labels[index] }), label) ||
            (label.value.kind != schema::EScalar::unsigned_integer) || (label.value.value.unsigned_value != index))
        {
            return false;
        }
    }
    return true;
}

static bool control_name(const std::uint32_t code, char (&name)[32]) noexcept
{
    static const char* const keys[]{ "enter", "escape", "backspace", "tab", "space", "minus", "equal",
        "left_bracket", "right_bracket", "backslash", "semicolon", "apostrophe", "grave", "comma", "period",
        "slash", "caps_lock", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
        "print_screen", "scroll_lock", "pause", "insert", "home", "page_up", "key_delete", "end", "page_down",
        "right", "left", "down", "up", "num_lock", "keypad_divide", "keypad_multiply", "keypad_subtract",
        "keypad_add", "keypad_enter", "keypad_1", "keypad_2", "keypad_3", "keypad_4", "keypad_5", "keypad_6",
        "keypad_7", "keypad_8", "keypad_9", "keypad_0", "keypad_decimal", "non_us_backslash", "left_ctrl",
        "left_shift", "left_alt", "left_super", "right_ctrl", "right_shift", "right_alt", "right_super",
        "application_menu", "either_shift", "either_ctrl", "either_alt", "either_super", "alt_graph" };
    static const char* const mouse[]{ "mouse_primary", "mouse_secondary", "mouse_middle", "mouse_x1", "mouse_x2", "mouse_position", "mouse_wheel" };
    static const char* const pad[]{ "pad_south", "pad_east", "pad_west", "pad_north", "pad_left_shoulder", "pad_right_shoulder",
        "pad_left_click", "pad_right_click", "pad_up", "pad_down", "pad_left", "pad_right", "pad_menu", "pad_view",
        "pad_left_stick", "pad_right_stick", "pad_left_trigger", "pad_right_trigger" };
    const char* text = nullptr;
    if (!code) text = "none";
    else if (code <= 26u)
    {
        std::memcpy(name, "key_a", 6u);
        name[4] = static_cast<char>('a' + (code - 1u));
        return true;
    }
    else if (code <= 36u)
    {
        std::memcpy(name, "digit_0", 8u);
        name[6] = static_cast<char>('0' + ((code - 26u) % 10u));
        return true;
    }
    else if (code <= 110u) text = keys[code - 37u];
    else if ((code >= 128u) && (code <= 134u)) text = mouse[code - 128u];
    else if ((code >= 256u) && (code <= 273u)) text = pad[code - 256u];
    else return false;
    std::memcpy(name, text, (std::strlen(text) + 1u));
    return true;
}

static bool enums(const schema::CResolvedTypes& types, const std::uint32_t ordinal) noexcept
{
    static const char* const actions[]{ "digital_state", "pulse", "scalar_state", "vector_state", "relative_delta" };
    static const char* const connections[]{ "desktop", "controller" };
    static const char* const activations[]{ "regular", "start_press", "release_press", "direct", "threshold", "chord" };
    static const char* const sources[]{ "physical_key", "mouse_button", "mouse_position", "mouse_wheel", "gamepad_button", "gamepad_stick", "gamepad_trigger" };
    static const char* const directions[]{ "whole", "positive_x", "negative_x", "positive_y", "negative_y" };
    static const char* const shortcuts[]{ "toggle_windowed", "toggle_exclusive" };
    if (!ordinal)
    {
        return enumeration(types, "ActionKind", actions, 5u) && enumeration(types, "ConnectionKind", connections, 2u) &&
            enumeration(types, "ActivationKind", activations, 6u) && enumeration(types, "SourceKind", sources, 7u) &&
            enumeration(types, "Direction", directions, 5u) && enumeration(types, "ShortcutOperation", shortcuts, 2u);
    }
    const schema::CTypesIndex controls = types.find_type(CStringView{ "ControlCode" });
    schema::SType layout;
    if (!types.type(controls, layout) || (layout.category != schema::ECategory::enumeration) ||
        (layout.size != 4u) || (layout.count != 136u) ||
        (layout.element_or_storage != types.find_type(CStringView{ "u32" })))
    {
        return false;
    }
    schema::SLabel label;
    char expected[32]{};
    return types.label(types.label_at(controls, ordinal - 1u), label) &&
        (label.value.kind == schema::EScalar::unsigned_integer) && (label.value.value.unsigned_value <= UINT32_MAX) &&
        control_name(static_cast<std::uint32_t>(label.value.value.unsigned_value), expected) &&
        equal(types.name(label.name), expected);
}

bool CInputConfiguration::fail(const EConfigurationError reason, const std::uint32_t definition,
    const std::uint32_t field) noexcept
{
    m_error.error = reason;
    m_error.category = static_cast<std::uint16_t>(m_table);
    m_error.definition = definition;
    m_error.field = field;
    m_stage = EStage::failed;
    return false;
}

bool CInputConfiguration::begin(const schema::SPreparedSchemaV1* const prepared) noexcept
{
    m_view = schema::CPreparedSchemaView{ prepared };
    if (!m_view.is_ready())
    {
        return fail(EConfigurationError::schema);
    }
    if (!m_profiles.initialise())
    {
        return fail(EConfigurationError::allocation);
    }
    m_stage = EStage::enumerations;
    return true;
}

bool CInputConfiguration::layout() noexcept
{
    const schema::CResolvedTypes& types = *m_view.types();
    const STableContract& expected = k_tables[m_table];
    SConfigurationTable& table = m_tables[m_table];
    table.type = types.find_type(CStringView{ expected.name });
    schema::SType value;
    if (!types.type(table.type, value) || (value.category != schema::ECategory::structure) ||
        (value.count != expected.count) || (value.size != expected.size) || (value.stride != expected.size) ||
        (value.alignment != 4u))
    {
        return fail(EConfigurationError::layout);
    }
    for (std::uint32_t field = 0u; field < expected.count; ++field)
    {
        schema::SMember member;
        if (!types.member(types.member_at(table.type, field), member) ||
            !equal(types.name(member.name), expected.fields[field].name) || (member.offset != expected.fields[field].offset) ||
            (member.type != types.find_type(CStringView{ expected.fields[field].type })))
        {
            return fail(EConfigurationError::layout, 0u, field);
        }
    }
    if (++m_table == 12u)
    {
        m_table = m_cursor = 0u;
        m_stage = EStage::inventory;
    }
    return true;
}

bool CInputConfiguration::inventory_step() noexcept
{
    if (m_cursor == (m_view.instance_count() + m_view.bulk_count()))
    {
        if (m_inventory[0] != 1u)
        {
            return fail(EConfigurationError::version);
        }
        m_cursor = m_total_relationships = 0u;
        m_stage = EStage::instances;
        return true;
    }
    const bool bulk = m_cursor >= m_view.instance_count();
    const schema::SPreparedInstance* const instance = bulk ? nullptr : m_view.instance_at(m_cursor);
    const schema::SPreparedBulk* const collection = bulk ? m_view.bulk_at(m_cursor - m_view.instance_count()) : nullptr;
    ++m_cursor;
    const schema::CTypesIndex type = bulk ? collection->type : instance->type;
    m_table = 0u;
    while ((m_table < 12u) && (m_tables[m_table].type != type)) ++m_table;
    if (m_table == 12u)
    {
        return fail(EConfigurationError::layout);
    }
    const STableContract& expected = k_tables[m_table];
    const std::uint32_t count = bulk ? collection->count : 1u;
    const CStringView name = bulk ? collection->name : instance->name;
    if (bulk != expected.bulk)
    {
        return fail(EConfigurationError::layout);
    }
    if ((name.length() > 127u) || (count > (expected.maximum - m_inventory[m_table])) ||
        (bulk && (count > (32768u - m_total_relationships))))
    {
        return fail(EConfigurationError::limit);
    }
    m_inventory[m_table] += count;
    if (bulk) m_total_relationships += count;
    return true;
}

bool CInputConfiguration::copy(const schema::CTypesIndex type, const CStringView name,
    const std::uint8_t* const bytes, const std::size_t size, const bool bulk) noexcept
{
    m_table = 0u;
    while ((m_table < 12u) && (m_tables[m_table].type != type)) ++m_table;
    if (m_table == 12u)
    {
        return fail(EConfigurationError::layout);
    }
    const STableContract& expected = k_tables[m_table];
    SConfigurationTable& table = m_tables[m_table];
    if ((bulk != expected.bulk) || (size != expected.size) || !bytes)
    {
        return fail(EConfigurationError::layout);
    }
    if ((name.length() > 127u) || (table.rows.size() >= expected.maximum) || (bulk && (++m_total_relationships > 32768u)))
    {
        return fail(EConfigurationError::limit);
    }
    SConfigurationRow row;
    std::memcpy(row.values, bytes, size);
    if (name.length()) std::memcpy(row.name, name.cstring(), name.length());
    if (!row.values[0])
    {
        return fail(EConfigurationError::value);
    }
    if (!m_table && (!equal(name, "configuration") || (row.values[0] != 1u)))
    {
        return fail(EConfigurationError::version);
    }
    const std::uint32_t index = table.rows.size();
    if (!table.rows.push(row))
    {
        return fail(EConfigurationError::allocation);
    }
    //  Append each index in a later step so one turn never grows several page
    //  directories together with a record page.
    m_copy_index = index;
    m_copy_stage = 1u;
    return true;
}

std::uint64_t CInputConfiguration::key(const SConfigurationRow& row, const std::uint32_t table) noexcept
{
    if (k_tables[table].bulk)
    {
        return (static_cast<std::uint64_t>(row.values[0]) << 32u) | row.values[1];
    }
    return row.values[0];
}

bool CInputConfiguration::sort_step() noexcept
{
    if (m_table == 12u)
    {
        m_table = m_cursor = 0u;
        m_stage = EStage::duplicates;
        return true;
    }
    SConfigurationTable& table = m_tables[m_table];
    const std::uint32_t count = table.rows.size();
    if (m_width >= count)
    {
        ++m_table;
        m_width = 1u;
        m_begin = m_end = m_output = 0u;
        return true;
    }
    if (m_begin >= count)
    {
        table.read_order ^= 1u;
        m_width *= 2u;
        m_begin = m_end = m_output = 0u;
        return true;
    }
    if (m_output == m_end)
    {
        m_left = m_begin;
        m_right = std::min((m_begin + m_width), count);
        m_end = std::min((m_begin + (2u * m_width)), count);
        m_output = m_begin;
    }
    TConfigurationPages<std::uint32_t>& source = table.order[table.read_order];
    const std::uint32_t middle = std::min((m_begin + m_width), count);
    const bool left = (m_left < middle) && ((m_right >= m_end) ||
        (key(table.rows.at(source.at(m_left)), m_table) <= key(table.rows.at(source.at(m_right)), m_table)));
    table.order[table.read_order ^ 1u].at(m_output++) = source.at(left ? m_left++ : m_right++);
    if (m_output == m_end) m_begin = m_end;
    return true;
}

const SConfigurationRow* CInputConfiguration::find(const Table type, const std::uint32_t id) const noexcept
{
    const SConfigurationTable& data = table(type);
    std::uint32_t begin = 0u, end = data.rows.size();
    while (begin < end)
    {
        const std::uint32_t middle = begin + ((end - begin) / 2u);
        if (data.sorted(middle).values[0] < id) begin = middle + 1u;
        else end = middle;
    }
    return ((begin < data.rows.size()) && (data.sorted(begin).values[0] == id)) ? &data.sorted(begin) : nullptr;
}

bool CInputConfiguration::contains(const Table type, const std::uint32_t first, const std::uint32_t second) const noexcept
{
    const SConfigurationTable& data = table(type);
    const std::uint64_t target = (static_cast<std::uint64_t>(first) << 32u) | second;
    std::uint32_t begin = 0u, end = data.rows.size();
    while (begin < end)
    {
        const std::uint32_t middle = begin + ((end - begin) / 2u);
        if (key(data.sorted(middle), static_cast<std::uint32_t>(type)) < target) begin = middle + 1u;
        else end = middle;
    }
    return (begin < data.rows.size()) && (key(data.sorted(begin), static_cast<std::uint32_t>(type)) == target);
}

static bool digital_source(const SConfigurationRow& source) noexcept
{
    return (source.values[1] == 0u) || (source.values[1] == 1u) || (source.values[1] == 4u);
}

bool CInputConfiguration::reference_step() noexcept
{
    if (m_table == 12u)
    {
        m_stage = EStage::grouping;
        m_width = 1u;
        m_begin = m_end = m_output = 0u;
        m_cursor = 0u;
        return true;
    }
    const SConfigurationTable& data = m_tables[m_table];
    if (m_cursor == data.rows.size())
    {
        ++m_table;
        m_cursor = 0u;
        return true;
    }
    const SConfigurationRow& row = data.sorted(m_cursor++);
    const std::uint32_t* const v = row.values;
    switch (static_cast<Table>(m_table))
    {
        case Table::header:
        case Table::context:
        case Table::policy:
        {
            break;
        }
        case Table::action:
        {
            if (v[1] > 4u)
            {
                return fail(EConfigurationError::value, v[0]);
            }
            break;
        }
        case Table::profile:
        {
            if (v[1] > 1u)
            {
                return fail(EConfigurationError::value, v[0]);
            }
            break;
        }
        case Table::membership:
        {
            if (!find(Table::context, v[0]) || !find(Table::action, v[1]))
            {
                return fail(EConfigurationError::reference, v[0]);
            }
            break;
        }
        case Table::source:
        {
            const std::uint32_t kind = v[1], code = v[2], direction = v[3];
            const bool valid = ((kind == 0u) && (code >= 1u) && (code <= 110u)) ||
                ((kind == 1u) && (code >= 128u) && (code <= 132u)) || ((kind == 2u) && (code == 133u)) ||
                ((kind == 3u) && (code == 134u)) || ((kind == 4u) && (code >= 256u) && (code <= 269u)) ||
                ((kind == 5u) && ((code == 270u) || (code == 271u))) ||
                ((kind == 6u) && ((code == 272u) || (code == 273u)));
            if (!valid || (direction > 4u) || ((digital_source(row) || (kind == 6u)) && direction))
            {
                return fail(EConfigurationError::value, v[0]);
            }
            break;
        }
        case Table::analog:
        {
            float values[6];
            std::memcpy(values, (v + 1u), sizeof(values));
            for (const float value : values)
            {
                if (!std::isfinite(value))
                {
                    return fail(EConfigurationError::value, v[0]);
                }
            }
            if ((values[0] < 0.0f) || (values[0] >= 1.0f) || (values[1] < 0.0f) ||
                (values[1] >= values[2]) || (values[2] > 1.0f) || (values[4] < 0.0f) ||
                (values[4] >= values[3]) || (values[3] > 1.0f))
            {
                return fail(EConfigurationError::value, v[0]);
            }
            break;
        }
        case Table::binding:
        {
            const SConfigurationRow* const profile = find(Table::profile, v[1]);
            const SConfigurationRow* const action = find(Table::action, v[3]);
            const SConfigurationRow* const source = find(Table::source, v[4]);
            if (!profile || !action || !source || !contains(Table::membership, v[2], v[3]) ||
                (v[5] && !find(Table::analog, v[5])))
            {
                return fail(EConfigurationError::reference, v[0]);
            }
            const std::uint32_t activation = v[6], flags = v[7], kind = action->values[1];
            const bool controller = source->values[1] >= 4u;
            const bool pulse = (activation == 1u) || (activation == 2u);
            const bool digital = digital_source(*source);
            const bool projected = source->values[3] != 0u;
            const bool modifiers = find(Table::modifier, v[0]) != nullptr;
            if ((profile->values[1] != (controller ? 1u : 0u)) || (activation > 5u) ||
                (flags & 0xffff0000u) || ((flags & 255u) > 1u) || (((flags >> 8u) & 255u) > 1u) ||
                (pulse != (kind == 1u)) || ((pulse || (activation == 0u) || (activation == 5u)) && !digital) ||
                ((activation == 0u) && (kind != 0u)) ||
                ((activation == 5u) && ((kind != 0u) || !modifiers)) ||
                (modifiers && !pulse && (activation != 5u)) ||
                ((activation == 4u) && ((kind != 0u) || digital ||
                    ((source->values[1] != 6u) && !projected))) ||
                ((activation == 3u) && ((kind < 2u) || digital)) ||
                ((kind >= 2u) && (activation != 3u)) ||
                ((kind == 3u) && ((source->values[1] != 5u) || source->values[3])) ||
                ((kind == 2u) && ((source->values[1] == 3u) ||
                    ((source->values[1] != 6u) && !projected))) ||
                ((kind == 4u) && (source->values[1] != 3u)))
            {
                return fail(EConfigurationError::combination, v[0]);
            }
            if (flags & 256u)
            {
                const std::uint32_t code = source->values[2];
                const bool editing = (code == 39u) || ((code >= 70u) && (code <= 78u));
                if ((source->values[1] != 0u) || (activation != 1u) || !editing)
                {
                    return fail(EConfigurationError::combination, v[0]);
                }
            }
            break;
        }
        case Table::modifier:
        case Table::shortcut_modifier:
        {
            const bool shortcut = static_cast<Table>(m_table) == Table::shortcut_modifier;
            const SConfigurationRow* const target = find(shortcut ? Table::shortcut : Table::binding, v[0]);
            const SConfigurationRow* const source = find(Table::source, v[1]);
            if (!target || !source)
            {
                return fail(EConfigurationError::reference, v[0]);
            }
            const SConfigurationRow* const primary = find(Table::source, target->values[shortcut ? 2u : 4u]);
            if (!primary || !digital_source(*source) ||
                ((primary->values[1] >= 4u) != (source->values[1] >= 4u)) ||
                (v[1] == target->values[shortcut ? 2u : 4u]))
            {
                return fail(EConfigurationError::combination, v[0]);
            }
            break;
        }
        case Table::shortcut:
        {
            const SConfigurationRow* const source = find(Table::source, v[2]);
            if (!find(Table::policy, v[1]) || !source)
            {
                return fail(EConfigurationError::reference, v[0]);
            }
            if ((source->values[1] > 1u) || (v[3] > 1u))
            {
                return fail(EConfigurationError::combination, v[0]);
            }
            break;
        }
        default:
        {
            return fail(EConfigurationError::layout);
        }
    }
    return true;
}

static bool binding_less(const SConfigurationRow& left, const SConfigurationRow& right) noexcept
{
    for (const std::uint32_t field : { 1u, 2u, 3u, 0u })
    {
        if (left.values[field] != right.values[field])
        {
            return left.values[field] < right.values[field];
        }
    }
    return false;
}

bool CInputConfiguration::group_step() noexcept
{
    const SConfigurationTable& bindings = table(Table::binding);
    const std::uint32_t count = bindings.rows.size();
    if (m_width >= count)
    {
        m_cursor = 1u;
        m_stage = EStage::conflicts;
        return true;
    }
    if (m_begin >= count)
    {
        m_binding_read ^= 1u;
        m_width *= 2u;
        m_begin = m_end = m_output = 0u;
        return true;
    }
    if (m_output == m_end)
    {
        m_left = m_begin;
        m_right = std::min((m_begin + m_width), count);
        m_end = std::min((m_begin + (2u * m_width)), count);
        m_output = m_begin;
    }
    const TConfigurationPages<std::uint32_t>& source = m_binding_order[m_binding_read];
    const bool left = (m_left < std::min((m_begin + m_width), count)) && ((m_right >= m_end) ||
        binding_less(bindings.rows.at(source.at(m_left)), bindings.rows.at(source.at(m_right))));
    m_binding_order[m_binding_read ^ 1u].at(m_output++) = source.at(left ? m_left++ : m_right++);
    if (m_output == m_end) m_begin = m_end;
    return true;
}

bool CInputConfiguration::profile_step() noexcept
{
    const SConfigurationTable& profiles = table(Table::profile);
    if (m_profile_cursor == profiles.rows.size())
    {
        m_stage = EStage::ready;
        return true;
    }
    if (!m_profile_stage)
    {
        m_profile_slot = m_profiles.emplace();
        if (m_profile_slot < 0)
        {
            return fail(EConfigurationError::allocation);
        }
        SInputInstallation& output = *m_profiles.get_object(m_profile_slot);
        const SConfigurationRow& row = profiles.sorted(m_profile_cursor);
        output.profile = row.values[0];
        output.kind = row.values[1] ? input::EConnection::controller : input::EConnection::desktop;
        std::memset(m_profile_actions, 0, sizeof(m_profile_actions));
        m_cursor = 0u;
        m_profile_stage = 1u;
        return true;
    }
    SInputInstallation& output = *m_profiles.get_object(m_profile_slot);
    if (m_profile_stage == 1u)
    {
        const SConfigurationTable& contexts = table(Table::context);
        if (m_cursor < contexts.rows.size())
        {
            if (!output.contexts.push_back(contexts.sorted(m_cursor++).values[0]))
            {
                return fail(EConfigurationError::allocation);
            }
            return true;
        }
        m_cursor = 0u;
        m_profile_stage = 2u;
    }
    const SConfigurationTable& bindings = table(Table::binding);
    if ((m_profile_binding < bindings.rows.size()) && (binding_at(m_profile_binding).values[1] == output.profile))
    {
        const SConfigurationRow& binding = binding_at(m_profile_binding++);
        const SConfigurationTable& actions = table(Table::action);
        std::uint32_t first = 0u, end = actions.rows.size();
        while (first < end)
        {
            const std::uint32_t middle = first + ((end - first) / 2u);
            if (actions.sorted(middle).values[0] < binding.values[3]) first = middle + 1u;
            else end = middle;
        }
        if (!m_profile_actions[first])
        {
            if (!output.subjects.push_back(binding.values[3]))
            {
                return fail(EConfigurationError::allocation);
            }
            m_profile_actions[first] = true;
        }
        return true;
    }
    ++m_profile_cursor;
    m_profile_stage = 0u;
    return true;
}

std::uint32_t CInputConfiguration::step_cost() const noexcept
{
    if (m_copy_stage) return 8u;
    //  Conservative check credits include binary lookups, field comparisons and
    //  bounded table transitions. They are charged before a step starts.
    switch (m_stage)
    {
        case EStage::enumerations: return m_cursor ? 16u : 256u;
        case EStage::layouts: return 128u;
        case EStage::references: return 128u;
        case EStage::profiles: return 32u;
        case EStage::conflicts: return 32u;
        case EStage::instances:
        case EStage::inventory:
        case EStage::bulk: return 32u;
        default: return 8u;
    }
}

std::uint32_t CInputConfiguration::step_bytes() const noexcept
{
    if (m_copy_stage)
    {
        const std::uint32_t count = (m_copy_stage < 3u) ? m_tables[m_table].order[m_copy_stage - 1u].size() :
            m_binding_order[m_copy_stage - 3u].size();
        return (count % 128u) ? 256u : 32768u;
    }
    schema::CTypesIndex type;
    if ((m_stage == EStage::instances) && (m_cursor < m_view.instance_count())) type = m_view.instance_at(m_cursor)->type;
    if ((m_stage == EStage::bulk) && (m_cursor < m_view.bulk_count())) type = m_view.bulk_at(m_cursor)->type;
    if (type)
    {
        for (const SConfigurationTable& table : m_tables)
        {
            if (table.type == type) return (table.rows.size() % 128u) ? 256u : 32768u;
        }
    }
    //  A record-page growth touches <=20 KiB of records plus <=12 KiB of
    //  directory/slot metadata at the 256-page cap. Charge that full bound.
    return (m_stage == EStage::profiles) ? 8192u : 64u;
}

EConfigurationBuild CInputConfiguration::step() noexcept
{
    if (m_copy_stage)
    {
        const bool first = (m_copy_stage == 1u) || (m_copy_stage == 3u);
        TConfigurationPages<std::uint32_t>& order = (m_copy_stage < 3u) ? m_tables[m_table].order[m_copy_stage - 1u] :
            m_binding_order[m_copy_stage - 3u];
        if (!order.push(first ? m_copy_index : 0u))
        {
            (void)fail(EConfigurationError::allocation);
            m_copy_stage = 0u;
            return EConfigurationBuild::failed;
        }
        if ((++m_copy_stage == 5u) || ((m_copy_stage == 3u) && (m_table != static_cast<std::uint32_t>(Table::binding))))
        {
            m_copy_stage = 0u;
        }
        return EConfigurationBuild::building;
    }
    switch (m_stage)
    {
        case EStage::enumerations:
        {
            if (!enums(*m_view.types(), m_cursor++)) (void)fail(EConfigurationError::layout);
            else if (m_cursor == 137u)
            {
                m_cursor = 0u;
                m_stage = EStage::layouts;
            }
            break;
        }
        case EStage::layouts:
        {
            (void)layout();
            break;
        }
        case EStage::inventory:
        {
            (void)inventory_step();
            break;
        }
        case EStage::instances:
        {
            if (m_cursor < m_view.instance_count())
            {
                const schema::SPreparedInstance& value = *m_view.instance_at(m_cursor++);
                (void)copy(value.type, value.name, value.bytes.data(), value.bytes.size(), false);
            }
            else
            {
                m_cursor = m_row = 0u;
                m_stage = EStage::bulk;
            }
            break;
        }
        case EStage::bulk:
        {
            if (m_cursor < m_view.bulk_count())
            {
                const schema::SPreparedBulk& value = *m_view.bulk_at(m_cursor);
                if (m_row < value.count)
                {
                    (void)copy(value.type, value.name, (value.bytes.data() + (m_row * value.stride)),
                        static_cast<std::size_t>(value.stride), true);
                    ++m_row;
                }
                else
                {
                    ++m_cursor;
                    m_row = 0u;
                }
            }
            else if (m_tables[0].rows.size() != 1u) (void)fail(EConfigurationError::version);
            else
            {
                m_table = m_cursor = 0u;
                m_stage = EStage::sorting;
            }
            break;
        }
        case EStage::sorting:
        {
            (void)sort_step();
            break;
        }
        case EStage::duplicates:
        {
            if (m_table == 12u)
            {
                m_table = m_cursor = 0u;
                m_stage = EStage::references;
            }
            else if (++m_cursor < m_tables[m_table].rows.size())
            {
                if (key(m_tables[m_table].sorted(m_cursor - 1u), m_table) == key(m_tables[m_table].sorted(m_cursor), m_table))
                {
                    (void)fail(EConfigurationError::duplicate, m_tables[m_table].sorted(m_cursor).values[0]);
                }
            }
            else
            {
                ++m_table;
                m_cursor = 0u;
            }
            break;
        }
        case EStage::references:
        {
            (void)reference_step();
            break;
        }
        case EStage::profiles:
        {
            (void)profile_step();
            break;
        }
        case EStage::grouping:
        {
            (void)group_step();
            break;
        }
        case EStage::conflicts:
        {
            if (m_cursor >= table(Table::binding).rows.size())
            {
                m_cursor = 0u;
                m_stage = EStage::profiles;
                break;
            }
            const SConfigurationRow& previous = binding_at(m_cursor - 1u);
            const SConfigurationRow& current = binding_at(m_cursor++);
            if ((previous.values[1] == current.values[1]) && (previous.values[2] == current.values[2]) &&
                (previous.values[3] == current.values[3]))
            {
                const std::uint32_t kind = find(Table::action, current.values[3])->values[1];
                if ((kind == 2u) || (kind == 3u))
                {
                    (void)fail(EConfigurationError::combination, current.values[0]);
                }
            }
            break;
        }
        default:
        {
            break;
        }
    }
    return (m_stage == EStage::ready) ? EConfigurationBuild::ready :
        ((m_stage == EStage::failed) ? EConfigurationBuild::failed : EConfigurationBuild::building);
}

void CInputConfiguration::publish(const input::SInstallationToken token) noexcept
{
    for (std::int32_t slot = m_profiles.first_live(); slot >= 0; slot = m_profiles.next_live(slot))
    {
        m_profiles.get_object(slot)->token = token;
    }
}

const SInputInstallation* CInputConfiguration::profile(const std::uint32_t id, const input::EConnection kind) const noexcept
{
    for (std::int32_t slot = m_profiles.first_live(); slot >= 0; slot = m_profiles.next_live(slot))
    {
        const SInputInstallation* const value = m_profiles.get_object(slot);
        if ((value->profile == id) && (value->kind == kind))
        {
            return value;
        }
    }
    return nullptr;
}

bool CInputConfiguration::clear_step() noexcept
{
    detach();
    if (m_cleanup < 36u)
    {
        SConfigurationTable& table = m_tables[m_cleanup / 3u];
        const std::uint32_t part = m_cleanup % 3u;
        if (part ? table.order[part - 1u].clear_page() : table.rows.clear_page()) ++m_cleanup;
        return false;
    }
    if (m_cleanup < 38u)
    {
        if (m_binding_order[m_cleanup - 36u].clear_page()) ++m_cleanup;
        return false;
    }
    const std::int32_t slot = m_profiles.first_live();
    if (slot >= 0)
    {
        (void)m_profiles.erase(slot);
        return false;
    }
    m_profiles.deallocate();
    return true;
}

}   // namespace platform::runtime
