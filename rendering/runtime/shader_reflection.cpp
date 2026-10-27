
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_reflection.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Oct 26
//
//  Bounded reflection staging and transactional publication. Physical resource
//  layouts and non-storage stage-value shapes remain separate descriptions.

#include "rendering/runtime/shader_reflection.hpp"
#include "schema/schema.hpp"
#include "text/utf8_string.hpp"
#include <charconv>
#include <new>

#define SPV_ENABLE_UTILITY_CODE
#include "rendering/runtime/shader_reflection_spirv.hpp"

namespace rendering::shader_reflection
{

CResult::CResult(CResult&& source) noexcept
{
    *this = std::move(source);
}

CResult& CResult::operator=(CResult&& source) noexcept
{
    if (this != &source)
    {
        clear();
        m_schema = std::move(source.m_schema);
        m_identity = std::move(source.m_identity);
        m_diagnostics = std::move(source.m_diagnostics);
        m_status = std::exchange(source.m_status, EStatus::not_executed);
        m_diagnostic = std::exchange(source.m_diagnostic, {});
        m_statistics = std::exchange(source.m_statistics, {});
        m_truncated = std::exchange(source.m_truncated, false);
    }
    return *this;
}

void CResult::clear() noexcept
{
    m_schema.clear();
    m_identity.deallocate();
    m_diagnostics.deallocate();
    m_status = EStatus::not_executed;
    m_diagnostic = {};
    m_statistics = {};
    m_truncated = false;
}

CStringView CResult::diagnostics() const noexcept
{
    return m_diagnostics.view();
}

CStringView CResult::component_identity() const noexcept
{
    return m_identity.view();
}

CStringView CResult::root_name() const noexcept
{
    return (m_status == EStatus::success) ? CStringView{ "component" } : CStringView{};
}

const schema::CSchema* CResult::schema() const noexcept
{
    return (m_status == EStatus::success) ? &m_schema : nullptr;
}

static bool valid_identity(const CStringView value) noexcept
{
    std::size_t measured = 0u;
    return (value.length() != 0u) &&
        utf8_string::validate_and_measure(value.string(), value.length(), utf8_string::ELiteralNulPolicy::reject, measured) &&
        (std::memchr(value.string(), 0xc0, value.length()) == nullptr);
}

struct SNode
{
    std::uint32_t offset;
    std::uint32_t edge;
    std::uint32_t expanded;
    std::uint32_t height;
    std::uint32_t rank;
    std::uint32_t annotation;
    std::uint16_t opcode;
    std::uint16_t words;
    std::uint8_t colour;
};

struct SEdge
{
    std::uint32_t target;
    std::uint32_t next;
};

struct SAnnotation
{
    std::uint32_t offset;
    std::uint32_t next;
};

class CPreflight final
{

public:
    CPreflight(const CByteConstView artifact, const SLimits& limits, SDiagnostic& diagnostic) noexcept
        : m_words{ static_cast<const std::uint32_t*>(static_cast<const void*>(artifact.data())) },
          m_count{ artifact.size() / 4u }, m_limits{ limits }, m_diagnostic{ diagnostic }
    {
    }

    [[nodiscard]] EStatus run() noexcept;
    [[nodiscard]] const std::uint32_t* words() const noexcept
    {
        return m_words;
    }

    [[nodiscard]] std::size_t count() const noexcept
    {
        return m_count;
    }

    [[nodiscard]] const std::uint32_t* instruction(const std::uint32_t id) const noexcept
    {
        return ((id < m_nodes.size()) && (m_nodes[id].offset != 0u)) ? m_words + m_nodes[id].offset : nullptr;
    }

    [[nodiscard]] std::uint32_t annotation(const std::uint32_t id) const noexcept
    {
        return (id < m_nodes.size()) ? m_nodes[id].annotation : 0u;
    }

    [[nodiscard]] const SAnnotation& annotation_at(const std::uint32_t link) const noexcept
    {
        return m_annotations[link - 1u];
    }

private:
    [[nodiscard]] bool edge(const std::uint32_t from, const std::uint32_t to) noexcept;
    [[nodiscard]] bool visit(const std::uint32_t id, const std::uint32_t depth) noexcept;
    [[nodiscard]] bool fail(const EStatus status, const EReason reason, const std::uint32_t offset) noexcept;
    const std::uint32_t* const m_words;
    const std::size_t m_count;
    const SLimits& m_limits;
    SDiagnostic& m_diagnostic;
    TPodVector<SNode> m_nodes;
    TPodVector<SEdge> m_edges;
    TPodVector<SAnnotation> m_annotations;
    EStatus m_status{ EStatus::success };
};

bool CPreflight::fail(const EStatus status, const EReason reason, const std::uint32_t offset) noexcept
{
    m_status = status;
    m_diagnostic.reason = reason;
    m_diagnostic.has_word_offset = true;
    m_diagnostic.word_offset = offset;
    return false;
}

bool CPreflight::edge(const std::uint32_t from, const std::uint32_t to) noexcept
{
    if ((to == 0u) || (to >= m_nodes.size()))
    {
        return fail(EStatus::invalid_input, EReason::malformed_artifact, m_nodes[from].offset);
    }
    if (m_edges.size() >= m_limits.document_nodes)
    {
        return fail(EStatus::limit_exceeded, EReason::dependency_limit, m_nodes[from].offset);
    }
    if (!m_edges.push_back({ to, m_nodes[from].edge }))
    {
        return fail(EStatus::allocation_failed, EReason::allocation, m_nodes[from].offset);
    }
    m_nodes[from].edge = static_cast<std::uint32_t>(m_edges.size());
    return true;
}

bool CPreflight::visit(const std::uint32_t id, const std::uint32_t depth) noexcept
{
    auto& node = m_nodes[id];
    const auto ceiling = std::min(m_limits.nesting, 32u);
    if (depth > ceiling)
    {
        return fail(EStatus::limit_exceeded, EReason::nesting_limit, node.offset);
    }
    if ((node.offset == 0u) || (node.colour == 1u))
    {
        return fail(EStatus::invalid_input, EReason::malformed_artifact, node.offset);
    }
    if (node.colour == 2u)
    {
        return ((node.height <= ceiling) && (depth <= (ceiling - node.height + 1u))) ||
            fail(EStatus::limit_exceeded, EReason::nesting_limit, node.offset);
    }
    node.colour = 1u;
    node.height = node.expanded = 1u;
    const bool array = (node.opcode == SpvOpTypeArray) || (node.opcode == SpvOpTypeRuntimeArray);
    node.rank = array ? 1u : 0u;
    for (auto item = node.edge; item != 0u; item = m_edges[item - 1u].next)
    {
        const auto target = m_edges[item - 1u].target;
        if (!visit(target, depth + 1u))
        {
            return false;
        }
        const auto& child = m_nodes[target];
        node.height = std::max(node.height, child.height + 1u);
        if ((node.expanded > m_limits.document_nodes) || (child.expanded > (m_limits.document_nodes - node.expanded)))
        {
            return fail(EStatus::limit_exceeded, EReason::dependency_limit, node.offset);
        }
        node.expanded += child.expanded;
        if (array)
        {
            node.rank = child.rank + 1u;
        }
    }
    if (node.rank > m_limits.array_rank)
    {
        return fail(EStatus::limit_exceeded, EReason::nesting_limit, node.offset);
    }
    node.colour = 2u;
    return true;
}

EStatus CPreflight::run() noexcept
{
    if ((m_count < 5u) || (m_words[0] != SpvMagicNumber) || (m_words[3] == 0u) || (m_words[4] != 0u))
    {
        (void)fail(EStatus::invalid_input, EReason::malformed_artifact, 0u);
        return m_status;
    }
    if (m_words[3] > m_limits.id_bound)
    {
        (void)fail(EStatus::limit_exceeded, EReason::id_limit, 3u);
        return m_status;
    }
    if (m_nodes.push_back_zeroed(m_words[3]) == nullptr)
    {
        return EStatus::allocation_failed;
    }
    std::uint32_t instructions = 0u;
    std::uint32_t function = 0u;
    std::uint32_t specialization = 0u;
    std::uint32_t resources = 0u;
    std::uint32_t interfaces = 0u;
    for (std::size_t offset = 5u; offset < m_count;)
    {
        const auto words = m_words[offset] >> 16u;
        const auto opcode = static_cast<SpvOp>(m_words[offset] & 0xffffu);
        const auto position = static_cast<std::uint32_t>(offset);
        if ((words == 0u) || (words > (m_count - offset)))
        {
            (void)fail(EStatus::invalid_input, EReason::malformed_artifact, position);
            return m_status;
        }
        if (++instructions > m_limits.instructions)
        {
            (void)fail(EStatus::limit_exceeded, EReason::instruction_limit, position);
            return m_status;
        }
        bool result = false;
        bool type = false;
        SpvHasResultAndType(opcode, &result, &type);
        std::uint32_t id = 0u;
        if (result)
        {
            const auto index = type ? 2u : 1u;
            if ((words <= index) || ((id = m_words[offset + index]) == 0u) || (id >= m_nodes.size()) || (m_nodes[id].offset != 0u))
            {
                (void)fail(EStatus::invalid_input, EReason::malformed_artifact, position);
                return m_status;
            }
            m_nodes[id].offset = position;
            m_nodes[id].opcode = static_cast<std::uint16_t>(opcode);
            m_nodes[id].words = static_cast<std::uint16_t>(words);
        }
        std::uint32_t start = 0u;
        std::uint32_t end = 0u;
        switch (opcode)
        {
            case (SpvOpName):
            case (SpvOpMemberName):
            case (SpvOpDecorate):
            case (SpvOpMemberDecorate):
            case (SpvOpDecorateStringGOOGLE):
            case (SpvOpMemberDecorateStringGOOGLE):
            {
                const bool member = (opcode == SpvOpMemberName) || (opcode == SpvOpMemberDecorate) ||
                    (opcode == SpvOpMemberDecorateStringGOOGLE);
                const auto minimum = member ? 4u : 3u;
                if ((words < minimum) || (m_words[offset + 1u] == 0u) || (m_words[offset + 1u] >= m_nodes.size()))
                {
                    (void)fail(EStatus::invalid_input, EReason::malformed_artifact, position);
                    return m_status;
                }
                auto& target = m_nodes[m_words[offset + 1u]];
                if (!m_annotations.push_back({ position, target.annotation }))
                {
                    return EStatus::allocation_failed;
                }
                target.annotation = static_cast<std::uint32_t>(m_annotations.size());
                break;
            }
            case (SpvOpDecorationGroup):
            case (SpvOpGroupDecorate):
            case (SpvOpGroupMemberDecorate):
            case (SpvOpDecorateId):
            {
                (void)fail(EStatus::unsupported_feature, EReason::unsupported_type, position);
                return m_status;
            }
            case (SpvOpTypeVector):
            case (SpvOpTypeMatrix):
            case (SpvOpTypeImage):
            case (SpvOpTypeSampledImage):
            case (SpvOpTypeArray):
            case (SpvOpTypeRuntimeArray):
            {
                start = 2u;
                end = 3u;
                break;
            }
            case (SpvOpTypePointer):
            {
                start = 3u;
                end = 4u;
                break;
            }
            case (SpvOpTypeStruct):
            case (SpvOpTypeFunction):
            {
                start = 2u;
                end = words;
                if ((words - 2u) > m_limits.aggregate_members)
                {
                    (void)fail(EStatus::limit_exceeded, EReason::member_limit, position);
                    return m_status;
                }
                break;
            }
            case (SpvOpTypeForwardPointer):
            {
                (void)fail(EStatus::unsupported_feature, EReason::unsupported_type, position);
                return m_status;
            }
            case (SpvOpFunction):
            {
                function = id;
                break;
            }
            case (SpvOpFunctionEnd):
            {
                function = 0u;
                break;
            }
            case (SpvOpVariable):
            {
                if (words < 4u)
                {
                    (void)fail(EStatus::invalid_input, EReason::malformed_artifact, position);
                    return m_status;
                }
                if ((m_words[offset + 3u] == SpvStorageClassInput) || (m_words[offset + 3u] == SpvStorageClassOutput))
                {
                    ++interfaces;
                }
                if ((m_words[offset + 3u] == SpvStorageClassUniformConstant) || (m_words[offset + 3u] == SpvStorageClassUniform) ||
                    (m_words[offset + 3u] == SpvStorageClassStorageBuffer))
                {
                    ++resources;
                }
                if ((interfaces > m_limits.interface_values) || (resources > m_limits.resources))
                {
                    (void)fail(EStatus::limit_exceeded, EReason::member_limit, position);
                    return m_status;
                }
                break;
            }
            case (SpvOpFunctionCall):
            {
                if ((function == 0u) || (words < 4u))
                {
                    (void)fail(EStatus::invalid_input, EReason::malformed_artifact, position);
                    return m_status;
                }
                if (!edge(function, m_words[offset + 3u]))
                {
                    return m_status;
                }
                break;
            }
            case (SpvOpSpecConstantTrue):
            case (SpvOpSpecConstantFalse):
            case (SpvOpSpecConstant):
            case (SpvOpSpecConstantComposite):
            case (SpvOpSpecConstantOp):
            {
                if (specialization == 0u)
                {
                    specialization = position;
                }
                break;
            }
            default:
            {
                break;
            }
        }
        if (end > words)
        {
            (void)fail(EStatus::invalid_input, EReason::malformed_artifact, position);
            return m_status;
        }
        for (auto operand = start; operand < end; ++operand)
        {
            if (!edge(id, m_words[offset + operand]))
            {
                return m_status;
            }
        }
        offset += words;
    }
    if (specialization != 0u)
    {
        auto reason = EReason::unsupported_specialization;
        for (std::size_t offset = 5u; offset < m_count; offset += (m_words[offset] >> 16u))
        {
            const auto opcode = m_words[offset] & 0xffffu;
            if ((opcode == SpvOpTypeArray) && ((m_words[offset] >> 16u) >= 4u))
            {
                const auto length = m_words[offset + 3u];
                if ((length < m_nodes.size()) && (m_nodes[length].opcode >= SpvOpSpecConstantTrue) &&
                    (m_nodes[length].opcode <= SpvOpSpecConstantOp))
                {
                    reason = EReason::specialization_shape;
                }
            }
            if (opcode == SpvOpExecutionModeId)
            {
                reason = EReason::specialization_execution;
            }
        }
        (void)fail(EStatus::unsupported_feature, reason, specialization);
        return m_status;
    }

    //  Bound every type expansion, including unused/debug-referenced types,
    //  and every static function-call graph before upstream recursive passes.
    //  Repeated DAG visits are charged, not merely unique reachable nodes.
    std::uint64_t expanded = 0u;
    for (std::uint32_t id = 1u; id < m_nodes.size(); ++id)
    {
        const auto opcode = m_nodes[id].opcode;
        if (((opcode >= SpvOpTypeVoid) && (opcode <= SpvOpTypeFunction)) || (opcode == SpvOpFunction))
        {
            if (!visit(id, 1u))
            {
                return m_status;
            }
            expanded += m_nodes[id].expanded;
            if (expanded > m_limits.document_nodes)
            {
                (void)fail(EStatus::limit_exceeded, EReason::dependency_limit, m_nodes[id].offset);
                return m_status;
            }
        }
    }
    return m_status;
}

class CBuilder final
{

public:
    CBuilder(schema::CSchema& schema, const SLimits& limits, SDiagnostic& diagnostic, const std::size_t identity_bytes) noexcept
        : m_document{ schema.edit() }, m_limits{ limits }, m_diagnostic{ diagnostic }, m_strings{ identity_bytes }
    {
    }

    [[nodiscard]] CNodeKey object(const CNodeKey parent, const char* const name) noexcept;
    [[nodiscard]] CNodeKey array(const CNodeKey parent, const char* const name) noexcept;
    [[nodiscard]] bool string(const CNodeKey parent, const char* const name, const char* const value) noexcept;
    [[nodiscard]] bool number(const CNodeKey parent, const char* const name, const std::uint32_t value) noexcept;
    [[nodiscard]] bool decimal(const CNodeKey parent, const char* const name, const std::uint32_t value) noexcept;
    [[nodiscard]] bool null(const CNodeKey parent, const char* const name) noexcept;
    [[nodiscard]] bool limit() noexcept;
    [[nodiscard]] bool attach(const CNodeKey parent, const CNodeKey value) noexcept;
    [[nodiscard]] bool text(const char* const value) noexcept;
    [[nodiscard]] bool charge(const std::uint32_t declarations, const std::uint32_t members) noexcept;
    [[nodiscard]] CNodeKey group(const CNodeKey parent, const char* const name) noexcept;

private:
    CLiveDocument& m_document;
    const SLimits& m_limits;
    SDiagnostic& m_diagnostic;
    std::uint32_t m_nodes{ 1u };
    std::size_t m_strings{ 0u };
    std::uint32_t m_declarations{ 4u };
    std::uint32_t m_members{ 3u };
};

bool CBuilder::charge(const std::uint32_t declarations, const std::uint32_t members) noexcept
{
    if ((declarations > m_limits.declarations - m_declarations) || (members > m_limits.members - m_members))
    {
        m_diagnostic.reason = (declarations > m_limits.declarations - m_declarations) ? EReason::declaration_limit : EReason::member_limit;
        return false;
    }
    m_declarations += declarations;
    m_members += members;
    return true;
}

CNodeKey CBuilder::group(const CNodeKey parent, const char* const name) noexcept
{
    const auto existing = m_document.object_child(parent, CStringView{ name });
    return existing.is_valid() ? existing : object(parent, name);
}

bool CBuilder::limit() noexcept
{
    if (m_nodes >= m_limits.document_nodes)
    {
        m_diagnostic.reason = EReason::node_limit;
        return false;
    }
    ++m_nodes;
    return true;
}

bool CBuilder::text(const char* const value) noexcept
{
    const auto bytes = std::strlen(value);
    if ((bytes > m_limits.string_bytes) || (m_strings > m_limits.strings_bytes) || (bytes > (m_limits.strings_bytes - m_strings)))
    {
        m_diagnostic.reason = EReason::string_limit;
        return false;
    }
    m_strings += bytes;
    return true;
}

bool CBuilder::attach(const CNodeKey parent, const CNodeKey value) noexcept
{
    return parent.is_valid() && value.is_valid() && m_document.append_child(parent, value).succeeded();
}

CNodeKey CBuilder::object(const CNodeKey parent, const char* const name) noexcept
{
    if (!parent.is_valid() || !limit() || !text(name))
    {
        return {};
    }

    const auto value = m_document.create_object(CStringView{ name });
    return attach(parent, value) ? value : CNodeKey{};
}

CNodeKey CBuilder::array(const CNodeKey parent, const char* const name) noexcept
{
    if (!parent.is_valid() || !limit() || !text(name))
    {
        return {};
    }

    const auto value = m_document.create_array(CStringView{ name });
    return attach(parent, value) ? value : CNodeKey{};
}

bool CBuilder::string(const CNodeKey parent, const char* const name, const char* const value) noexcept
{
    return parent.is_valid() && limit() && text(name) && text(value) &&
        attach(parent, m_document.create_string(CStringView{ value }, CStringView{ name }));
}

bool CBuilder::decimal(const CNodeKey parent, const char* const name, const std::uint32_t value) noexcept
{
    char digits[16]{};
    (void)std::to_chars(digits, digits + sizeof(digits) - 1u, value);
    return string(parent, name, digits);
}

bool CBuilder::null(const CNodeKey parent, const char* const name) noexcept
{
    return parent.is_valid() && limit() && text(name) && attach(parent, m_document.create_null(CStringView{ name }));
}

bool CBuilder::number(const CNodeKey parent, const char* const name, const std::uint32_t value) noexcept
{
    return parent.is_valid() && limit() && text(name) && attach(parent, m_document.create_unsigned_integer(value, CStringView{ name }));
}

static void ordinal_name(char (&name)[32], const char prefix, const std::uint32_t ordinal) noexcept
{
    name[0u] = prefix;
    name[1u] = '_';
    (void)std::to_chars(name + 2u, name + sizeof(name) - 1u, ordinal);
}

struct SPhysicalLayout
{
    std::uint32_t id;
    std::uint32_t matrix_stride;
    std::uint32_t major;
    std::uint32_t size;
    std::uint32_t alignment;
    std::uint32_t base_alignment;
    std::uint32_t count;
    std::uint32_t stride;
    std::uint32_t element;
    bool uniform;
    bool array;
    bool matrix;
    bool vector;
    char name[32];
};

class CResources final
{

public:
    CResources(CBuilder& builder, const CPreflight& index, const SLimits& limits, SDiagnostic& diagnostic,
        const CNodeKey types, const CNodeKey interfaces, const CNodeKey members) noexcept
        : m_builder{ builder }, m_index{ index }, m_limits{ limits }, m_diagnostic{ diagnostic },
          m_types{ types }, m_interfaces{ interfaces }, m_members{ members }
    {
    }

    [[nodiscard]] EStatus run(const SpvReflectShaderModule& module) noexcept;

private:
    [[nodiscard]] bool add(const SpvReflectDescriptorBinding& binding, const std::uint32_t ordinal) noexcept;
    [[nodiscard]] bool reject(const EReason reason, const std::uint32_t id) noexcept;
    [[nodiscard]] const std::uint32_t* find_decoration(const std::uint32_t id, const SpvDecoration kind,
        const std::uint32_t member = UINT32_MAX) const noexcept;
    [[nodiscard]] bool decoration(const std::uint32_t id, const SpvDecoration kind, std::uint32_t& value,
        const std::uint32_t member = UINT32_MAX) const noexcept;
    [[nodiscard]] bool metadata(const CNodeKey owner, const std::uint32_t id, const bool variable,
        const std::uint32_t member = UINT32_MAX) noexcept;
    [[nodiscard]] bool image(const CNodeKey metadata, const std::uint32_t* type) noexcept;
    [[nodiscard]] bool unnamed_layer(const std::uint32_t id) noexcept;
    [[nodiscard]] bool layout(const std::uint32_t id, const bool uniform, const std::uint32_t matrix_stride,
        const std::uint32_t major, std::uint32_t& output) noexcept;
    [[nodiscard]] bool type(const CNodeKey parent, const char* key, const std::uint32_t layout) noexcept;
    [[nodiscard]] bool named_type(const std::uint32_t layout) noexcept;
    [[nodiscard]] bool buffer(const SpvReflectDescriptorBinding& binding, const std::uint32_t id,
        const CNodeKey association, const CNodeKey details, const std::uint32_t ordinal) noexcept;
    [[nodiscard]] bool charge(const std::uint32_t declarations, const std::uint32_t members) noexcept;
    [[nodiscard]] bool conflict(const std::uint32_t id, const std::uint32_t member = UINT32_MAX) noexcept;
    CBuilder& m_builder;
    const CPreflight& m_index;
    const SLimits& m_limits;
    SDiagnostic& m_diagnostic;
    const CNodeKey m_types;
    const CNodeKey m_interfaces;
    const CNodeKey m_members;
    CNodeKey m_associations;
    CNodeKey m_structures;
    TPodVector<SPhysicalLayout> m_layouts;
    EStatus m_status{ EStatus::success };
};

bool CResources::reject(const EReason reason, const std::uint32_t id) noexcept
{
    m_status = (reason == EReason::malformed_artifact) ? EStatus::invalid_input : EStatus::unsupported_feature;
    m_diagnostic.reason = reason;
    m_diagnostic.has_result_id = true;
    m_diagnostic.result_id = id;
    return false;
}

const std::uint32_t* CResources::find_decoration(const std::uint32_t id, const SpvDecoration kind, const std::uint32_t member) const noexcept
{
    for (auto link = m_index.annotation(id); link != 0u; link = m_index.annotation_at(link).next)
    {
        const auto* const words = m_index.words() + m_index.annotation_at(link).offset;
        if ((member == UINT32_MAX) && ((words[0u] & 0xffffu) == SpvOpDecorate) && (words[2u] == kind))
        {
            return words;
        }
        if ((member != UINT32_MAX) && ((words[0u] & 0xffffu) == SpvOpMemberDecorate) &&
            (words[2u] == member) && (words[3u] == kind))
        {
            return words;
        }
    }
    return nullptr;
}

bool CResources::decoration(const std::uint32_t id, const SpvDecoration kind, std::uint32_t& value, const std::uint32_t member) const noexcept
{
    const auto* const words = find_decoration(id, kind, member);
    const auto operand = (member == UINT32_MAX) ? 3u : 4u;
    if ((words == nullptr) || ((words[0u] >> 16u) != operand + 1u))
    {
        return false;
    }
    value = words[operand];
    return true;
}

bool CResources::metadata(const CNodeKey owner, const std::uint32_t id, const bool variable, const std::uint32_t member) noexcept
{
    //  Read actual decoration targets. SPIRV-Reflect's convenience flags may
    //  combine variable, type and member decorations, losing their ownership.
    for (auto link = m_index.annotation(id); link != 0u; link = m_index.annotation_at(link).next)
    {
        const auto* const words = m_index.words() + m_index.annotation_at(link).offset;
        const auto opcode = words[0u] & 0xffffu;
        const auto count = words[0u] >> 16u;
        const bool member_annotation = (opcode == SpvOpMemberName) || (opcode == SpvOpMemberDecorate) ||
            (opcode == SpvOpMemberDecorateStringGOOGLE);
        if (member_annotation != (member != UINT32_MAX))
        {
            continue;
        }
        if (member_annotation && (words[2u] != member))
        {
            continue;
        }
        const auto argument = member_annotation ? 3u : 2u;
        if ((opcode == SpvOpName) || (opcode == SpvOpMemberName))
        {
            const auto* const name = reinterpret_cast<const char*>(words + argument);
            if (std::memchr(name, 0, (count - argument) * 4u) == nullptr)
            {
                return reject(EReason::malformed_artifact, id);
            }
            if ((*name != 0) && !m_builder.string(owner, "rendering.source.name", name))
            {
                return false;
            }
            continue;
        }
        if ((opcode == SpvOpDecorateStringGOOGLE) && (words[2u] == SpvDecorationUserTypeGOOGLE))
        {
            continue;
        }
        if ((opcode != SpvOpDecorate) && (opcode != SpvOpMemberDecorate))
        {
            return reject(EReason::unsupported_resource, id);
        }
        const char* key = nullptr;
        switch (static_cast<SpvDecoration>(words[argument]))
        {
            case (SpvDecorationNonWritable):
            {
                key = "rendering.spirv.decoration.NonWritable";
                break;
            }
            case (SpvDecorationNonReadable):
            {
                key = "rendering.spirv.decoration.NonReadable";
                break;
            }
            case (SpvDecorationCoherent):
            {
                key = "rendering.spirv.decoration.Coherent";
                break;
            }
            case (SpvDecorationVolatile):
            {
                key = "rendering.spirv.decoration.Volatile";
                break;
            }
            case (SpvDecorationRestrict):
            {
                key = "rendering.spirv.decoration.Restrict";
                break;
            }
            case (SpvDecorationDescriptorSet):
            case (SpvDecorationBinding):
            {
                if (variable && (count == 4u))
                {
                    continue;
                }
                return reject(EReason::unsupported_resource, id);
            }
            case (SpvDecorationBlock):
            case (SpvDecorationBufferBlock):
            {
                if (!variable && !member_annotation && (count == 3u))
                {
                    continue;
                }
                return reject(EReason::unsupported_resource, id);
            }
            case (SpvDecorationArrayStride):
            {
                if (!variable && !member_annotation && (count == 4u))
                {
                    continue;
                }
                return reject(EReason::unsupported_resource, id);
            }
            case (SpvDecorationOffset):
            case (SpvDecorationMatrixStride):
            {
                if (member_annotation && (count == 5u))
                {
                    continue;
                }
                return reject(EReason::unsupported_resource, id);
            }
            case (SpvDecorationRowMajor):
            case (SpvDecorationColMajor):
            {
                if (member_annotation && (count == 4u))
                {
                    continue;
                }
                return reject(EReason::unsupported_resource, id);
            }
            default:
            {
                return reject(EReason::unsupported_resource, id);
            }
        }
        if (count != argument + 1u)
        {
            return reject(EReason::malformed_artifact, id);
        }
        if (!m_builder.string(owner, key, "true"))
        {
            return false;
        }
    }
    return true;
}

bool CResources::unnamed_layer(const std::uint32_t id) noexcept
{
    //  There is no declaration owner for these pointer/descriptor-array layers.
    //  Reject semantic decorations here rather than silently moving them.
    for (auto link = m_index.annotation(id); link != 0u; link = m_index.annotation_at(link).next)
    {
        const auto* const words = m_index.words() + m_index.annotation_at(link).offset;
        if ((words[0u] & 0xffffu) != SpvOpName)
        {
            return reject(EReason::unsupported_resource, id);
        }
    }
    return true;
}

bool CResources::charge(const std::uint32_t declarations, const std::uint32_t members) noexcept
{
    if (!m_builder.charge(declarations, members))
    {
        m_status = EStatus::limit_exceeded;
        return false;
    }
    return true;
}

bool CResources::conflict(const std::uint32_t id, const std::uint32_t member) noexcept
{
    (void)reject(EReason::layout_conflict, id);
    m_status = EStatus::unrepresentable_layout;
    m_diagnostic.has_member = member != UINT32_MAX;
    m_diagnostic.member = (member == UINT32_MAX) ? 0u : member;
    return false;
}

bool CResources::type(const CNodeKey parent, const char* const key, const std::uint32_t index) noexcept
{
    const auto& value = m_layouts[index];
    if (!value.array)
    {
        return m_builder.string(parent, key, value.name);
    }

    const auto descriptor = m_builder.object(parent, key);
    const auto details = m_builder.object(descriptor, "decorations");
    if (!descriptor.is_valid() || !details.is_valid() || !type(descriptor, "element", value.element) ||
        !m_builder.number(descriptor, "count", value.count) || !m_builder.number(descriptor, "stride", value.stride) ||
        !m_builder.number(descriptor, "alignment", value.alignment))
    {
        return false;
    }
    if ((value.id != 0u) && !metadata(details, value.id, false))
    {
        return false;
    }
    return !value.matrix || (m_builder.string(details, "rendering.matrix.basis", "spirv") &&
        m_builder.string(details, "rendering.matrix.storage_major", (value.major == 1u) ? "row" : "column"));
}

bool CResources::layout(const std::uint32_t id, const bool uniform, const std::uint32_t matrix_stride,
    const std::uint32_t major, std::uint32_t& output) noexcept
{
    for (std::uint32_t index = 0u; index < m_layouts.size(); ++index)
    {
        const auto& cached = m_layouts[index];
        if ((cached.id == id) && (cached.uniform == uniform) && (cached.matrix_stride == matrix_stride) && (cached.major == major))
        {
            output = index;
            return true;
        }
    }

    const auto* const words = m_index.instruction(id);
    if (words == nullptr)
    {
        return reject(EReason::malformed_artifact, id);
    }

    const auto opcode = words[0u] & 0xffffu;
    const auto count = words[0u] >> 16u;
    SPhysicalLayout value{};
    value.id = id;
    value.uniform = uniform;
    value.matrix_stride = matrix_stride;
    value.major = major;
    value.alignment = value.base_alignment = 4u;
    output = static_cast<std::uint32_t>(m_layouts.size());
    if (!m_layouts.push_back(value))
    {
        return false;
    }
    if ((opcode != SpvOpTypeMatrix) && (opcode != SpvOpTypeArray) && ((matrix_stride != 0u) || (major != 0u)))
    {
        return conflict(id);
    }
    if (((opcode == SpvOpTypeFloat) && (count == 3u)) || ((opcode == SpvOpTypeInt) && (count == 4u)))
    {
        if ((words[2u] != 32u) || ((opcode == SpvOpTypeInt) && (words[3u] > 1u)))
        {
            return reject(EReason::unsupported_type, id);
        }
        const char* const scalar = (opcode == SpvOpTypeFloat) ? "f32" : ((words[3u] == 0u) ? "u32" : "i32");
        std::memcpy(value.name, scalar, 4u);
        value.size = 4u;
    }
    else if ((opcode == SpvOpTypeArray) && (count == 4u))
    {
        const auto* const extent = m_index.instruction(words[3u]);
        if ((extent == nullptr) || ((extent[0u] & 0xffffu) != SpvOpConstant) || ((extent[0u] >> 16u) != 4u) || (extent[3u] == 0u))
        {
            return reject(EReason::unsupported_type, id);
        }
        value.array = true;
        value.count = extent[3u];
        if (!layout(words[2u], uniform, matrix_stride, major, value.element))
        {
            return false;
        }
        const auto child = m_layouts[value.element];
        value.alignment = value.base_alignment = uniform ? std::max(16u, child.base_alignment) : child.base_alignment;
        if (!decoration(id, SpvDecorationArrayStride, value.stride) || (value.stride < child.size) ||
            (value.stride % value.alignment != 0u))
        {
            return conflict(id);
        }
        const auto size = static_cast<std::uint64_t>(value.count) * value.stride;
        if (size > memory::k_byte_size_ceiling)
        {
            return conflict(id);
        }
        value.size = static_cast<std::uint32_t>(size);
    }
    else if ((opcode == SpvOpTypeMatrix) && (count == 4u))
    {
        const auto* const vector = m_index.instruction(words[2u]);
        if ((vector == nullptr) || ((vector[0u] & 0xffffu) != SpvOpTypeVector) || ((vector[0u] >> 16u) != 4u) ||
            (words[3u] < 2u) || (words[3u] > 4u) || (vector[3u] < 2u) || (vector[3u] > 4u) ||
            (major < 1u) || (major > 2u) || (matrix_stride == 0u))
        {
            return conflict(id);
        }
        SPhysicalLayout components{};
        components.array = true;
        components.uniform = uniform;
        components.count = (major == 1u) ? words[3u] : vector[3u];
        components.size = components.count * 4u;
        components.stride = components.alignment = 4u;
        components.base_alignment = (components.count == 2u) ? 8u : 16u;
        if (!layout(vector[2u], uniform, 0u, 0u, components.element))
        {
            return false;
        }
        if (std::strcmp(m_layouts[components.element].name, "f32") != 0)
        {
            return reject(EReason::unsupported_type, id);
        }
        value.array = value.matrix = true;
        value.count = (major == 1u) ? vector[3u] : words[3u];
        value.alignment = value.base_alignment = uniform ? 16u : components.base_alignment;
        value.stride = matrix_stride;
        if ((value.stride < components.size) || (value.stride % value.alignment != 0u) ||
            (static_cast<std::uint64_t>(value.count) * value.stride > memory::k_byte_size_ceiling))
        {
            return conflict(id);
        }
        value.size = value.count * value.stride;
        value.element = static_cast<std::uint32_t>(m_layouts.size());
        if (!m_layouts.push_back(components))
        {
            return false;
        }
    }
    else if (((opcode == SpvOpTypeVector) && (count == 4u)) || (opcode == SpvOpTypeStruct))
    {
        value.vector = opcode == SpvOpTypeVector;
        const auto members = value.vector ? words[3u] : count - 2u;
        if ((members == 0u) || (value.vector && ((members < 2u) || (members > 4u))))
        {
            return reject(EReason::unsupported_type, id);
        }
        if (members > m_limits.aggregate_members)
        {
            m_status = EStatus::limit_exceeded;
            m_diagnostic.reason = EReason::member_limit;
            return false;
        }
        if (!charge(1u, members))
        {
            return false;
        }
        if (!m_structures.is_valid())
        {
            m_structures = m_builder.object(m_types, "structures");
        }
        ordinal_name(value.name, 'p', output);
        const auto declaration = m_builder.object(m_structures, value.name);
        const auto fields = m_builder.array(declaration, "members");
        const auto details = m_builder.object(declaration, "detail");
        const auto decorations = m_builder.object(declaration, "decorations");
        if (!declaration.is_valid() || !fields.is_valid() || !details.is_valid() || !decorations.is_valid() ||
            !metadata(decorations, id, false))
        {
            return false;
        }
        TPodVector<SEdge> occupied;
        std::uint32_t complete = 0u;
        for (std::uint32_t member = 0u; member < members; ++member)
        {
            std::uint32_t offset = member * 4u;
            std::uint32_t stride = 0u;
            std::uint32_t orientation = 0u;
            std::uint32_t child_index = 0u;
            if (!value.vector)
            {
                if (!decoration(id, SpvDecorationOffset, offset, member))
                {
                    return conflict(id, member);
                }
                const bool row = find_decoration(id, SpvDecorationRowMajor, member) != nullptr;
                const bool column = find_decoration(id, SpvDecorationColMajor, member) != nullptr;
                if (row && column)
                {
                    return conflict(id, member);
                }
                orientation = row ? 1u : (column ? 2u : 0u);
                (void)decoration(id, SpvDecorationMatrixStride, stride, member);
            }
            if (!layout(value.vector ? words[2u] : words[2u + member], uniform, stride, orientation, child_index))
            {
                return false;
            }
            const auto child = m_layouts[child_index];
            if (value.vector && ((child.size != 4u) || child.array))
            {
                return reject(EReason::unsupported_type, id);
            }
            const auto end = static_cast<std::uint64_t>(offset) + child.size;
            if ((offset % child.alignment != 0u) || (end > memory::k_byte_size_ceiling) ||
                (child.vector && ((offset / 16u) != ((end - 1u) / 16u))))
            {
                return conflict(id, member);
            }
            for (std::size_t index = 0u; index < occupied.size(); ++index)
            {
                if ((offset < occupied[index].next) && (occupied[index].target < end))
                {
                    return conflict(id, member);
                }
            }
            if (!occupied.push_back({ offset, static_cast<std::uint32_t>(end) }))
            {
                return false;
            }
            complete = std::max(complete, static_cast<std::uint32_t>(end));
            value.base_alignment = std::max(value.base_alignment, child.base_alignment);
            char member_name[32]{};
            ordinal_name(member_name, 'm', member);
            const auto field = m_builder.object(fields, member_name);
            const auto field_details = m_builder.object(field, "decorations");
            if (!field.is_valid() || !field_details.is_valid() || !type(field, "type", child_index) ||
                !m_builder.number(field, "offset", offset) || (!value.vector && !metadata(field_details, id, false, member)))
            {
                return false;
            }
        }
        if (value.vector)
        {
            value.alignment = 4u;
            value.base_alignment = (members == 2u) ? 8u : 16u;
        }
        else
        {
            value.alignment = value.base_alignment = uniform ? std::max(16u, value.base_alignment) : value.base_alignment;
        }
        const auto rounded = (static_cast<std::uint64_t>(complete) + value.alignment - 1u) & ~(static_cast<std::uint64_t>(value.alignment) - 1u);
        if (rounded > memory::k_byte_size_ceiling)
        {
            return conflict(id);
        }
        value.size = static_cast<std::uint32_t>(rounded);
        if (!m_builder.number(details, "size", value.size) || !m_builder.number(details, "alignment", value.alignment))
        {
            return false;
        }
    }
    else
    {
        return reject(EReason::unsupported_type, id);
    }
    m_layouts[output] = value;
    return true;
}

bool CResources::named_type(const std::uint32_t index) noexcept
{
    auto& value = m_layouts[index];
    if (value.name[0u] != 0)
    {
        return true;
    }
    if (!charge(1u, 1u))
    {
        return false;
    }
    if (!m_structures.is_valid())
    {
        m_structures = m_builder.object(m_types, "structures");
    }
    ordinal_name(value.name, 'p', index);
    const auto declaration = m_builder.object(m_structures, value.name);
    const auto members = m_builder.array(declaration, "members");
    const auto storage = m_builder.object(members, "storage");
    const auto details = m_builder.object(declaration, "detail");
    //  Associations require a named target. This is a storage wrapper, not a
    //  source declaration; interpretation stays on the actual array descriptor.
    return declaration.is_valid() && members.is_valid() && storage.is_valid() && details.is_valid() &&
        type(storage, "type", index) && m_builder.number(storage, "offset", 0u) &&
        m_builder.number(details, "size", value.size) && m_builder.number(details, "alignment", value.alignment);
}

bool CResources::buffer(const SpvReflectDescriptorBinding& binding, const std::uint32_t id,
    const CNodeKey association, const CNodeKey details, const std::uint32_t ordinal) noexcept
{
    const bool uniform = binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    const char* interpretation = nullptr;
    switch (binding.user_type)
    {
        case (SPV_REFLECT_USER_TYPE_CBUFFER):
        case (SPV_REFLECT_USER_TYPE_CONSTANT_BUFFER):
        case (SPV_REFLECT_USER_TYPE_TBUFFER):
        case (SPV_REFLECT_USER_TYPE_TEXTURE_BUFFER):
        {
            interpretation = "typed";
            break;
        }
        case (SPV_REFLECT_USER_TYPE_STRUCTURED_BUFFER):
        case (SPV_REFLECT_USER_TYPE_RW_STRUCTURED_BUFFER):
        {
            interpretation = "structured";
            break;
        }
        case (SPV_REFLECT_USER_TYPE_BYTE_ADDRESS_BUFFER):
        case (SPV_REFLECT_USER_TYPE_RW_BYTE_ADDRESS_BUFFER):
        {
            interpretation = "raw";
            break;
        }
        default:
        {
            return reject(EReason::unsupported_resource, binding.spirv_id);
        }
    }
    if (!m_builder.string(details, "rendering.buffer.interpretation", interpretation))
    {
        return false;
    }

    const auto* const block = m_index.instruction(id);
    if ((block == nullptr) || ((block[0u] & 0xffffu) != SpvOpTypeStruct) || ((block[0u] >> 16u) < 3u))
    {
        return reject(EReason::unsupported_type, id);
    }

    const auto members = (block[0u] >> 16u) - 2u;
    const auto* const tail = m_index.instruction(block[members + 1u]);
    if ((tail == nullptr) || ((tail[0u] & 0xffffu) != SpvOpTypeRuntimeArray))
    {
        std::uint32_t physical = 0u;
        return layout(id, uniform, 0u, 0u, physical) && m_builder.string(association, "target", m_layouts[physical].name);
    }
    //  Fixed-prefix regions need a separately reviewed representation; do not
    //  synthesize a structure whose rounded tail could invent owned padding.
    if (uniform || (members != 1u) || ((tail[0u] >> 16u) != 3u))
    {
        return reject(EReason::unsupported_resource, id);
    }
    std::uint32_t element = 0u;
    std::uint32_t stride = 0u;
    std::uint32_t offset = 0u;
    std::uint32_t matrix_stride = 0u;
    const bool row = find_decoration(id, SpvDecorationRowMajor, 0u) != nullptr;
    const bool column = find_decoration(id, SpvDecorationColMajor, 0u) != nullptr;
    if (row && column)
    {
        return conflict(id, 0u);
    }
    (void)decoration(id, SpvDecorationMatrixStride, matrix_stride, 0u);
    if (!layout(tail[2u], false, matrix_stride, row ? 1u : (column ? 2u : 0u), element) || !named_type(element))
    {
        return false;
    }

    const auto element_layout = m_layouts[element];
    if (!decoration(tail[1u], SpvDecorationArrayStride, stride) || !decoration(id, SpvDecorationOffset, offset, members - 1u) ||
        (stride < element_layout.size) || (stride % element_layout.base_alignment != 0u) ||
        (offset % element_layout.base_alignment != 0u))
    {
        return conflict(id, members - 1u);
    }
    if (!charge(2u, 1u))
    {
        return false;
    }
    char layout_name[32]{};
    char sequence_name[32]{};
    ordinal_name(layout_name, 'b', ordinal);
    ordinal_name(sequence_name, 'q', ordinal);
    const auto declaration = m_builder.object(m_interfaces, layout_name);
    const auto regions = m_builder.array(declaration, "members");
    const auto block_details = m_builder.object(declaration, "decorations");
    const auto sequence = m_builder.object(m_associations, sequence_name);
    const auto sequence_details = m_builder.object(sequence, "decorations");
    if (!declaration.is_valid() || !regions.is_valid() || !block_details.is_valid() || !sequence.is_valid() || !sequence_details.is_valid() ||
        !m_builder.string(association, "target", layout_name) || !m_builder.string(block_details, "rendering.interface.kind", "buffer_layout") ||
        !metadata(block_details, id, false) || !m_builder.string(sequence, "target", element_layout.name) ||
        !m_builder.string(sequence_details, "rendering.sequence.kind", "runtime") ||
        !m_builder.decimal(sequence_details, "rendering.sequence.stride", stride) || !metadata(sequence_details, tail[1u], false))
    {
        return false;
    }

    const auto region = m_builder.object(regions, "elements");
    const auto region_details = m_builder.object(region, "decorations");
    return region.is_valid() && region_details.is_valid() && m_builder.string(region, "type", sequence_name) &&
        m_builder.decimal(region_details, "rendering.buffer.offset", offset) && metadata(region_details, id, false, members - 1u);
}

bool CResources::image(const CNodeKey metadata, const std::uint32_t* const type) noexcept
{
    const auto count = type[0u] >> 16u;
    if ((count != 9u) || ((type[0u] & 0xffffu) != SpvOpTypeImage))
    {
        return reject(EReason::unsupported_resource, type[1u]);
    }

    const auto* const scalar = m_index.instruction(type[2u]);
    const char* scalar_name = nullptr;
    if (scalar != nullptr)
    {
        const auto op = scalar[0u] & 0xffffu;
        if ((op == SpvOpTypeFloat) && ((scalar[0u] >> 16u) == 3u) && (scalar[2u] == 32u))
        {
            scalar_name = "f32";
        }
        if ((op == SpvOpTypeInt) && ((scalar[0u] >> 16u) == 4u) && (scalar[2u] == 32u) && (scalar[3u] <= 1u))
        {
            scalar_name = (scalar[3u] == 0u) ? "u32" : "i32";
        }
    }
    if (scalar_name == nullptr)
    {
        return reject(EReason::unsupported_type, type[2u]);
    }

    const char* dimension = nullptr;
    switch (static_cast<SpvDim>(type[3u]))
    {
        case (SpvDim1D):
        {
            dimension = "1d";
            break;
        }
        case (SpvDim2D):
        {
            dimension = "2d";
            break;
        }
        case (SpvDim3D):
        {
            dimension = "3d";
            break;
        }
        case (SpvDimCube):
        {
            dimension = "cube";
            break;
        }
        case (SpvDimBuffer):
        {
            dimension = "buffer";
            break;
        }
        default:
        {
            return reject(EReason::unsupported_resource, type[1u]);
        }
    }
    if ((type[4u] > 2u) || (type[5u] > 1u) || (type[6u] > 1u) || (type[7u] == 0u) || (type[7u] > 2u))
    {
        return reject(EReason::unsupported_resource, type[1u]);
    }

    static const char* const depths[]{ "non_depth", "depth", "unknown" };
    const auto* const format = SpvImageFormatToString(static_cast<SpvImageFormat>(type[8u]));
    if (std::strcmp(format, "Unknown") == 0 && type[8u] != SpvImageFormatUnknown)
    {
        return reject(EReason::unsupported_resource, type[1u]);
    }
    return m_builder.string(metadata, "rendering.image.dimension", dimension) &&
        m_builder.string(metadata, "rendering.image.arrayed", (type[5u] == 0u) ? "false" : "true") &&
        m_builder.string(metadata, "rendering.image.multisampled", (type[6u] == 0u) ? "false" : "true") &&
        m_builder.string(metadata, "rendering.image.sampled_scalar", scalar_name) &&
        m_builder.string(metadata, "rendering.spirv.image_format", format) &&
        m_builder.string(metadata, "rendering.spirv.image_depth", depths[type[4u]]);
}

bool CResources::add(const SpvReflectDescriptorBinding& binding, const std::uint32_t ordinal) noexcept
{
    const char* kind = nullptr;
    switch (binding.descriptor_type)
    {
        case (SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER):
        {
            kind = "sampler";
            break;
        }
        case (SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE):
        {
            kind = "sampled_image";
            break;
        }
        case (SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE):
        {
            kind = "storage_image";
            break;
        }
        case (SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER):
        {
            kind = "uniform_texel_buffer";
            break;
        }
        case (SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER):
        {
            kind = "storage_texel_buffer";
            break;
        }
        case (SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER):
        {
            kind = "uniform_buffer";
            break;
        }
        case (SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER):
        {
            kind = "storage_buffer";
            break;
        }
        default:
        {
            return reject(EReason::unsupported_resource, binding.spirv_id);
        }
    }

    const auto* variable = m_index.instruction(binding.spirv_id);
    if ((variable == nullptr) || ((variable[0u] & 0xffffu) != SpvOpVariable) || ((variable[0u] >> 16u) < 4u))
    {
        return reject(EReason::malformed_artifact, binding.spirv_id);
    }

    const auto* pointer = m_index.instruction(variable[1u]);
    if ((pointer == nullptr) || ((pointer[0u] & 0xffffu) != SpvOpTypePointer) || ((pointer[0u] >> 16u) != 4u))
    {
        return reject(EReason::unsupported_type, variable[1u]);
    }
    if (!unnamed_layer(variable[1u]))
    {
        return false;
    }

    const auto* type = m_index.instruction(pointer[3u]);
    if (type == nullptr)
    {
        return reject(EReason::malformed_artifact, pointer[3u]);
    }
    if ((binding.array.dims_count > m_limits.array_rank) || (binding.array.dims_count > SPV_REFLECT_MAX_ARRAY_DIMS))
    {
        m_status = EStatus::limit_exceeded;
        m_diagnostic.reason = EReason::nesting_limit;
        return false;
    }
    std::uint32_t descriptors = 1u;
    for (std::uint32_t dimension = 0u; dimension < binding.array.dims_count; ++dimension)
    {
        if (((type[0u] & 0xffffu) != SpvOpTypeArray) || ((type[0u] >> 16u) != 4u))
        {
            return reject(EReason::unsupported_resource, type[1u]);
        }
        if (!unnamed_layer(type[1u]))
        {
            return false;
        }
        const auto* const extent = m_index.instruction(type[3u]);
        if ((extent == nullptr) || ((extent[0u] & 0xffffu) != SpvOpConstant) || ((extent[0u] >> 16u) != 4u) ||
            (extent[3u] == 0u) || (extent[3u] != binding.array.dims[dimension]) || (extent[3u] > UINT32_MAX / descriptors))
        {
            return reject(EReason::unsupported_resource, type[1u]);
        }
        descriptors *= extent[3u];
        type = m_index.instruction(type[2u]);
        if (type == nullptr)
        {
            return reject(EReason::malformed_artifact, binding.spirv_id);
        }
    }
    if (descriptors != binding.count)
    {
        return reject(EReason::unsupported_resource, binding.spirv_id);
    }
    std::uint32_t set = 0u;
    std::uint32_t slot = 0u;
    if (!decoration(binding.spirv_id, SpvDecorationDescriptorSet, set) || !decoration(binding.spirv_id, SpvDecorationBinding, slot) ||
        (set != binding.set) || (slot != binding.binding))
    {
        return reject(EReason::unsupported_resource, binding.spirv_id);
    }
    char name[32]{};
    ordinal_name(name, 'r', ordinal);
    const auto declaration = m_builder.object(m_associations, name);
    const auto details = m_builder.object(declaration, "decorations");
    const auto occurrence = m_builder.object(m_members, name);
    const auto occurrence_details = m_builder.object(occurrence, "decorations");
    const bool is_buffer = (binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) ||
        (binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    if (!declaration.is_valid() || !details.is_valid() || !occurrence.is_valid() || !occurrence_details.is_valid() ||
        (!is_buffer && !m_builder.null(declaration, "target")) || !m_builder.string(details, "rendering.resource.kind", kind) ||
        !m_builder.string(occurrence, "type", name) || !m_builder.number(occurrence, "count", descriptors) ||
        !m_builder.decimal(occurrence_details, "rendering.vulkan.set", set) ||
        !m_builder.decimal(occurrence_details, "rendering.vulkan.binding", slot) ||
        (!is_buffer && !metadata(details, type[1u], false)) || !metadata(occurrence_details, binding.spirv_id, true))
    {
        return false;
    }
    if (binding.array.dims_count != 0u)
    {
        if (!m_builder.decimal(occurrence_details, "rendering.descriptor_array.rank", binding.array.dims_count))
        {
            return false;
        }
        for (std::uint32_t dimension = 0u; dimension < binding.array.dims_count; ++dimension)
        {
            char key[64]{ "rendering.descriptor_array.extent." };
            const auto prefix = std::strlen(key);
            (void)std::to_chars(key + prefix, key + sizeof(key) - 1u, dimension);
            if (!m_builder.decimal(occurrence_details, key, binding.array.dims[dimension]))
            {
                return false;
            }
        }
    }
    if (is_buffer)
    {
        return buffer(binding, type[1u], declaration, details, ordinal);
    }
    if (binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER)
    {
        return ((type[0u] & 0xffffu) == SpvOpTypeSampler) || reject(EReason::unsupported_resource, type[1u]);
    }

    const bool texel = (binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) ||
        (binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
    if (texel && !m_builder.string(details, "rendering.buffer.interpretation", "typed"))
    {
        return false;
    }
    return image(details, type);
}

EStatus CResources::run(const SpvReflectShaderModule& module) noexcept
{
    const auto count = module.descriptor_binding_count;
    if (count == 0u)
    {
        return EStatus::success;
    }
    if (count > m_limits.aggregate_members)
    {
        m_diagnostic.reason = EReason::declaration_limit;
        return EStatus::limit_exceeded;
    }
    if (!charge(count, count))
    {
        return m_status;
    }
    m_associations = m_builder.object(m_types, "associations");
    if (!m_associations.is_valid())
    {
        return (m_diagnostic.reason == EReason::none) ? EStatus::allocation_failed : EStatus::limit_exceeded;
    }
    for (std::uint32_t ordinal = 0u; ordinal < count; ++ordinal)
    {
        if (!add(module.descriptor_bindings[ordinal], ordinal))
        {
            return (m_status != EStatus::success) ? m_status :
                ((m_diagnostic.reason == EReason::none) ? EStatus::allocation_failed : EStatus::limit_exceeded);
        }
    }
    return EStatus::success;
}

static constexpr std::uint32_t k_vertex = 1u, k_hull = 2u, k_domain = 4u, k_geometry = 8u, k_fragment = 16u, k_compute = 32u;

struct SExecutionMode
{
    SpvExecutionMode mode;
    std::uint32_t stages;
    std::uint32_t operands;
    std::uint32_t exclusive;
};

static EStatus execution_modes(CBuilder& builder, const CPreflight& index, const SpvReflectEntryPoint& entry,
    const shader::EStage stage, const CNodeKey decorations, SDiagnostic& diagnostic) noexcept
{
    //  Local facts only: tessellation modes can be supplied by the other stage.
    //  Cross-stage agreement, device limits and pipeline configuration are later checks.
    static constexpr SExecutionMode modes[]{
        { SpvExecutionModeInvocations, k_geometry, 1u, 0u },
        { SpvExecutionModeSpacingEqual, k_hull | k_domain, 0u, 1u },
        { SpvExecutionModeSpacingFractionalEven, k_hull | k_domain, 0u, 1u },
        { SpvExecutionModeSpacingFractionalOdd, k_hull | k_domain, 0u, 1u },
        { SpvExecutionModeVertexOrderCw, k_hull | k_domain, 0u, 2u },
        { SpvExecutionModeVertexOrderCcw, k_hull | k_domain, 0u, 2u },
        { SpvExecutionModeOriginUpperLeft, k_fragment, 0u, 0u },
        { SpvExecutionModeEarlyFragmentTests, k_fragment, 0u, 0u },
        { SpvExecutionModePointMode, k_hull | k_domain, 0u, 0u },
        { SpvExecutionModeDepthReplacing, k_fragment, 0u, 0u },
        { SpvExecutionModeDepthGreater, k_fragment, 0u, 3u },
        { SpvExecutionModeDepthLess, k_fragment, 0u, 3u },
        { SpvExecutionModeDepthUnchanged, k_fragment, 0u, 3u },
        { SpvExecutionModeLocalSize, k_compute, 3u, 0u },
        { SpvExecutionModeInputPoints, k_geometry, 0u, 4u },
        { SpvExecutionModeInputLines, k_geometry, 0u, 4u },
        { SpvExecutionModeInputLinesAdjacency, k_geometry, 0u, 4u },
        { SpvExecutionModeTriangles, k_hull | k_domain | k_geometry, 0u, 4u },
        { SpvExecutionModeInputTrianglesAdjacency, k_geometry, 0u, 4u },
        { SpvExecutionModeQuads, k_hull | k_domain, 0u, 4u },
        { SpvExecutionModeIsolines, k_hull | k_domain, 0u, 4u },
        { SpvExecutionModeOutputVertices, k_hull | k_domain | k_geometry, 1u, 0u },
        { SpvExecutionModeOutputPoints, k_geometry, 0u, 5u },
        { SpvExecutionModeOutputLineStrip, k_geometry, 0u, 5u },
        { SpvExecutionModeOutputTriangleStrip, k_geometry, 0u, 5u }
    };
    bool seen[std::size(modes)]{};
    bool exclusive[6]{};
    const auto stage_mask = 1u << static_cast<std::uint8_t>(stage);
    const auto reject = [&](const std::uint32_t offset) noexcept
    {
        diagnostic.reason = EReason::unsupported_execution;
        diagnostic.has_word_offset = diagnostic.has_result_id = true;
        diagnostic.word_offset = offset;
        diagnostic.result_id = entry.id;
        return EStatus::unsupported_feature;
    };
    const auto* const words = index.words();
    for (std::size_t offset = 5u; offset < index.count(); offset += words[offset] >> 16u)
    {
        const auto opcode = words[offset] & 0xffffu;
        if ((opcode != SpvOpExecutionMode) && (opcode != SpvOpExecutionModeId))
        {
            continue;
        }
        const auto count = words[offset] >> 16u;
        if ((count < 3u) || (words[offset + 1u] != entry.id) || (opcode == SpvOpExecutionModeId))
        {
            return reject(static_cast<std::uint32_t>(offset));
        }
        std::size_t selected = 0u;
        while ((selected < std::size(modes)) && (modes[selected].mode != words[offset + 2u]))
        {
            ++selected;
        }
        if (selected == std::size(modes))
        {
            return reject(static_cast<std::uint32_t>(offset));
        }
        const auto& mode = modes[selected];
        if (((mode.stages & stage_mask) == 0u) || (count != 3u + mode.operands) || seen[selected] ||
            ((mode.exclusive != 0u) && exclusive[mode.exclusive]))
        {
            return reject(static_cast<std::uint32_t>(offset));
        }
        seen[selected] = true;
        exclusive[mode.exclusive] = true;
        for (std::uint32_t operand = 0u; operand < mode.operands; ++operand)
        {
            if (words[offset + 3u + operand] == 0u)
            {
                return reject(static_cast<std::uint32_t>(offset));
            }
        }
        char key[96]{ "rendering.spirv.execution." };
        const auto prefix = std::strlen(key);
        const auto* const name = SpvExecutionModeToString(mode.mode);
        std::memcpy(key + prefix, name, std::strlen(name) + 1u);
        bool built = true;
        if (mode.operands == 3u)
        {
            const auto suffix = std::strlen(key);
            key[suffix] = '.';
            for (std::uint32_t axis = 0u; built && (axis < 3u); ++axis)
            {
                key[suffix + 1u] = "xyz"[axis];
                built = builder.decimal(decorations, key, words[offset + 3u + axis]);
            }
        }
        else
        {
            built = (mode.operands == 1u) ? builder.decimal(decorations, key, words[offset + 3u]) : builder.string(decorations, key, "true");
        }
        if (!built)
        {
            return (diagnostic.reason == EReason::none) ? EStatus::allocation_failed : EStatus::limit_exceeded;
        }
    }

    const auto has = [&](const SpvExecutionMode mode) noexcept
    {
        for (std::size_t i = 0u; i < std::size(modes); ++i)
        {
            if (modes[i].mode == mode)
            {
                return seen[i];
            }
        }
        return false;
    };
    if (((stage == shader::EStage::compute) && !has(SpvExecutionModeLocalSize)) ||
        ((stage == shader::EStage::pixel) && !has(SpvExecutionModeOriginUpperLeft)) ||
        ((stage == shader::EStage::hull) && !has(SpvExecutionModeOutputVertices)) ||
        ((stage == shader::EStage::geometry) && (!exclusive[4u] || !exclusive[5u] || !has(SpvExecutionModeOutputVertices))) ||
        (exclusive[3u] && !has(SpvExecutionModeDepthReplacing)))
    {
        return reject(0u);
    }
    return EStatus::success;
}

enum class EValueDomain : std::uint8_t
{
    vertex = 0u,
    patch,
    fragment,
    target,
    system
};

struct SInterfaceMetadata
{
    std::uint32_t builtin{ UINT32_MAX };
    bool patch{ false };
    bool location{ false };
    bool component{ false };
    bool index{ false };
};

struct SValueShape
{
    std::uint32_t id;
    char name[32];
};

struct SStageValue
{
    std::uint32_t id;
    std::uint32_t type;
    std::uint32_t wrapper;
    std::uint32_t extent;
    EValueDomain domain;
};

class CStages final
{

public:
    CStages(CBuilder& builder, const CPreflight& index, const SLimits& limits, SDiagnostic& diagnostic,
        const shader::EStage stage, const CNodeKey types, const CNodeKey interfaces) noexcept
        : m_builder{ builder }, m_index{ index }, m_limits{ limits }, m_diagnostic{ diagnostic },
          m_stage{ stage }, m_types{ types }, m_interfaces{ interfaces }
    {
    }

    [[nodiscard]] EStatus run(const SpvReflectEntryPoint& entry, const CNodeKey inputs, const CNodeKey outputs) noexcept;

private:
    [[nodiscard]] bool reject(const std::uint32_t id, const std::uint32_t member = UINT32_MAX) noexcept;
    [[nodiscard]] bool metadata(const CNodeKey owner, const std::uint32_t id, const std::uint32_t member,
        const bool occurrence, SInterfaceMetadata& details) noexcept;
    [[nodiscard]] bool builtin(const SInterfaceMetadata& details, const bool output, EValueDomain& domain) noexcept;
    [[nodiscard]] bool classify(const std::uint32_t type, const SInterfaceMetadata& details, const bool output,
        EValueDomain& domain) noexcept;
    [[nodiscard]] bool extent(const std::uint32_t id, std::uint32_t& value) noexcept;
    [[nodiscard]] bool shape(const std::uint32_t id, std::uint32_t& result) noexcept;
    [[nodiscard]] bool direction(SpvReflectInterfaceVariable* const* variables, const std::uint32_t count,
        const bool output, const CNodeKey members) noexcept;
    CBuilder& m_builder;
    const CPreflight& m_index;
    const SLimits& m_limits;
    SDiagnostic& m_diagnostic;
    const shader::EStage m_stage;
    const CNodeKey m_types;
    const CNodeKey m_interfaces;
    TPodVector<SValueShape> m_shapes;
};

bool CStages::reject(const std::uint32_t id, const std::uint32_t member) noexcept
{
    m_diagnostic.reason = EReason::unsupported_interface;
    m_diagnostic.has_result_id = true;
    m_diagnostic.result_id = id;
    m_diagnostic.has_member = member != UINT32_MAX;
    m_diagnostic.member = (member == UINT32_MAX) ? 0u : member;
    return false;
}

bool CStages::metadata(const CNodeKey owner, const std::uint32_t id, const std::uint32_t member,
    const bool occurrence, SInterfaceMetadata& details) noexcept
{
    std::uint32_t interpolation_site = 0u;
    std::uint32_t interpolation_mode = 0u;
    for (auto link = m_index.annotation(id); link != 0u; link = m_index.annotation_at(link).next)
    {
        const auto* const words = m_index.words() + m_index.annotation_at(link).offset;
        const auto opcode = words[0u] & 0xffffu;
        const auto count = words[0u] >> 16u;
        const bool member_annotation = (opcode == SpvOpMemberName) || (opcode == SpvOpMemberDecorate) ||
            (opcode == SpvOpMemberDecorateStringGOOGLE);
        if ((member_annotation != (member != UINT32_MAX)) || (member_annotation && (words[2u] != member)))
        {
            continue;
        }
        const auto argument = member_annotation ? 3u : 2u;
        const char* key = nullptr;
        if ((opcode == SpvOpName) || (opcode == SpvOpMemberName))
        {
            key = "rendering.source.name";
        }
        const bool semantic = ((opcode == SpvOpDecorateStringGOOGLE) || (opcode == SpvOpMemberDecorateStringGOOGLE)) &&
            (words[argument] == SpvDecorationHlslSemanticGOOGLE);
        if (semantic)
        {
            if (!occurrence)
            {
                return reject(id, member);
            }
            key = "rendering.hlsl.semantic";
        }
        if (key != nullptr)
        {
            const auto start = argument + (semantic ? 1u : 0u);
            const auto* const text = reinterpret_cast<const char*>(words + start);
            if ((start >= count) || (std::memchr(text, 0, (count - start) * 4u) == nullptr))
            {
                return reject(id, member);
            }
            if (owner.is_valid() && (*text != 0) && !m_builder.string(owner, key, text))
            {
                return false;
            }
            continue;
        }
        if ((opcode != SpvOpDecorate) && (opcode != SpvOpMemberDecorate))
        {
            return reject(id, member);
        }
        const auto decoration = static_cast<SpvDecoration>(words[argument]);
        bool numeric = false;
        switch (decoration)
        {
            case (SpvDecorationBlock):
            {
                if (!occurrence && (count == argument + 1u))
                {
                    continue;
                }
                return reject(id, member);
            }
            case (SpvDecorationBuiltIn):
            {
                key = "rendering.spirv.builtin";
                numeric = true;
                break;
            }
            case (SpvDecorationLocation):
            {
                key = "rendering.vulkan.location";
                numeric = true;
                details.location = true;
                break;
            }
            case (SpvDecorationComponent):
            {
                key = "rendering.vulkan.component";
                numeric = true;
                details.component = true;
                break;
            }
            case (SpvDecorationIndex):
            {
                key = "rendering.vulkan.index";
                numeric = true;
                details.index = true;
                break;
            }
            case (SpvDecorationFlat):
            {
                key = "rendering.spirv.decoration.Flat";
                ++interpolation_mode;
                break;
            }
            case (SpvDecorationNoPerspective):
            {
                key = "rendering.spirv.decoration.NoPerspective";
                ++interpolation_mode;
                break;
            }
            case (SpvDecorationCentroid):
            {
                key = "rendering.spirv.decoration.Centroid";
                ++interpolation_site;
                break;
            }
            case (SpvDecorationSample):
            {
                key = "rendering.spirv.decoration.Sample";
                ++interpolation_site;
                break;
            }
            case (SpvDecorationPatch):
            {
                key = "rendering.spirv.decoration.Patch";
                details.patch = true;
                ++interpolation_site;
                break;
            }
            case (SpvDecorationInvariant):
            {
                key = "rendering.spirv.decoration.Invariant";
                break;
            }
            case (SpvDecorationRelaxedPrecision):
            {
                key = "rendering.spirv.decoration.RelaxedPrecision";
                break;
            }
            default:
            {
                return reject(id, member);
            }
        }
        if (!occurrence || (count != argument + (numeric ? 2u : 1u)) || (interpolation_site > 1u) ||
            (interpolation_mode > 1u))
        {
            return reject(id, member);
        }
        if (decoration == SpvDecorationBuiltIn)
        {
            if (details.builtin != UINT32_MAX)
            {
                return reject(id, member);
            }
            details.builtin = words[argument + 1u];
            if (owner.is_valid() && !m_builder.string(owner, key, SpvBuiltInToString(static_cast<SpvBuiltIn>(details.builtin))))
            {
                return false;
            }
        }
        else if (numeric)
        {
            if (((decoration == SpvDecorationComponent) && (words[argument + 1u] > 3u)) ||
                ((decoration == SpvDecorationIndex) && (words[argument + 1u] > 1u)))
            {
                return reject(id, member);
            }
            if (owner.is_valid() && !m_builder.decimal(owner, key, words[argument + 1u]))
            {
                return false;
            }
        }
        else if (owner.is_valid() && !m_builder.string(owner, key, "true"))
        {
            return false;
        }
    }
    if ((details.builtin != UINT32_MAX) && (details.location || details.component || details.index))
    {
        return reject(id, member);
    }
    return true;
}

bool CStages::builtin(const SInterfaceMetadata& details, const bool output, EValueDomain& domain) noexcept
{
    const auto stage = 1u << static_cast<std::uint8_t>(m_stage);
    std::uint32_t inputs = 0u;
    std::uint32_t outputs = 0u;
    domain = EValueDomain::system;
    switch (static_cast<SpvBuiltIn>(details.builtin))
    {
        case (SpvBuiltInPosition):
        case (SpvBuiltInPointSize):
        {
            inputs = k_hull | k_domain | k_geometry;
            outputs = k_vertex | k_hull | k_domain | k_geometry;
            domain = EValueDomain::vertex;
            break;
        }
        case (SpvBuiltInClipDistance):
        case (SpvBuiltInCullDistance):
        {
            inputs = k_hull | k_domain | k_geometry | k_fragment;
            outputs = k_vertex | k_hull | k_domain | k_geometry;
            domain = (m_stage == shader::EStage::pixel) ? EValueDomain::fragment : EValueDomain::vertex;
            break;
        }
        case (SpvBuiltInTessLevelOuter):
        case (SpvBuiltInTessLevelInner):
        {
            inputs = k_domain;
            outputs = k_hull;
            domain = EValueDomain::patch;
            break;
        }
        case (SpvBuiltInVertexIndex):
        case (SpvBuiltInInstanceIndex):
        case (SpvBuiltInBaseVertex):
        case (SpvBuiltInBaseInstance):
        {
            inputs = k_vertex;
            break;
        }
        case (SpvBuiltInDrawIndex):
        {
            inputs = k_vertex;
            break;
        }
        case (SpvBuiltInPrimitiveId):
        {
            inputs = k_hull | k_domain | k_geometry | k_fragment;
            outputs = k_geometry;
            break;
        }
        case (SpvBuiltInInvocationId):
        {
            inputs = k_hull | k_geometry;
            break;
        }
        case (SpvBuiltInLayer):
        case (SpvBuiltInViewportIndex):
        {
            inputs = k_fragment;
            outputs = k_vertex | k_domain | k_geometry;
            break;
        }
        case (SpvBuiltInTessCoord):
        {
            inputs = k_domain;
            break;
        }
        case (SpvBuiltInPatchVertices):
        {
            inputs = k_hull | k_domain;
            break;
        }
        case (SpvBuiltInFragCoord):
        case (SpvBuiltInPointCoord):
        case (SpvBuiltInFrontFacing):
        case (SpvBuiltInSampleId):
        case (SpvBuiltInSamplePosition):
        case (SpvBuiltInHelperInvocation):
        {
            inputs = k_fragment;
            break;
        }
        case (SpvBuiltInSampleMask):
        {
            inputs = outputs = k_fragment;
            break;
        }
        case (SpvBuiltInFragDepth):
        {
            outputs = k_fragment;
            break;
        }
        case (SpvBuiltInNumWorkgroups):
        case (SpvBuiltInWorkgroupId):
        case (SpvBuiltInLocalInvocationId):
        case (SpvBuiltInGlobalInvocationId):
        case (SpvBuiltInLocalInvocationIndex):
        {
            inputs = k_compute;
            break;
        }
        default:
        {
            return false;
        }
    }
    return ((output ? outputs : inputs) & stage) != 0u;
}

bool CStages::classify(const std::uint32_t id, const SInterfaceMetadata& details, const bool output, EValueDomain& domain) noexcept
{
    if (details.index && ((m_stage != shader::EStage::pixel) || !output))
    {
        return reject(id);
    }
    if (details.patch && (m_stage != shader::EStage::hull) && (m_stage != shader::EStage::domain))
    {
        return reject(id);
    }
    //  Built-in grouping is independent of backend Patch. DXC, for example,
    //  decorates domain-stage PrimitiveId with Patch; it remains a system value.
    if (details.builtin != UINT32_MAX)
    {
        return builtin(details, output, domain) || reject(id);
    }
    if (details.patch && !(((m_stage == shader::EStage::hull) && output) || ((m_stage == shader::EStage::domain) && !output)))
    {
        return reject(id);
    }

    const auto* const words = m_index.instruction(id);
    if (words == nullptr)
    {
        return reject(id);
    }

    const auto opcode = words[0u] & 0xffffu;
    const auto count = words[0u] >> 16u;
    if ((opcode == SpvOpTypeArray) && (count == 4u))
    {
        return classify(words[2u], details, output, domain);
    }
    if (opcode == SpvOpTypeStruct)
    {
        if (count < 3u)
        {
            return reject(id);
        }
        for (std::uint32_t i = 0u; i < count - 2u; ++i)
        {
            SInterfaceMetadata member;
            EValueDomain child{};
            if (!metadata({}, id, i, true, member))
            {
                return false;
            }
            //  Classification follows containing Patch facts, but metadata is
            //  emitted only on the exact source owner, never copied downward.
            member.patch = member.patch || details.patch;
            if (!classify(words[2u + i], member, output, child))
            {
                return false;
            }
            if ((i != 0u) && (child != domain))
            {
                return reject(id, i);
            }
            domain = child;
        }
        return true;
    }
    if (details.patch)
    {
        domain = EValueDomain::patch;
    }
    else if (m_stage == shader::EStage::pixel)
    {
        domain = output ? EValueDomain::target : EValueDomain::fragment;
    }
    else if (m_stage == shader::EStage::compute)
    {
        return reject(id);
    }
    else
    {
        domain = EValueDomain::vertex;
    }
    return true;
}

bool CStages::extent(const std::uint32_t id, std::uint32_t& value) noexcept
{
    const auto* const constant = m_index.instruction(id);
    if ((constant == nullptr) || ((constant[0u] & 0xffffu) != SpvOpConstant) || ((constant[0u] >> 16u) != 4u) ||
        (constant[3u] == 0u))
    {
        return reject(id);
    }

    const auto* const type = m_index.instruction(constant[1u]);
    if ((type == nullptr) || ((type[0u] & 0xffffu) != SpvOpTypeInt) || ((type[0u] >> 16u) != 4u) ||
        (type[2u] != 32u) || (type[3u] > 1u) || ((type[3u] == 1u) && (constant[3u] > INT32_MAX)))
    {
        return reject(id);
    }
    value = constant[3u];
    return true;
}

bool CStages::shape(const std::uint32_t id, std::uint32_t& result) noexcept
{
    for (std::uint32_t i = 0u; i < m_shapes.size(); ++i)
    {
        if (m_shapes[i].id == id) { result = i;
        return true; }
    }

    const auto* const words = m_index.instruction(id);
    if (words == nullptr)
    {
        return reject(id);
    }

    const auto opcode = words[0u] & 0xffffu;
    const auto count = words[0u] >> 16u;
    SValueShape value{ id, {} };
    result = static_cast<std::uint32_t>(m_shapes.size());
    const char* scalar = nullptr;
    if ((opcode == SpvOpTypeBool) && (count == 2u))
    {
        scalar = "b8";
    }
    if ((opcode == SpvOpTypeFloat) && (count == 3u) && (words[2u] == 32u))
    {
        scalar = "f32";
    }
    if ((opcode == SpvOpTypeInt) && (count == 4u) && (words[2u] == 32u) && (words[3u] <= 1u))
    {
        scalar = words[3u] ? "i32" : "u32";
    }
    if (scalar != nullptr)
    {
        //  Primitive and pointer names are not new schema declarations. Other
        //  annotations on such a layer have no faithful owner in this form.
        for (auto link = m_index.annotation(id); link != 0u; link = m_index.annotation_at(link).next)
        {
            if ((m_index.words()[m_index.annotation_at(link).offset] & 0xffffu) != SpvOpName)
            {
                return reject(id);
            }
        }
        std::memcpy(value.name, scalar, std::strlen(scalar) + 1u);
        return m_shapes.push_back(value);
    }

    const char* kind = nullptr;
    std::uint32_t child = 0u;
    std::uint32_t multiplicity = 1u;
    std::uint32_t members = 1u;
    if (((opcode == SpvOpTypeVector) || (opcode == SpvOpTypeMatrix)) && (count == 4u) && (words[3u] >= 2u) && (words[3u] <= 4u))
    {
        kind = (opcode == SpvOpTypeVector) ? "vector" : "matrix";
        child = words[2u];
        multiplicity = words[3u];
        const auto* const element = m_index.instruction(child);
        if ((element == nullptr) || ((opcode == SpvOpTypeMatrix) && ((element[0u] & 0xffffu) != SpvOpTypeVector)) ||
            ((opcode == SpvOpTypeVector) && ((element[0u] & 0xffffu) != SpvOpTypeBool) &&
                ((element[0u] & 0xffffu) != SpvOpTypeFloat) && ((element[0u] & 0xffffu) != SpvOpTypeInt)))
        {
            return reject(id);
        }
    }
    else if ((opcode == SpvOpTypeArray) && (count == 4u))
    {
        kind = "array";
        child = words[2u];
        if (!extent(words[3u], multiplicity))
        {
            return false;
        }
    }
    else if ((opcode == SpvOpTypeStruct) && (count >= 2u))
    {
        kind = "structure";
        members = count - 2u;
    }
    else
    {
        return reject(id);
    }
    if (members > m_limits.aggregate_members) { m_diagnostic.reason = EReason::member_limit;
    return false; }
    if (!m_builder.charge(1u, members))
    {
        return false;
    }
    ordinal_name(value.name, 's', result);
    if (!m_shapes.push_back(value))
    {
        return false;
    }

    const auto declaration = m_builder.object(m_interfaces, value.name);
    const auto entries = m_builder.array(declaration, "members");
    const auto details = m_builder.object(declaration, "decorations");
    SInterfaceMetadata annotations;
    if (!declaration.is_valid() || !entries.is_valid() || !details.is_valid() ||
        !m_builder.string(details, "rendering.shape.kind", kind) || !metadata(details, id, UINT32_MAX, false, annotations) ||
        ((opcode == SpvOpTypeMatrix) && !m_builder.string(details, "rendering.matrix.basis", "spirv")))
    {
        return false;
    }
    for (std::uint32_t i = 0u; i < members; ++i)
    {
        const bool structure = opcode == SpvOpTypeStruct;
        char name[32]{};
        ordinal_name(name, 'm', i);
        std::uint32_t target = 0u;
        if (!shape(structure ? words[2u + i] : child, target))
        {
            return false;
        }
        const auto item = m_builder.object(entries, structure ? name : ((opcode == SpvOpTypeMatrix) ? "column" : "element"));
        if (!item.is_valid() || !m_builder.string(item, "type", m_shapes[target].name) ||
            !m_builder.number(item, "count", structure ? 1u : multiplicity))
        {
            return false;
        }
        if (structure)
        {
            const auto item_details = m_builder.object(item, "decorations");
            SInterfaceMetadata member;
            if (!item_details.is_valid() || !metadata(item_details, id, i, true, member))
            {
                return false;
            }
        }
    }
    return true;
}

bool CStages::direction(SpvReflectInterfaceVariable* const* variables, const std::uint32_t count,
    const bool output, const CNodeKey members) noexcept
{
    TPodVector<SStageValue> values;
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        const auto id = variables[i]->spirv_id;
        const auto* const variable = m_index.instruction(id);
        if ((variable == nullptr) || ((variable[0u] & 0xffffu) != SpvOpVariable) || ((variable[0u] >> 16u) < 4u) ||
            (variable[3u] != (output ? SpvStorageClassOutput : SpvStorageClassInput)))
        {
            return reject(id);
        }
        const auto* const pointer = m_index.instruction(variable[1u]);
        if ((pointer == nullptr) || ((pointer[0u] & 0xffffu) != SpvOpTypePointer) || ((pointer[0u] >> 16u) != 4u) ||
            (pointer[2u] != variable[3u]))
        {
            return reject(id);
        }
        for (auto link = m_index.annotation(pointer[1u]); link != 0u; link = m_index.annotation_at(link).next)
        {
            if ((m_index.words()[m_index.annotation_at(link).offset] & 0xffffu) != SpvOpName)
            {
                return reject(pointer[1u]);
            }
        }
        SInterfaceMetadata annotations;
        SStageValue value{ id, pointer[3u], 0u, 0u, {} };
        if (!metadata({}, id, UINT32_MAX, true, annotations) || !classify(value.type, annotations, output, value.domain))
        {
            return false;
        }
        const bool wrapped = (value.domain == EValueDomain::vertex) &&
            ((m_stage == shader::EStage::hull) || (!output && ((m_stage == shader::EStage::domain) || (m_stage == shader::EStage::geometry))));
        if (wrapped)
        {
            const auto* const array = m_index.instruction(value.type);
            if ((array == nullptr) || ((array[0u] & 0xffffu) != SpvOpTypeArray) || ((array[0u] >> 16u) != 4u))
            {
                return reject(id);
            }
            value.wrapper = value.type;
            value.type = array[2u];
            if (!extent(array[3u], value.extent))
            {
                return false;
            }
        }
        if (!values.push_back(value))
        {
            return false;
        }
    }

    static const char* const groups[]{ "vertex_values", "patch_values", "fragment_values", "target_values", "system_values" };
    static const char* const domains[]{ "vertex", "patch", "fragment", "target", "system" };
    std::uint32_t group_count = 0u;
    for (std::uint32_t group = 0u; group < 5u; ++group)
    {
        std::uint32_t size = 0u;
        for (std::uint32_t i = 0u; i < values.size(); ++i)
        {
            if (static_cast<std::uint32_t>(values[i].domain) == group)
            {
                ++size;
            }
        }
        if (size == 0u)
        {
            continue;
        }
        if ((++group_count > m_limits.aggregate_members) || (size > m_limits.aggregate_members))

            {
                m_diagnostic.reason = EReason::member_limit;
                return false;
            }
        if (!m_builder.charge(1u, 1u + size))
        {
            return false;
        }
        char name[32]{};
        ordinal_name(name, 'g', group + (output ? 5u : 0u));
        const auto declaration = m_builder.object(m_interfaces, name);
        const auto entries = m_builder.array(declaration, "members");
        const auto details = m_builder.object(declaration, "decorations");
        const auto occurrence = m_builder.object(members, groups[group]);
        if (!declaration.is_valid() || !entries.is_valid() || !details.is_valid() || !occurrence.is_valid() ||
            !m_builder.string(occurrence, "type", name) || !m_builder.string(details, "rendering.interface.kind", "value_group") ||
            !m_builder.string(details, "rendering.interface.domain", domains[group]))
        {
            return false;
        }
        for (std::uint32_t i = 0u; i < values.size(); ++i)
        {
            const auto& value = values[i];
            if (static_cast<std::uint32_t>(value.domain) != group)
            {
                continue;
            }
            std::uint32_t target = 0u;
            if (!shape(value.type, target))
            {
                return false;
            }
            char wrapper[32]{};
            if (value.wrapper != 0u)
            {
                if (!m_builder.charge(1u, 0u))
                {
                    return false;
                }
                ordinal_name(wrapper, output ? 'o' : 'i', i);
                const auto associations = m_builder.group(m_types, "associations");
                const auto association = m_builder.object(associations, wrapper);
                const auto wrapper_details = m_builder.object(association, "decorations");
                SInterfaceMetadata array_annotations;
                if (!association.is_valid() || !wrapper_details.is_valid() ||
                    !m_builder.string(association, "target", m_shapes[target].name) ||
                    !m_builder.string(wrapper_details, "rendering.sequence.kind", "vertex_array") ||
                    !m_builder.decimal(wrapper_details, "rendering.sequence.extent", value.extent) ||
                    !m_builder.decimal(wrapper_details, "rendering.sequence.source_dimension", 0u) ||
                    !metadata(wrapper_details, value.wrapper, UINT32_MAX, false, array_annotations))
                {
                    return false;
                }
            }
            char item_name[32]{};
            ordinal_name(item_name, 'v', i);
            const auto item = m_builder.object(entries, item_name);
            const auto item_details = m_builder.object(item, "decorations");
            SInterfaceMetadata annotations;
            if (!item.is_valid() || !item_details.is_valid() || !m_builder.string(item, "type", value.wrapper ? wrapper : m_shapes[target].name) ||
                !metadata(item_details, value.id, UINT32_MAX, true, annotations))
            {
                return false;
            }
        }
    }
    return true;
}

EStatus CStages::run(const SpvReflectEntryPoint& entry, const CNodeKey inputs, const CNodeKey outputs) noexcept
{
    if (direction(entry.input_variables, entry.input_variable_count, false, inputs) &&
        direction(entry.output_variables, entry.output_variable_count, true, outputs))
    {
        return EStatus::success;
    }
    if (m_diagnostic.reason == EReason::unsupported_interface)
    {
        return EStatus::unsupported_feature;
    }
    return (m_diagnostic.reason == EReason::none) ? EStatus::allocation_failed : EStatus::limit_exceeded;
}

static EStatus extract(const SRequest& request, const SLimits& limits, schema::CSchema& document, SDiagnostic& diagnostic,
    const CPreflight& index, const detail::CModule& module) noexcept
{
    const auto& compilation = *request.compilation;
    auto status = EStatus::success;
    const auto* const entry = spvReflectGetEntryPoint(&module.value(), compilation.entry_point.cstring());
    static constexpr SpvExecutionModel models[]{ SpvExecutionModelVertex, SpvExecutionModelTessellationControl,
        SpvExecutionModelTessellationEvaluation, SpvExecutionModelGeometry, SpvExecutionModelFragment, SpvExecutionModelGLCompute };
    if ((entry == nullptr) || (module.value().entry_point_count != 1u) ||
        (entry->spirv_execution_model != models[static_cast<std::uint8_t>(compilation.stage)]))
    {
        diagnostic.reason = EReason::context_mismatch;
        return EStatus::invalid_input;
    }
    if ((module.value().descriptor_binding_count > limits.resources) ||
        ((static_cast<std::uint64_t>(entry->input_variable_count) + entry->output_variable_count) > limits.interface_values))
    {
        diagnostic.reason = EReason::member_limit;
        return EStatus::limit_exceeded;
    }
    if (module.value().push_constant_block_count != 0u)
    {
        diagnostic.reason = EReason::unsupported_resource;
        return EStatus::unsupported_feature;
    }
    if ((limits.declarations < 4u) || (limits.members < 3u) || (limits.aggregate_members < 3u))
    {
        diagnostic.reason = EReason::declaration_limit;
        return EStatus::limit_exceeded;
    }
    if (!document.initialise())
    {
        return EStatus::allocation_failed;
    }

    CBuilder builder{ document, limits, diagnostic, request.component_identity.length() };
    const auto types = builder.object(document.document().root(), "types");
    const auto interfaces = builder.object(types, "interfaces");
    const auto root = builder.object(interfaces, "component");
    const auto members = builder.array(root, "members");
    const auto decorations = builder.object(root, "decorations");
    static const char* const targets[]{ "vulkan_1_1", "vulkan_1_2", "vulkan_1_3" };
    static const char* const stages[]{ "vertex", "tessellation_control", "tessellation_evaluation", "geometry", "fragment", "compute" };
    bool built = types.is_valid() && interfaces.is_valid() && root.is_valid() && members.is_valid() && decorations.is_valid() &&
        builder.string(decorations, "rendering.schema.version", "1") &&
        builder.string(decorations, "rendering.interface.kind", "component") &&
        builder.string(decorations, "rendering.shader.stage", stages[static_cast<std::uint8_t>(compilation.stage)]) &&
        builder.string(decorations, "rendering.shader.entry_point", compilation.entry_point.cstring()) &&
        builder.string(decorations, "rendering.shader.target", targets[static_cast<std::uint8_t>(compilation.options.target)]);
    static const char* const groups[]{ "resources", "inputs", "outputs" };
    CNodeKey group_members[3];
    std::uint32_t group_index = 0u;
    for (const auto* const group : groups)
    {
        if (!built)
        {
            break;
        }
        const auto occurrence = builder.object(members, group);
        const auto declaration = builder.object(interfaces, group);
        const auto empty = builder.array(declaration, "members");
        group_members[group_index++] = empty;
        const auto detail = builder.object(declaration, "decorations");
        built = occurrence.is_valid() && declaration.is_valid() && empty.is_valid() && detail.is_valid() &&
            builder.string(occurrence, "type", group) && builder.string(detail, "rendering.interface.kind", group);
    }
    if (built)
    {
        CResources resources{ builder, index, limits, diagnostic, types, interfaces, group_members[0u] };
        status = resources.run(module.value());
        if (status != EStatus::success)
        {
            return status;
        }
    }
    if (!built)
    {
        return (diagnostic.reason == EReason::none) ? EStatus::allocation_failed : EStatus::limit_exceeded;
    }
    status = execution_modes(builder, index, *entry, compilation.stage, decorations, diagnostic);
    if (status != EStatus::success)
    {
        return status;
    }

    CStages stage_values{ builder, index, limits, diagnostic, compilation.stage, types, interfaces };
    status = stage_values.run(*entry, group_members[1u], group_members[2u]);
    if (status != EStatus::success)
    {
        return status;
    }
    schema::SSchemaDiagnostic error;
    if (!document.validate(error))
    {
        diagnostic.reason = EReason::schema_failure;
        diagnostic.schema_stage = static_cast<std::uint32_t>(error.stage);
        diagnostic.schema_reason = static_cast<std::uint32_t>(error.reason);
        diagnostic.types_stage = static_cast<std::uint32_t>(error.types.stage);
        diagnostic.types_reason = static_cast<std::uint32_t>(error.types.reason);
        return ((error.reason == schema::ESchemaReason::allocation_failed) || (error.types.reason == schema::EReason::allocation_failed)) ?
            EStatus::allocation_failed : EStatus::validation_failed;
    }
    return EStatus::success;
}

static EStatus build(const SRequest& request, const SLimits& limits, schema::CSchema& document,
    SDiagnostic& diagnostic, detail::CModule& module) noexcept
{
    const auto artifact = request.compilation->shader.view();
    CPreflight index{ artifact, limits, diagnostic };
    const auto admission = index.run();
    if (admission != EStatus::success)
    {
        return admission;
    }
    bool size_rejected = false;
    const auto reflected = module.create(artifact.size(), artifact.data(), size_rejected, request.component_identity);
    diagnostic.library_result = static_cast<std::int32_t>(reflected);
    if (reflected != SPV_REFLECT_RESULT_SUCCESS)
    {
        diagnostic.reason = (reflected == SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED) ? EReason::allocation : EReason::library_failure;
        if (size_rejected)
        {
            diagnostic.reason = EReason::allocation_size_limit;
            return EStatus::limit_exceeded;
        }
        return (reflected == SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED) ? EStatus::allocation_failed : EStatus::reflection_failed;
    }

    const auto status = extract(request, limits, document, diagnostic, index, module);
    module.close();
    return status;
}

CReflection::~CReflection() noexcept
{
    if (m_module_storage.is_ready())
    {
        auto* const module = reinterpret_cast<detail::CModule*>(m_module_storage.data());
        module->~CModule();
    }
}

bool CReflection::prepare() noexcept
{
    if (m_module_storage.is_ready())
    {
        return false;
    }
    if (!m_module_storage.resize(sizeof(detail::CModule), alignof(detail::CModule)))
    {
        return false;
    }
    ::new (static_cast<void*>(m_module_storage.data())) detail::CModule;
    return true;
}

bool CReflection::ready() const noexcept
{
    return m_module_storage.is_ready() && !m_started;
}

EStatus CReflection::run(const SRequest& request, const SLimits& limits, CResult& result) noexcept
{
    if (!ready())
    {
        return EStatus::invalid_input;
    }
    m_started = true;
    auto& module = *reinterpret_cast<detail::CModule*>(m_module_storage.data());
    return reflect_module(request, limits, result, module);
}

EStatus reflect(const SRequest& request, const SLimits& limits, CResult& result) noexcept
{
    //  Direct synchronous callers own their temporary state on this thread.
    //  Dispatched jobs instead lend the instance prepared by their requester.
    detail::CModule module;
    return CReflection::reflect_module(request, limits, result, module);
}

EStatus CReflection::reflect_module(const SRequest& request, const SLimits& limits,
    CResult& result, detail::CModule& module) noexcept
{
    if (result.m_status != EStatus::not_executed)
    {
        return EStatus::invalid_input;
    }
    bool identity_admitted = false;
    bool identity_attempted = false;
    const auto finish = [&](const EStatus status, const EReason reason) noexcept
    {
        result.m_status = status;
        result.m_diagnostic.reason = reason;
        if (identity_admitted && !identity_attempted)
        {
            identity_attempted = true;
            //  A secondary retention failure must preserve the primary reason.
            (void)result.m_identity.set(request.component_identity.string(), request.component_identity.length());
        }
        if (identity_admitted && (status != EStatus::success))
        {
            const char* message = "Shader reflection failed; inspect the structured diagnostic reason.";
            if (status == EStatus::allocation_failed)
            {
                message = "Shader reflection could not allocate storage.";
            }
            if (status == EStatus::limit_exceeded)
            {
                message = "Shader reflection exceeded a configured service limit.";
            }
            if (status == EStatus::unsupported_feature)
            {
                message = "Shader reflection encountered an unsupported feature.";
            }
            const auto length = std::strlen(message);
            const auto retained = std::min(length, limits.diagnostic_bytes);
            result.m_truncated = retained < length;
            if ((retained != 0u) && !result.m_diagnostics.set(message, retained))
            {
                result.m_truncated = true;
            }
        }
        result.m_statistics.retained_bytes = sizeof(CResult) + result.m_schema.document().memory_attribution().allocation_size +
            result.m_identity.memory_attribution().allocation_size + result.m_diagnostics.memory_attribution().allocation_size;
        if (result.m_statistics.retained_bytes > limits.retained_bytes)
        {
            result.m_schema.clear();
            result.m_identity.deallocate();
            result.m_diagnostics.deallocate();
            result.m_statistics.retained_bytes = sizeof(CResult);
            result.m_truncated = true;
            if (status == EStatus::success)
            {
                result.m_status = EStatus::limit_exceeded;
                result.m_diagnostic.reason = EReason::retained_limit;
            }
        }
        return result.m_status;
    };
    const auto fail = finish;
    if ((request.component_identity.length() > limits.string_bytes) || (request.component_identity.length() > limits.strings_bytes))
    {
        return fail(EStatus::limit_exceeded, EReason::string_limit);
    }
    if (!valid_identity(request.component_identity))
    {
        return fail(EStatus::invalid_input, EReason::invalid_request);
    }
    identity_admitted = true;
    if (request.compilation == nullptr)
    {
        return fail(EStatus::invalid_input, EReason::invalid_request);
    }

    const auto& compilation = *request.compilation;
    if (compilation.status != shader::EStatus::success)
    {
        return fail(EStatus::blocked_by_compilation, EReason::compilation_failed);
    }

    const auto artifact = compilation.shader.view();
    if ((compilation.stage != compilation.shader.stage) || (compilation.options.target != compilation.shader.target) ||
        (static_cast<std::uint8_t>(compilation.stage) > static_cast<std::uint8_t>(shader::EStage::compute)) ||
        (static_cast<std::uint8_t>(compilation.options.target) > static_cast<std::uint8_t>(shader::ETarget::vulkan_1_3)) ||
        (compilation.entry_point.length() == 0u) || !artifact.is_ready() || ((artifact.size() % 4u) != 0u) ||
        ((reinterpret_cast<std::uintptr_t>(artifact.data()) % alignof(std::uint32_t)) != 0u))
    {
        return fail(EStatus::invalid_input, EReason::context_mismatch);
    }
    if (compilation.entry_point.length() > limits.string_bytes)
    {
        return fail(EStatus::limit_exceeded, EReason::string_limit);
    }
    if (!valid_identity(compilation.entry_point.view()) || (compilation.options.features != shader::EFeatures::conventional_32_bit))
    {
        return fail(EStatus::invalid_input, EReason::context_mismatch);
    }
    if ((artifact.size() > limits.artifact_bytes) || (artifact.size() > memory::k_byte_size_ceiling))
    {
        return fail(EStatus::limit_exceeded, EReason::artifact_limit);
    }
    if (artifact.size() < 20u)
    {
        return fail(EStatus::invalid_input, EReason::malformed_artifact);
    }

    const auto* const words = reinterpret_cast<const std::uint32_t*>(artifact.data());
    std::size_t arena_capacity = 0u;
    if (!detail::SAllocationOwner::capacity_for(artifact.size(), arena_capacity))
    {
        return fail(EStatus::limit_exceeded, EReason::allocation_size_limit);
    }
    //  Arena reservation plus approximate index/document expansion.
    //  This is a service admission estimate, not a measurement of peak memory.
    result.m_statistics.estimated_working_bytes = arena_capacity + (static_cast<std::uint64_t>(artifact.size()) * 8u) +
        (static_cast<std::uint64_t>(words[3]) * sizeof(SNode)) + (64u * 1024u);
    if (result.m_statistics.estimated_working_bytes > limits.estimated_working_bytes)
    {
        return fail(EStatus::limit_exceeded, EReason::working_limit);
    }
    schema::CSchema staging;
    identity_attempted = true;
    if (!result.m_identity.set(request.component_identity.string(), request.component_identity.length()))
    {
        return fail(EStatus::allocation_failed, EReason::allocation);
    }
    result.m_status = build(request, limits, staging, result.m_diagnostic, module);
    result.m_statistics.reflection_arena_capacity = module.arena_capacity();
    result.m_statistics.reflection_arena_consumed = module.arena_consumed();
    if (result.m_status == EStatus::success)
    {
        result.m_schema = std::move(staging);
    }
    else
    {
        staging.clear();
        if (result.m_diagnostic.reason == EReason::none)
        {
            result.m_diagnostic.reason = EReason::allocation;
        }
    }
    return finish(result.m_status, result.m_diagnostic.reason);
}

}   //  namespace rendering::shader_reflection
