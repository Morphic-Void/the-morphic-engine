
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
    { "ActionDefinition", 2u, 8u, k_configuration_max_actions, false, {{"id", "u32", 0u}, {"kind", "ActionKind", 4u}} },
    { "ContextDefinition", 1u, 4u, 128u, false, {{"id", "u32", 0u}} },
    { "ContextAction", 2u, 8u, k_configuration_max_relationships, true, {{"context", "u32", 0u}, {"action", "u32", 4u}} },
    { "BindingProfile", 2u, 8u, 32u, false, {{"id", "u32", 0u}, {"connection_kind", "ConnectionKind", 4u}} },
    { "BindingDefinition", 10u, 32u, 8192u, false, {{"id", "u32", 0u}, {"profile", "u32", 4u},
        {"context", "u32", 8u}, {"action", "u32", 12u}, {"source", "u32", 16u}, {"parameters", "u32", 20u},
        {"activation", "ActivationKind", 24u}, {"allow_during_text", "b8", 28u}, {"native_repeat", "b8", 29u}, {"reserved", "u16", 30u}} },
    { "InputSourceDefinition", 4u, 16u, 8192u, false, {{"id", "u32", 0u}, {"kind", "SourceKind", 4u},
        {"control", "ControlCode", 8u}, {"direction", "Direction", 12u}} },
    { "BindingModifier", 2u, 8u, k_configuration_max_relationships, true, {{"binding", "u32", 0u}, {"source", "u32", 4u}} },
    { "AnalogParameters", 7u, 28u, 8192u, false, {{"id", "u32", 0u}, {"dead_zone", "f32", 4u},
        {"neutral_enter", "f32", 8u}, {"neutral_leave", "f32", 12u}, {"press", "f32", 16u},
        {"release", "f32", 20u}, {"scale", "f32", 24u}} },
    { "WindowPolicy", 1u, 4u, 32u, false, {{"id", "u32", 0u}} },
    { "ShortcutDefinition", 4u, 16u, 8192u, false, {{"id", "u32", 0u}, {"policy", "u32", 4u},
        {"primary_source", "u32", 8u}, {"operation", "ShortcutOperation", 12u}} },
    { "ShortcutModifier", 2u, 8u, k_configuration_max_relationships, true, {{"shortcut", "u32", 0u}, {"source", "u32", 4u}} }
};
static_assert((sizeof(k_tables) / sizeof(k_tables[0])) == k_configuration_table_count);

//  Decode the copied schema rows at this boundary. Validation and plan building
//  use named fields rather than repeating the schema's word offsets.
struct SBindingDefinition
{
    explicit SBindingDefinition(const SConfigurationRow& row) noexcept :
        id{ row.values[0] }, profile{ row.values[1] }, context{ row.values[2] }, action{ row.values[3] },
        source{ row.values[4] }, parameters{ row.values[5] },
        activation{ static_cast<EInputActivation>(row.values[6]) }, flags{ row.values[7] } {}

    static constexpr std::uint32_t k_allow_text = 0x00000001u, k_native_repeat = 0x00000100u;
    std::uint32_t id, profile, context, action, source, parameters;
    EInputActivation activation;
    std::uint32_t flags;
    [[nodiscard]] bool valid_flags() const noexcept { return !(flags & ~(k_allow_text | k_native_repeat)); }
    [[nodiscard]] bool allow_text() const noexcept { return (flags & k_allow_text) != 0u; }
    [[nodiscard]] bool native_repeat() const noexcept { return (flags & k_native_repeat) != 0u; }
};

struct SSourceDefinition
{
    explicit SSourceDefinition(const SConfigurationRow& row) noexcept : id{ row.values[0] },
        kind{ static_cast<EInputSource>(row.values[1]) }, control{ row.values[2] },
        direction{ static_cast<EInputDirection>(row.values[3]) } {}

    std::uint32_t id;
    EInputSource kind;
    std::uint32_t control;
    EInputDirection direction;
    [[nodiscard]] bool digital() const noexcept
    { return (kind == EInputSource::key) || (kind == EInputSource::button) || (kind == EInputSource::pad_button); }
    [[nodiscard]] bool controller() const noexcept { return kind >= EInputSource::pad_button; }
    [[nodiscard]] bool projected() const noexcept { return direction != EInputDirection::whole; }
};

struct SActionDefinition
{
    explicit SActionDefinition(const SConfigurationRow& row) noexcept :
        id{ row.values[0] }, kind{ static_cast<EInputAction>(row.values[1]) } {}
    std::uint32_t id;
    EInputAction kind;
};

struct SProfileDefinition
{
    explicit SProfileDefinition(const SConfigurationRow& row) noexcept :
        id{ row.values[0] }, kind{ static_cast<EInputProfileKind>(row.values[1]) } {}
    std::uint32_t id;
    EInputProfileKind kind;
};

enum class EShortcutOperation : std::uint32_t { toggle_windowed = 0u, toggle_exclusive = 1u };
struct SShortcutDefinition
{
    explicit SShortcutDefinition(const SConfigurationRow& row) noexcept :
        id{ row.values[0] }, policy{ row.values[1] }, primary_source{ row.values[2] },
        operation{ static_cast<EShortcutOperation>(row.values[3]) } {}
    std::uint32_t id, policy, primary_source;
    EShortcutOperation operation;
};

static SInputParameters analog_parameters(const SConfigurationRow& row) noexcept
{
    SInputParameters parameters;
    //  The six parameter floats follow the definition ID in schema v1.
    std::memcpy(&parameters, (row.values + 1u), sizeof(parameters));
    return parameters;
}

static bool valid_source_control(const SSourceDefinition& source) noexcept
{
    using namespace input_control;
    switch (source.kind)
    {
        case EInputSource::key: return is_key(source.control);
        case EInputSource::button: return is_mouse_button(source.control);
        case EInputSource::position: return source.control == k_mouse_position;
        case EInputSource::wheel: return source.control == k_mouse_wheel;
        case EInputSource::pad_button: return is_pad_button(source.control);
        case EInputSource::stick: return (source.control == k_left_stick) || (source.control == k_right_stick);
        case EInputSource::trigger: return (source.control == k_left_trigger) || (source.control == k_right_trigger);
        default: return false;
    }
}

static bool valid_direct_action(const EInputAction action, const SSourceDefinition& source) noexcept
{
    if (source.digital()) return false;
    switch (action)
    {
        case EInputAction::scalar:
        {
            return (source.kind != EInputSource::wheel) && ((source.kind == EInputSource::trigger) || source.projected());
        }
        case EInputAction::vector:
        {
            return (source.kind == EInputSource::stick) && !source.projected();
        }
        case EInputAction::delta:
        {
            return source.kind == EInputSource::wheel;
        }
        default:
        {
            return false;
        }
    }
}

static bool valid_activation(const EInputActivation activation, const EInputAction action,
    const SSourceDefinition& source, const bool modifiers) noexcept
{
    switch (activation)
    {
        case EInputActivation::regular:
        {
            return (action == EInputAction::digital) && source.digital() && !modifiers;
        }
        case EInputActivation::chord:
        {
            return (action == EInputAction::digital) && source.digital() && modifiers;
        }
        case EInputActivation::start:
        case EInputActivation::release:
        {
            return (action == EInputAction::pulse) && source.digital();
        }
        case EInputActivation::threshold:
        {
            const bool normalized = (source.kind == EInputSource::trigger) ||
                ((source.kind == EInputSource::stick) && source.projected());
            return (action == EInputAction::digital) && normalized && !modifiers;
        }
        case EInputActivation::direct:
        {
            return !modifiers && valid_direct_action(action, source);
        }
        default:
        {
            return false;
        }
    }
}

static bool equal(const CStringView value, const char* const expected) noexcept
{
    const std::size_t size = std::strlen(expected);
    return (value.length() == size) && (!size || (std::memcmp(value.cstring(), expected, size) == 0));
}

static bool enumeration(const schema::CResolvedTypes& types, const char* const name, const char* const* const labels, const std::uint32_t count) noexcept
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
    using namespace input_control;
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
    else if (code <= k_last_letter)
    {
        std::memcpy(name, "key_a", 6u);
        name[4] = static_cast<char>('a' + (code - 1u));
        return true;
    }
    else if (code <= k_last_digit)
    {
        std::memcpy(name, "digit_0", 8u);
        name[6] = static_cast<char>('0' + ((code - k_last_letter) % 10u));
        return true;
    }
    else if (code <= k_last_key) text = keys[code - k_first_named_key];
    else if ((code >= k_first_mouse_button) && (code <= k_mouse_wheel)) text = mouse[code - k_first_mouse_button];
    else if (is_controller(code)) text = pad[code - k_first_pad_button];
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
        (layout.size != 4u) || (layout.count != input_control::k_defined_count) ||
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

bool CInputConfiguration::fail(const EConfigurationError reason, const std::uint32_t definition, const std::uint32_t field) noexcept
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
    if (++m_table == k_configuration_table_count)
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
    while ((m_table < k_configuration_table_count) && (m_tables[m_table].type != type)) ++m_table;
    if (m_table == k_configuration_table_count)
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
        (bulk && (count > (k_configuration_max_relationships - m_total_relationships))))
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
    while ((m_table < k_configuration_table_count) && (m_tables[m_table].type != type)) ++m_table;
    if (m_table == k_configuration_table_count)
    {
        return fail(EConfigurationError::layout);
    }
    const STableContract& expected = k_tables[m_table];
    SConfigurationTable& table = m_tables[m_table];
    if ((bulk != expected.bulk) || (size != expected.size) || !bytes)
    {
        return fail(EConfigurationError::layout);
    }
    if ((name.length() > 127u) || (table.rows.size() >= expected.maximum) || (bulk && (++m_total_relationships > k_configuration_max_relationships)))
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
    if (m_table == k_configuration_table_count)
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
        m_width <<= 1u;
        m_begin = m_end = m_output = 0u;
        return true;
    }
    if (m_output == m_end)
    {
        m_left = m_begin;
        m_right = std::min((m_begin + m_width), count);
        m_end = std::min((m_begin + (m_width << 1u)), count);
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
        const std::uint32_t middle = begin + ((end - begin) >> 1u);
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
        const std::uint32_t middle = begin + ((end - begin) >> 1u);
        if (key(data.sorted(middle), static_cast<std::uint32_t>(type)) < target) begin = middle + 1u;
        else end = middle;
    }
    return (begin < data.rows.size()) && (key(data.sorted(begin), static_cast<std::uint32_t>(type)) == target);
}

bool CInputConfiguration::reference_step() noexcept
{
    if (m_table == k_configuration_table_count)
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
            const SActionDefinition action{ row };
            if (action.kind > EInputAction::delta)
            {
                return fail(EConfigurationError::value, action.id);
            }
            break;
        }
        case Table::profile:
        {
            const SProfileDefinition profile{ row };
            if (profile.kind > EInputProfileKind::controller)
            {
                return fail(EConfigurationError::value, profile.id);
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
            const SSourceDefinition source{ row };
            const bool whole_only = source.digital() || (source.kind == EInputSource::trigger);
            if (!valid_source_control(source) || (source.direction > EInputDirection::negative_y) ||
                (whole_only && source.projected()))
            {
                return fail(EConfigurationError::value, source.id);
            }
            break;
        }
        case Table::analog:
        {
            const SInputParameters parameters = analog_parameters(row);
            for (const float value : { parameters.dead_zone, parameters.neutral_enter, parameters.neutral_leave,
                parameters.press, parameters.release, parameters.scale })
            {
                if (!std::isfinite(value))
                {
                    return fail(EConfigurationError::value, v[0]);
                }
            }
            if ((parameters.dead_zone < 0.0f) || (parameters.dead_zone >= 1.0f) ||
                (parameters.neutral_enter < 0.0f) || (parameters.neutral_enter >= parameters.neutral_leave) ||
                (parameters.neutral_leave > 1.0f) || (parameters.release < 0.0f) ||
                (parameters.release >= parameters.press) || (parameters.press > 1.0f))
            {
                return fail(EConfigurationError::value, v[0]);
            }
            break;
        }
        case Table::binding:
        {
            const SBindingDefinition binding{ row };
            const SConfigurationRow* const profile_row = find(Table::profile, binding.profile);
            const SConfigurationRow* const action_row = find(Table::action, binding.action);
            const SConfigurationRow* const source_row = find(Table::source, binding.source);
            if (!profile_row || !action_row || !source_row || !contains(Table::membership, binding.context, binding.action) ||
                (binding.parameters && !find(Table::analog, binding.parameters)))
            {
                return fail(EConfigurationError::reference, binding.id);
            }
            const SProfileDefinition profile{ *profile_row };
            const SActionDefinition action{ *action_row };
            const SSourceDefinition source{ *source_row };
            const EInputProfileKind required_profile = source.controller() ? EInputProfileKind::controller : EInputProfileKind::desktop;
            if ((profile.kind != required_profile) || !binding.valid_flags())
            {
                return fail(EConfigurationError::combination, binding.id);
            }
            const bool modifiers = find(Table::modifier, binding.id) != nullptr;
            if (!valid_activation(binding.activation, action.kind, source, modifiers))
            {
                return fail(EConfigurationError::combination, binding.id);
            }
            if (binding.native_repeat())
            {
                if ((source.kind != EInputSource::key) || (binding.activation != EInputActivation::start) ||
                    !input_control::permits_editing_repeat(source.control))
                {
                    return fail(EConfigurationError::combination, binding.id);
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
            const std::uint32_t primary_id = shortcut ? SShortcutDefinition{ *target }.primary_source : SBindingDefinition{ *target }.source;
            const SConfigurationRow* const primary = find(Table::source, primary_id);
            const SSourceDefinition modifier{ *source };
            if (!primary || !modifier.digital() ||
                (SSourceDefinition{ *primary }.controller() != modifier.controller()) || (modifier.id == primary_id))
            {
                return fail(EConfigurationError::combination, v[0]);
            }
            break;
        }
        case Table::shortcut:
        {
            const SShortcutDefinition shortcut{ row };
            const SConfigurationRow* const source = find(Table::source, shortcut.primary_source);
            if (!find(Table::policy, shortcut.policy) || !source)
            {
                return fail(EConfigurationError::reference, shortcut.id);
            }
            if ((SSourceDefinition{ *source }.kind > EInputSource::button) || (shortcut.operation > EShortcutOperation::toggle_exclusive))
            {
                return fail(EConfigurationError::combination, shortcut.id);
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
    const SBindingDefinition first{ left }, second{ right };
    if (first.profile != second.profile)
    {
        return first.profile < second.profile;
    }
    if (first.context != second.context)
    {
        return first.context < second.context;
    }
    if (first.action != second.action)
    {
        return first.action < second.action;
    }
    return first.id < second.id;
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
        m_width <<= 1u;
        m_begin = m_end = m_output = 0u;
        return true;
    }
    if (m_output == m_end)
    {
        m_left = m_begin;
        m_right = std::min((m_begin + m_width), count);
        m_end = std::min((m_begin + (m_width << 1u)), count);
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
        const SProfileDefinition profile{ profiles.sorted(m_profile_cursor) };
        output.profile = profile.id;
        output.plan = &m_plan;
        output.binding_begin = m_profile_binding;
        output.kind = (profile.kind == EInputProfileKind::controller) ? input::EConnection::controller : input::EConnection::desktop;
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
    if ((m_profile_binding < bindings.rows.size()) && (SBindingDefinition{ binding_at(m_profile_binding) }.profile == output.profile))
    {
        const SBindingDefinition binding{ binding_at(m_profile_binding++) };
        ++output.binding_count;
        const SConfigurationTable& actions = table(Table::action);
        std::uint32_t first = 0u, end = actions.rows.size();
        while (first < end)
        {
            const std::uint32_t middle = first + ((end - first) >> 1u);
            if (actions.sorted(middle).values[0] < binding.action) first = middle + 1u;
            else end = middle;
        }
        if (!m_profile_actions[first])
        {
            if (!output.subjects.push_back(binding.action))
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

bool CInputConfiguration::plan_step() noexcept
{
    const SConfigurationTable& modifiers = table(Table::modifier);
    if (m_plan_modifiers)
    {
        if (m_cursor == modifiers.rows.size())
        {
            m_cursor = 0u;
            m_plan_modifiers = false;
            return true;
        }
        const SConfigurationRow& modifier = modifiers.sorted(m_cursor++);
        const SSourceDefinition source{ *find(Table::source, modifier.values[1]) };
        return m_plan.modifiers.push(source.control) ||
            fail(EConfigurationError::allocation);
    }
    if (m_cursor == table(Table::binding).rows.size())
    {
        m_cursor = 0u;
        m_stage = EStage::profiles;
        return true;
    }
    const SBindingDefinition definition{ binding_at(m_cursor++) };
    const SSourceDefinition source{ *find(Table::source, definition.source) };
    SInputBinding binding;
    binding.id = definition.id;
    binding.context = definition.context;
    binding.action = definition.action;
    binding.action_kind = SActionDefinition{ *find(Table::action, definition.action) }.kind;
    binding.control = source.control;
    binding.direction = source.direction;
    binding.source = source.kind;
    binding.activation = definition.activation;
    binding.allow_text = definition.allow_text();
    binding.repeat = definition.native_repeat();
    if (definition.parameters)
    {
        binding.parameters = analog_parameters(*find(Table::analog, definition.parameters));
    }
    else if (binding.source == EInputSource::stick)
    {
        binding.parameters.dead_zone = 0.15f;
        binding.parameters.neutral_enter = 0.12f;
        binding.parameters.neutral_leave = 0.15f;
    }
    else if (binding.source == EInputSource::trigger)
    {
        binding.parameters.dead_zone = 0.05f;
        binding.parameters.neutral_enter = 0.03f;
        binding.parameters.neutral_leave = 0.05f;
    }
    //  Resolve both ends independently; modifier counts cannot make one step unbounded.
    for (const bool upper : { false, true })
    {
        std::uint32_t first = 0u, end = modifiers.rows.size();
        while (first < end)
        {
            const std::uint32_t middle = first + ((end - first) >> 1u);
            const std::uint32_t id = modifiers.sorted(middle).values[0];
            if ((id < definition.id) || (upper && (id == definition.id))) first = middle + 1u;
            else end = middle;
        }
        if (upper) binding.modifier_end = first;
        else binding.modifier_begin = first;
    }
    return m_plan.bindings.push(binding) || fail(EConfigurationError::allocation);
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
        case EStage::plan: return 128u;
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
        return (count & k_input_page_mask) ? 256u : 32768u;
    }
    schema::CTypesIndex type;
    if ((m_stage == EStage::instances) && (m_cursor < m_view.instance_count())) type = m_view.instance_at(m_cursor)->type;
    if ((m_stage == EStage::bulk) && (m_cursor < m_view.bulk_count())) type = m_view.bulk_at(m_cursor)->type;
    if (type)
    {
        for (const SConfigurationTable& table : m_tables)
        {
            if (table.type == type) return (table.rows.size() & k_input_page_mask) ? 256u : 32768u;
        }
    }
    //  A record-page growth touches <=20 KiB of records plus <=12 KiB of
    //  directory/slot metadata at the 256-page cap. Charge that full bound.
    if (m_stage == EStage::plan)
    {
        const std::uint32_t count = m_plan_modifiers ? m_plan.modifiers.size() : m_plan.bindings.size();
        return (count & k_input_page_mask) ? 256u : 32768u;
    }
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
            else if (m_cursor == (input_control::k_defined_count + 1u))
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
            if (m_table == k_configuration_table_count)
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
        case EStage::plan:
        {
            (void)plan_step();
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
                m_stage = EStage::plan;
                break;
            }
            const SBindingDefinition previous{ binding_at(m_cursor - 1u) };
            const SBindingDefinition current{ binding_at(m_cursor++) };
            if ((previous.profile == current.profile) && (previous.context == current.context) &&
                (previous.action == current.action))
            {
                const EInputAction kind = SActionDefinition{ *find(Table::action, current.action) }.kind;
                if ((kind == EInputAction::scalar) || (kind == EInputAction::vector))
                {
                    (void)fail(EConfigurationError::combination, current.id);
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
    //  Each table owns rows and two sort buffers; the binding sort owns two more.
    const std::uint32_t stores_per_table = 3u;
    const std::uint32_t table_stores = k_configuration_table_count * stores_per_table;
    const std::uint32_t plan_bindings = table_stores + 2u;
    const std::uint32_t plan_modifiers = plan_bindings + 1u;
    detach();
    if (m_cleanup < table_stores)
    {
        SConfigurationTable& table = m_tables[m_cleanup / stores_per_table];
        const std::uint32_t part = m_cleanup % stores_per_table;
        if (part ? table.order[part - 1u].clear_page() : table.rows.clear_page()) ++m_cleanup;
        return false;
    }
    if (m_cleanup < plan_bindings)
    {
        if (m_binding_order[m_cleanup - table_stores].clear_page()) ++m_cleanup;
        return false;
    }
    if (m_cleanup == plan_bindings)
    {
        if (m_plan.bindings.clear_page()) ++m_cleanup;
        return false;
    }
    if (m_cleanup == plan_modifiers)
    {
        if (m_plan.modifiers.clear_page()) ++m_cleanup;
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
