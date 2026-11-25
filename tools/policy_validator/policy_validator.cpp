
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    policy_validator.cpp
//  Primary implementation: OpenAI tools
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    19 Aug 26

#include "policy_validator.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace morphic::policy
{

namespace fs = std::filesystem;

enum class ESeverity
{
    warning,
    error
};

struct SDiagnostic
{
    fs::path path;
    std::size_t line{ 1u };
    std::size_t column{ 1u };
    ESeverity severity{ ESeverity::error };
    std::string rule;
    std::string message;
    bool suppressed{ false };
};

struct SToken
{
    std::string text;
    std::size_t line{ 1u };
    std::size_t column{ 1u };
};

struct SDirective
{
    std::string text;
    std::size_t line{ 1u };
    std::size_t column{ 1u };
};

struct SDecoration
{
    std::string rule;
    std::string reason;
    std::size_t line{ 1u };
    std::size_t column{ 1u };
    bool malformed{ false };
};

struct SLexedFile
{
    std::vector<SToken> tokens;
    std::vector<SDirective> directives;
    std::vector<SDecoration> decorations;
};

struct SSourceText
{
    std::string text;
    std::vector<std::size_t> original_offsets;
    std::vector<std::size_t> line_starts{ 0u };
    std::vector<std::size_t> original_line_starts{ 0u };
};

struct SScanRoot
{
    fs::path path;
    std::string scope;
};

struct SIncludeRule
{
    std::string operand;
    std::string scope;
    std::string match_kind;
    std::string path;
    std::string classification;
    std::size_t line;
};

struct SMacroRule
{
    std::string name;
    std::string diagnostic_rule;
    std::string match_kind;
    std::string path;
    std::size_t line;
};

struct SPolicyRecord
{
    std::vector<std::string> fields;
    std::size_t line;
};

struct SPolicyUsage
{
    std::unordered_set<std::size_t> includes;
    std::unordered_set<std::size_t> macros;
    std::unordered_set<std::string> placements;
    std::unordered_set<std::string> allocations;
    std::unordered_set<std::string> memory_tokens;
};

struct SProjectRule
{
    fs::path path;
    std::string scope;
};

struct SPolicy
{
    std::vector<SPolicyRecord> records;
    std::vector<SScanRoot> scan_roots;
    std::vector<SIncludeRule> include_rules;
    std::vector<SMacroRule> macro_rules;
    std::vector<SProjectRule> projects;
    std::unordered_set<std::string> placement_paths;
    std::unordered_set<std::string> allocation_paths;
    std::unordered_set<std::string> memory_token_paths;
};

struct SProjectConfiguration
{
    std::string configuration;
    std::string platform;
};

struct SCompilerGroup
{
    std::string condition;
    std::optional<std::string> language_standard;
    std::optional<std::string> exception_handling;
    std::optional<std::string> additional_options;
    std::size_t line{ 1u };
    bool condition_understood{ true };
};

[[nodiscard]] static bool is_identifier_start(const char value)
{
    return std::isalpha(static_cast<unsigned char>(value)) != 0 || value == '_';
}

[[nodiscard]] static bool is_identifier_continue(const char value)
{
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

[[nodiscard]] static std::string trim(std::string value)
{
    const auto not_space = [](const unsigned char character)
    {
        return std::isspace(character) == 0;
    };
    const auto begin = std::find_if(value.begin(), value.end(), not_space);
    const auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return (begin < end) ? std::string(begin, end) : std::string{};
}

[[nodiscard]] static std::string lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character)
    {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] static std::string normalise_relative_path(const fs::path& path)
{
    std::string result = path.lexically_normal().generic_string();
    while (result.rfind("./", 0u) == 0u)
    {
        result.erase(0u, 2u);
    }
    return result;
}

[[nodiscard]] static std::string read_file(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream.good() && !stream.eof())
    {
        throw std::runtime_error("cannot read " + path.string());
    }
    return contents.str();
}

[[nodiscard]] static std::vector<std::string> split_fields(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t begin = 0u;
    for (;;)
    {
        const std::size_t separator = line.find('|', begin);
        fields.push_back(trim(line.substr(begin, separator - begin)));
        if (separator == std::string::npos)
        {
            break;
        }
        begin = separator + 1u;
    }
    return fields;
}

[[nodiscard]] static bool path_matches(
    const std::string& relative_path,
    const std::string& match_kind,
    const std::string& configured_path)
{
    if (match_kind == "any")
    {
        return true;
    }
    if (match_kind == "exact")
    {
        return relative_path == configured_path;
    }
    if (match_kind == "prefix")
    {
        return relative_path.rfind(configured_path, 0u) == 0u;
    }
    return false;
}

[[nodiscard]] static SPolicy load_policy(const fs::path& path)
{
    SPolicy policy;
    std::istringstream input(read_file(path));
    std::string line;
    std::size_t line_number = 0u;
    bool version_seen = false;
    while (std::getline(input, line))
    {
        ++line_number;
        line = trim(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        const std::vector<std::string> fields = split_fields(line);
        const auto invalid = [&]()
        {
            throw std::invalid_argument(
                path.string() + '(' + std::to_string(line_number) + "): invalid policy record");
        };

        if (std::any_of(fields.begin(), fields.end(), [](const std::string& field) { return field.empty(); }))
        {
            invalid();
        }
        for (const SPolicyRecord& record : policy.records)
        {
            const std::size_t key_size = (fields[0] == "include") ? std::min(std::size_t{ 5u }, fields.size()) :
                ((fields[0] == "memory-token") ? std::min(std::size_t{ 2u }, fields.size()) : fields.size());
            if ((record.fields.size() == fields.size()) &&
                std::equal(fields.begin(), (fields.begin() + key_size), record.fields.begin()))
            {
                invalid();
            }
        }

        if ((fields[0] == "version") && (fields.size() == 2u))
        {
            if (version_seen || (fields[1] != "1"))
            {
                throw std::invalid_argument("unsupported policy version: " + fields[1]);
            }
            version_seen = true;
        }
        else if ((fields[0] == "scan") && (fields.size() == 3u))
        {
            if ((fields[2] != "engine") && (fields[2] != "tests"))
            {
                invalid();
            }
            policy.scan_roots.push_back({ fields[1], fields[2] });
        }
        else if ((fields[0] == "include") && (fields.size() == 6u))
        {
            if ((fields[2] != "engine") && (fields[2] != "tests") && (fields[2] != "all"))
            {
                invalid();
            }
            policy.include_rules.push_back({
                fields[1], fields[2], fields[3], fields[4], fields[5], line_number });
        }
        else if ((fields[0] == "placement") && (fields.size() == 2u))
        {
            policy.placement_paths.insert(fields[1]);
        }
        else if ((fields[0] == "allocation") && (fields.size() == 2u))
        {
            policy.allocation_paths.insert(fields[1]);
        }
        else if ((fields[0] == "memory-token") && (fields.size() == 3u))
        {
            policy.memory_token_paths.insert(fields[1]);
        }
        else if ((fields[0] == "macro") && (fields.size() == 5u))
        {
            if ((fields[2] != "GID001") && (fields[2] != "GID002"))
            {
                invalid();
            }
            if (!is_identifier_start(fields[1][0]) ||
                !std::all_of(fields[1].begin(), fields[1].end(), is_identifier_continue))
            {
                invalid();
            }
            for (const SMacroRule& rule : policy.macro_rules)
            {
                if ((rule.name == fields[1]) && (rule.diagnostic_rule != fields[2]))
                {
                    invalid();
                }
            }
            policy.macro_rules.push_back({
                fields[1], fields[2], fields[3], fields[4], line_number });
        }
        else if ((fields[0] == "project") && (fields.size() == 3u))
        {
            if ((fields[2] != "engine") && (fields[2] != "tool"))
            {
                invalid();
            }
            for (const SProjectRule& project : policy.projects)
            {
                if (project.path == fs::path(fields[1])) invalid();
            }
            policy.projects.push_back({ fields[1], fields[2] });
        }
        else
        {
            invalid();
        }
        const bool permission = (fields[0] == "include") || (fields[0] == "macro");
        if (permission && (fields[3] != "any") && (fields[3] != "exact") && (fields[3] != "prefix"))
        {
            invalid();
        }
        if (fields[0] != "version")
        {
            const std::string& configured = fields[permission ? 4u : 1u];
            if (permission && (fields[3] == "any"))
            {
                if (configured != "*") invalid();
            }
            else
            {
                const fs::path target(configured);
                if (target.has_root_directory() || target.has_root_name() ||
                    (configured.find_first_of("\\:*?") != std::string::npos) ||
                    (normalise_relative_path(target) != configured) || (configured == "."))
                {
                    invalid();
                }
                for (const fs::path& part : target)
                {
                    if ((part == "..") || (part == "."))
                    {
                        invalid();
                    }
                }
                const bool prefix = permission && (fields[3] == "prefix");
                if (prefix != (configured.back() == '/'))
                {
                    invalid();
                }
            }
        }
        policy.records.push_back({ fields, line_number });
    }
    if (!version_seen || policy.scan_roots.empty() || policy.projects.empty())
    {
        throw std::invalid_argument("policy is missing required version, scan, or project records");
    }
    return policy;
}

[[nodiscard]] static bool begins_raw_string(const std::string& source, const std::size_t index)
{
    static const std::string prefixes[] = { "R\"", "u8R\"", "uR\"", "UR\"", "LR\"" };
    if ((index != 0u) && is_identifier_continue(source[index - 1u]))
    {
        return false;
    }
    for (const std::string& prefix : prefixes)
    {
        if (source.compare(index, prefix.size(), prefix) == 0)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] static std::size_t raw_prefix_length(const std::string& source, const std::size_t index)
{
    if (source.compare(index, 4u, "u8R\"") == 0)
    {
        return 4u;
    }
    if ((source.compare(index, 3u, "uR\"") == 0) ||
        (source.compare(index, 3u, "UR\"") == 0) ||
        (source.compare(index, 3u, "LR\"") == 0))
    {
        return 3u;
    }
    return 2u;
}

// Apply physical line splicing before recognizing comments or identifiers, while
// retaining original diagnostic locations and the contents of raw literals.
[[nodiscard]] static SSourceText splice_source(const std::string& source)
{
    SSourceText result;
    result.text.reserve(source.size());
    result.original_offsets.reserve(source.size() + 1u);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        if (source[index] == '\n') result.original_line_starts.push_back(index + 1u);
    }
    char quote = 0;
    char previous = 0;
    bool escaped = false;
    bool line_comment = false;
    bool block_comment = false;
    const auto append = [&](const std::size_t index)
    {
        result.original_offsets.push_back(index);
        result.text += source[index];
        if (source[index] == '\n') result.line_starts.push_back(result.text.size());
    };
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        const char character = source[index];
        if ((character == '\\') && ((index + 1u) < source.size()))
        {
            if (source[index + 1u] == '\n')
            {
                ++index;
                continue;
            }
            if (((index + 2u) < source.size()) && (source[index + 1u] == '\r') && (source[index + 2u] == '\n'))
            {
                index += 2u;
                continue;
            }
        }
        if (line_comment)
        {
            if (character == '\n') line_comment = false;
        }
        else if (block_comment)
        {
            if ((previous == '*') && (character == '/'))
            {
                block_comment = false;
                previous = 0;
                append(index);
                continue;
            }
        }
        else if (quote != 0)
        {
            if (!escaped && (character == quote)) quote = 0;
            escaped = !escaped && (character == '\\');
        }
        else if ((previous == '/') && (character == '/')) line_comment = true;
        else if ((previous == '/') && (character == '*')) block_comment = true;
        else if ((character == '"') || (character == '\''))
        {
            bool raw = false;
            if (character == '"')
            {
                const std::string probe = result.text.substr((result.text.size() > 5u) ? (result.text.size() - 5u) : 0u) + character;
                const std::size_t begin = (probe.size() > 4u) ? (probe.size() - 4u) : 0u;
                for (std::size_t cursor = begin; (cursor + 1u) < probe.size(); ++cursor)
                {
                    if (begins_raw_string(probe, cursor)) raw = true;
                }
            }
            if (raw)
            {
                const std::size_t open = source.find('(', index + 1u);
                if ((open != std::string::npos) && ((open - index - 1u) <= 16u))
                {
                    const std::string delimiter = source.substr((index + 1u), (open - index - 1u));
                    const std::string terminator = ')' + delimiter + '"';
                    const std::size_t close = source.find(terminator, open + 1u);
                    const std::size_t end = (close == std::string::npos) ? source.size() : (close + terminator.size());
                    while (index < end)
                    {
                        append(index);
                        ++index;
                    }
                    --index;
                    previous = 0;
                    continue;
                }
            }
            quote = character;
            escaped = false;
        }
        append(index);
        previous = character;
    }
    result.original_offsets.push_back(source.size());
    return result;
}

static void restore_position(const SSourceText& source, std::size_t& line, std::size_t& column)
{
    const std::size_t logical = source.line_starts.at(line - 1u) + column - 1u;
    const std::size_t original = source.original_offsets.at(logical);
    const auto next_line = std::upper_bound(source.original_line_starts.begin(), source.original_line_starts.end(), original);
    line = static_cast<std::size_t>(next_line - source.original_line_starts.begin());
    column = original - source.original_line_starts[line - 1u] + 1u;
}

static void update_position(
    const std::string& source,
    const std::size_t begin,
    const std::size_t end,
    std::size_t& line,
    std::size_t& line_start)
{
    for (std::size_t index = begin; index < end; ++index)
    {
        if (source[index] == '\n')
        {
            ++line;
            line_start = index + 1u;
        }
    }
}

[[nodiscard]] static std::optional<SDecoration> parse_decoration(
    const std::string& comment,
    const std::size_t line,
    const std::size_t column)
{
    const std::string text = trim(comment);
    static const std::string prefix = "morphic-policy:";
    if (text.rfind(prefix, 0u) != 0u)
    {
        return std::nullopt;
    }

    SDecoration decoration;
    decoration.line = line;
    decoration.column = column;
    const std::string command = trim(text.substr(prefix.size()));
    static const std::string suppress = "suppress-next-line ";
    if (command.rfind(suppress, 0u) != 0u)
    {
        decoration.malformed = true;
        return decoration;
    }

    const std::string remainder = command.substr(suppress.size());
    const std::size_t separator = remainder.find(' ');
    if (separator == std::string::npos)
    {
        decoration.malformed = true;
        return decoration;
    }
    decoration.rule = trim(remainder.substr(0u, separator));
    const std::string reason_field = trim(remainder.substr(separator + 1u));
    static const std::string reason_prefix = "reason=\"";
    if ((reason_field.rfind(reason_prefix, 0u) != 0u) ||
        (reason_field.size() <= reason_prefix.size()) ||
        (reason_field.back() != '"'))
    {
        decoration.malformed = true;
        return decoration;
    }
    decoration.reason = reason_field.substr(
        reason_prefix.size(), reason_field.size() - reason_prefix.size() - 1u);
    decoration.malformed = decoration.rule.empty() || trim(decoration.reason).empty();
    return decoration;
}

[[nodiscard]] static std::size_t directive_end(const std::string& source, const std::size_t begin)
{
    for (std::size_t index = begin; index < source.size(); ++index)
    {
        if (source[index] == '\n') return index + 1u;
        if (source.compare(index, 2u, "//") == 0)
        {
            const std::size_t end = source.find('\n', index);
            return (end == std::string::npos) ? source.size() : (end + 1u);
        }
        if (source.compare(index, 2u, "/*") == 0)
        {
            const std::size_t end = source.find("*/", index + 2u);
            if (end == std::string::npos) return source.size();
            index = end + 1u;
        }
        else if (begins_raw_string(source, index))
        {
            const std::size_t delimiter_begin = index + raw_prefix_length(source, index);
            const std::size_t open = source.find('(', delimiter_begin);
            if ((open == std::string::npos) || ((open - delimiter_begin) > 16u))
            {
                return source.size();
            }
            const std::string terminator = ')' + source.substr(delimiter_begin, open - delimiter_begin) + '"';
            const std::size_t end = source.find(terminator, open + 1u);
            if (end == std::string::npos) return source.size();
            index = end + terminator.size() - 1u;
        }
        else if ((source[index] == '"') || (source[index] == '\''))
        {
            const char quote = source[index];
            for (++index; index < source.size(); ++index)
            {
                if (source[index] == quote) break;
                if (source[index] == '\n') return index + 1u;
                if ((source[index] == '\\') && ((index + 1u) < source.size())) ++index;
            }
        }
    }
    return source.size();
}

[[nodiscard]] static SLexedFile lex_source(const std::string& source)
{
    SLexedFile result;
    std::size_t index = 0u;
    std::size_t line = 1u;
    std::size_t line_start = 0u;
    bool only_whitespace_on_line = true;

    while (index < source.size())
    {
        const char character = source[index];
        if (character == '\n')
        {
            ++line;
            ++index;
            line_start = index;
            only_whitespace_on_line = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(character)) != 0)
        {
            ++index;
            continue;
        }

        if (only_whitespace_on_line && character == '#')
        {
            const std::size_t begin = index;
            const std::size_t directive_line = line;
            const std::size_t directive_column = index - line_start + 1u;
            index = directive_end(source, index);
            update_position(source, begin, index, line, line_start);
            result.directives.push_back({
                source.substr(begin, index - begin), directive_line, directive_column });
            only_whitespace_on_line = true;
            continue;
        }
        if ((character == '/') && ((index + 1u) < source.size()) && source[index + 1u] == '/')
        {
            const std::size_t comment_line = line;
            const std::size_t comment_column = index - line_start + 1u;
            const std::size_t begin = index + 2u;
            const std::size_t end = source.find('\n', begin);
            const std::size_t actual_end = (end == std::string::npos) ? source.size() : end;
            if (const auto decoration = parse_decoration(
                    source.substr(begin, actual_end - begin), comment_line, comment_column))
            {
                result.decorations.push_back(*decoration);
            }
            index = actual_end;
            continue;
        }
        if ((character == '/') && ((index + 1u) < source.size()) && source[index + 1u] == '*')
        {
            const std::size_t end = source.find("*/", index + 2u);
            const std::size_t actual_end = (end == std::string::npos) ? source.size() : end + 2u;
            update_position(source, index, actual_end, line, line_start);
            index = actual_end;
            continue;
        }
        only_whitespace_on_line = false;
        if (begins_raw_string(source, index))
        {
            const std::size_t prefix_length = raw_prefix_length(source, index);
            const std::size_t delimiter_begin = index + prefix_length;
            const std::size_t open = source.find('(', delimiter_begin);
            if ((open == std::string::npos) || ((open - delimiter_begin) > 16u))
            {
                ++index;
                continue;
            }
            const std::string delimiter = source.substr(delimiter_begin, open - delimiter_begin);
            const std::string terminator = ')' + delimiter + '"';
            const std::size_t close = source.find(terminator, open + 1u);
            const std::size_t actual_end = (close == std::string::npos)
                ? source.size()
                : close + terminator.size();
            update_position(source, index, actual_end, line, line_start);
            index = actual_end;
            continue;
        }
        if ((character == '"') || (character == '\''))
        {
            const char terminator = character;
            const std::size_t begin = index++;
            bool escaped = false;
            while (index < source.size())
            {
                const char current = source[index++];
                if (current == '\n')
                {
                    ++line;
                    line_start = index;
                }
                if (!escaped && current == terminator)
                {
                    break;
                }
                if (!escaped && current == '\\')
                {
                    escaped = true;
                }
                else
                {
                    escaped = false;
                }
            }
            (void)begin;
            continue;
        }
        if (is_identifier_start(character))
        {
            const std::size_t begin = index++;
            while ((index < source.size()) && is_identifier_continue(source[index]))
            {
                ++index;
            }
            result.tokens.push_back({
                source.substr(begin, index - begin), line, begin - line_start + 1u });
            continue;
        }

        result.tokens.push_back({ std::string(1u, character), line, index - line_start + 1u });
        ++index;
    }
    return result;
}

[[nodiscard]] static std::vector<SToken> directive_tokens(const SDirective& directive)
{
    std::string definition = directive.text;
    definition[0] = ' ';
    std::vector<SToken> tokens = lex_source(definition).tokens;
    for (SToken& token : tokens)
    {
        if (token.line == 1u) token.column += directive.column - 1u;
        token.line += directive.line - 1u;
    }
    return tokens;
}

[[nodiscard]] static bool is_source_file(const fs::path& path)
{
    static const std::set<std::string> extensions = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".inl", ".def"
    };
    return extensions.find(lower_copy(path.extension().string())) != extensions.end();
}

[[nodiscard]] static bool include_is_allowed(
    const SPolicy& policy,
    const std::string& operand,
    const std::string& scope,
    const std::string& relative_path,
    SPolicyUsage& usage)
{
    bool allowed = false;
    for (const SIncludeRule& rule : policy.include_rules)
    {
        if ((rule.operand == operand) &&
            ((rule.scope == "all") || (rule.scope == scope)) &&
            path_matches(relative_path, rule.match_kind, rule.path))
        {
            usage.includes.insert(rule.line);
            allowed = true;
        }
    }
    return allowed;
}

[[nodiscard]] static bool macro_is_known(const SPolicy& policy, const std::string& name)
{
    return std::any_of(policy.macro_rules.begin(), policy.macro_rules.end(),
        [&](const SMacroRule& rule) { return rule.name == name; });
}

[[nodiscard]] static bool macro_is_allowed(
    const SPolicy& policy,
    const std::string& name,
    const std::string& relative_path,
    SPolicyUsage& usage)
{
    bool allowed = false;
    for (const SMacroRule& rule : policy.macro_rules)
    {
        if ((rule.name == name) && path_matches(relative_path, rule.match_kind, rule.path))
        {
            usage.macros.insert(rule.line);
            allowed = true;
        }
    }
    return allowed;
}

[[nodiscard]] static std::string macro_diagnostic_rule(
    const SPolicy& policy,
    const std::string& name)
{
    const auto found = std::find_if(policy.macro_rules.begin(), policy.macro_rules.end(),
        [&](const SMacroRule& rule) { return rule.name == name; });
    return (found != policy.macro_rules.end()) ? found->diagnostic_rule : "GID001";
}

static void add_diagnostic(
    std::vector<SDiagnostic>& diagnostics,
    const fs::path& path,
    const std::size_t line,
    const std::size_t column,
    const ESeverity severity,
    std::string rule,
    std::string message)
{
    diagnostics.push_back({ path, line, column, severity, std::move(rule), std::move(message), false });
}

[[nodiscard]] static bool permission_covers(
    const std::string& broad_kind, const std::string& broad_path,
    const std::string& narrow_kind, const std::string& narrow_path)
{
    if (broad_kind == "any") return true;
    if (narrow_kind == "any") return false;
    if (broad_kind == "prefix")
    {
        return path_matches(narrow_path, broad_kind, broad_path);
    }
    return (narrow_kind == "exact") && (broad_path == narrow_path);
}

[[nodiscard]] static bool is_within_repository(const fs::path& root, const fs::path& path)
{
    std::error_code error;
    const fs::path canonical_root = fs::weakly_canonical(root, error);
    if (error) return false;
    const fs::path canonical_path = fs::weakly_canonical(path, error);
    if (error) return false;
    std::string prefix = canonical_root.generic_string();
    if (prefix.back() != '/') prefix += '/';
    std::string target = canonical_path.generic_string();
#ifdef _WIN32
    prefix = lower_copy(prefix);
    target = lower_copy(target);
#endif
    return target.rfind(prefix, 0u) == 0u;
}

static void audit_policy_targets(
    const fs::path& root, const SPolicy& policy, std::vector<SDiagnostic>& diagnostics)
{
    for (const SPolicyRecord& record : policy.records)
    {
        const std::vector<std::string>& fields = record.fields;
        if (fields[0] == "version") continue;
        const bool permission = (fields[0] == "include") || (fields[0] == "macro");
        if (permission && (fields[3] == "any"))
        {
            continue;
        }
        const std::string& path = fields[permission ? 4u : 1u];
        const bool directory = (fields[0] == "scan") || (permission && (fields[3] == "prefix"));
        const fs::path target = root / path;
        std::error_code error;
        const bool exists = directory ? fs::is_directory(target, error) : fs::is_regular_file(target, error);
        if (!exists || error || !is_within_repository(root, target))
        {
            add_diagnostic(diagnostics, "policy/morphic_policy.cfg", record.line, 1u,
                ESeverity::error, ((fields[0] == "scan") ? "SRC001" : "CFG001"),
                "configured target must exist inside the repository: " + path);
            continue;
        }
        if ((fields[0] == "project") || (fields[0] == "scan"))
        {
            continue;
        }
        const bool scanned = std::any_of(policy.scan_roots.begin(), policy.scan_roots.end(),
            [&](const SScanRoot& scan)
            {
                const std::string prefix = normalise_relative_path(scan.path) + '/';
                const bool scope_matches = (fields[0] == "memory-token") ? (scan.scope == "tests") :
                    ((fields[0] != "include") || (fields[2] == "all") || (fields[2] == scan.scope));
                return scope_matches && ((path.rfind(prefix, 0u) == 0u) ||
                    (directory && (prefix.rfind(path, 0u) == 0u)));
            });
        if (!scanned || (!directory && !is_source_file(target)))
        {
            add_diagnostic(diagnostics, "policy/morphic_policy.cfg", record.line, 1u,
                ESeverity::error, "CFG001", "permission target is outside its configured source scope: " + path);
        }
    }
    for (std::size_t index = 0u; index < policy.scan_roots.size(); ++index)
    {
        std::error_code error;
        const fs::path first_path = fs::weakly_canonical(root / policy.scan_roots[index].path, error);
        if (error) continue;
        std::string first = normalise_relative_path(first_path) + '/';
#ifdef _WIN32
        first = lower_copy(first);
#endif
        for (std::size_t other = index + 1u; other < policy.scan_roots.size(); ++other)
        {
            const fs::path second_path = fs::weakly_canonical(root / policy.scan_roots[other].path, error);
            if (error) continue;
            std::string second = normalise_relative_path(second_path) + '/';
#ifdef _WIN32
            second = lower_copy(second);
#endif
            if ((first.rfind(second, 0u) == 0u) || (second.rfind(first, 0u) == 0u))
            {
                add_diagnostic(diagnostics, "policy/morphic_policy.cfg", 1u, 1u,
                    ESeverity::error, "CFG001", "overlapping source roots: " + first + " and " + second);
            }
        }
    }
    for (std::size_t index = 0u; index < policy.projects.size(); ++index)
    {
        for (std::size_t other = index + 1u; other < policy.projects.size(); ++other)
        {
            std::error_code error;
            if (fs::equivalent((root / policy.projects[index].path), (root / policy.projects[other].path), error) && !error)
            {
                add_diagnostic(diagnostics, "policy/morphic_policy.cfg", 1u, 1u,
                    ESeverity::error, "CFG001", "multiple classifications refer to the same project: " +
                        policy.projects[index].path.generic_string() + " and " + policy.projects[other].path.generic_string());
            }
        }
    }
}

static void audit_policy_usage(
    const SPolicy& policy, const SPolicyUsage& usage, std::vector<SDiagnostic>& diagnostics)
{
    for (const SPolicyRecord& record : policy.records)
    {
        const std::vector<std::string>& fields = record.fields;
        bool unused = false;
        if ((fields[0] == "include") && (fields[3] != "any"))
        {
            unused = usage.includes.find(record.line) == usage.includes.end();
        }
        else if ((fields[0] == "macro") && (fields[3] != "any"))
        {
            unused = usage.macros.find(record.line) == usage.macros.end();
        }
        else if (fields[0] == "placement")
        {
            unused = usage.placements.find(fields[1]) == usage.placements.end();
        }
        else if (fields[0] == "allocation")
        {
            unused = usage.allocations.find(fields[1]) == usage.allocations.end();
        }
        else if (fields[0] == "memory-token")
        {
            unused = usage.memory_tokens.find(fields[1]) == usage.memory_tokens.end();
        }
        if (unused)
        {
            add_diagnostic(diagnostics, "policy/morphic_policy.cfg", record.line, 1u,
                ESeverity::warning, "CFG101", "unused narrow " + fields[0] + " permission");
        }
    }
    for (const SIncludeRule& rule : policy.include_rules)
    {
        for (const SIncludeRule& other : policy.include_rules)
        {
            if ((rule.line != other.line) && (rule.operand == other.operand) &&
                ((other.scope == "all") || (other.scope == rule.scope)) &&
                permission_covers(other.match_kind, other.path, rule.match_kind, rule.path))
            {
                add_diagnostic(diagnostics, "policy/morphic_policy.cfg", rule.line, 1u,
                    ESeverity::warning, "CFG102", "include permission is covered by line " + std::to_string(other.line));
                break;
            }
        }
    }
    for (const SMacroRule& rule : policy.macro_rules)
    {
        for (const SMacroRule& other : policy.macro_rules)
        {
            if ((rule.line != other.line) && (rule.name == other.name) &&
                permission_covers(other.match_kind, other.path, rule.match_kind, rule.path))
            {
                add_diagnostic(diagnostics, "policy/morphic_policy.cfg", rule.line, 1u,
                    ESeverity::warning, "CFG102", "macro permission is covered by line " + std::to_string(other.line));
                break;
            }
        }
    }
}

static void audit_project_coverage(
    const fs::path& root, const SPolicy& policy, std::vector<SDiagnostic>& diagnostics)
{
    std::set<fs::path> discovered;
    for (const fs::directory_entry& entry : fs::directory_iterator(root))
    {
        if (entry.is_regular_file() && (lower_copy(entry.path().extension().string()) == ".vcxproj"))
        {
            discovered.insert(entry.path());
        }
    }
    if (fs::is_directory(root / "visual_studio"))
    {
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root / "visual_studio"))
        {
            if (entry.is_regular_file() && (lower_copy(entry.path().extension().string()) == ".vcxproj"))
            {
                discovered.insert(entry.path());
            }
        }
    }
    std::istringstream solution(read_file(root / "MorphicEngine.sln"));
    std::string line;
    while (std::getline(solution, line))
    {
        line = trim(line);
        if (line.rfind("Project(", 0u) != 0u) continue;
        const std::size_t assignment = line.find('=');
        if (assignment == std::string::npos) continue;
        const std::size_t name_begin = line.find('"', assignment);
        if (name_begin == std::string::npos) continue;
        const std::size_t name_end = line.find('"', name_begin + 1u);
        if (name_end == std::string::npos) continue;
        const std::size_t begin = line.find('"', name_end + 1u);
        if (begin == std::string::npos) continue;
        const std::size_t end = line.find('"', begin + 1u);
        if (end == std::string::npos) continue;
        std::string path = line.substr((begin + 1u), (end - begin - 1u));
        if (lower_copy(fs::path(path).extension().string()) != ".vcxproj")
        {
            continue;
        }
        std::replace(path.begin(), path.end(), '\\', '/');
        path = normalise_relative_path(path);
        if (path.rfind("external/", 0u) == 0u) continue;
        discovered.insert((root / path).lexically_normal());
    }
    for (const fs::path& path : discovered)
    {
        const bool classified = std::any_of(policy.projects.begin(), policy.projects.end(),
            [&](const SProjectRule& project)
            {
                std::error_code error;
                return fs::equivalent((root / project.path), path, error) && !error;
            });
        if (!classified)
        {
            add_diagnostic(diagnostics, path.lexically_relative(root), 1u, 1u,
                ESeverity::error, "PRJ004", "repository-owned project has no policy classification");
        }
    }
}

[[nodiscard]] static bool layer_include_target_is_allowed(
    const fs::path& root, const fs::path& candidate, const bool allow_framework)
{
    std::error_code error;
    const fs::path canonical_target = fs::weakly_canonical(candidate, error);
    if (error)
    {
        return false;
    }
    std::string target = normalise_relative_path(canonical_target);
#ifdef _WIN32
    target = lower_copy(target);
#endif
    std::vector<fs::path> allowed_roots{ root / "foundation", root / "external" / "SuiteUTF" };
    if (allow_framework) allowed_roots.push_back(root / "framework");
    for (const fs::path& allowed : allowed_roots)
    {
        const fs::path canonical_allowed = fs::weakly_canonical(allowed, error);
        if (error)
        {
            continue;
        }
        std::string prefix = normalise_relative_path(canonical_allowed) + '/';
#ifdef _WIN32
        prefix = lower_copy(prefix);
#endif
        if (target.rfind(prefix, 0u) == 0u)
        {
            return true;
        }
    }
    return false;
}

static void check_include(
    const fs::path& root,
    const fs::path& absolute_path,
    const fs::path& relative_path,
    const std::string& scope,
    const SPolicy& policy,
    SPolicyUsage& usage,
    bool& includes_new,
    const SDirective& directive,
    std::vector<SDiagnostic>& diagnostics)
{
    const std::string& text = directive.text;
    const auto skip_space = [&text](std::size_t& cursor)
    {
        while (cursor < text.size())
        {
            if (std::isspace(static_cast<unsigned char>(text[cursor])) != 0) ++cursor;
            else if (text.compare(cursor, 2u, "/*") == 0)
            {
                const std::size_t end = text.find("*/", cursor + 2u);
                cursor = (end == std::string::npos) ? text.size() : (end + 2u);
            }
            else break;
        }
    };
    std::size_t cursor = 1u;
    skip_space(cursor);
    if ((text.compare(cursor, 7u, "include") != 0) ||
        (((cursor + 7u) < text.size()) && is_identifier_continue(text[cursor + 7u])))
    {
        return;
    }
    cursor += 7u;
    skip_space(cursor);
    if (cursor == text.size()) return;

    const std::size_t include_line = directive.line + static_cast<std::size_t>(
        std::count(text.begin(), (text.begin() + cursor), '\n'));
    const std::size_t last_newline = text.rfind('\n', cursor);
    const std::size_t column = (last_newline == std::string::npos)
        ? (directive.column + cursor + 1u) : (cursor - last_newline + 1u);
    const std::string source_path = normalise_relative_path(relative_path);
    const bool in_foundation = source_path.rfind("foundation/", 0u) == 0u;
    const bool in_framework = source_path.rfind("framework/", 0u) == 0u;
    const char* const boundary_rule = in_foundation ? "INC002" : "INC006";
    const std::string layer_name = in_foundation ? "foundation" : "framework";
    const char opening = text[cursor];
    const char closing = (opening == '<') ? '>' : ((opening == '"') ? '"' : '\0');
    if (closing == '\0')
    {
        if (in_foundation || in_framework)
        {
            add_diagnostic(diagnostics, relative_path, include_line, column,
                ESeverity::error, boundary_rule, layer_name + " include must use a literal operand so its boundary can be checked");
        }
        else
        {
            add_diagnostic(diagnostics, relative_path, include_line, column,
                ESeverity::warning, "INC102", "macro-expanded include operand was not evaluated");
        }
        return;
    }
    const std::size_t close = text.find(closing, cursor + 1u);
    if (close == std::string::npos)
    {
        add_diagnostic(diagnostics, relative_path, include_line, column,
            ESeverity::error, "INC001", "unterminated include operand");
        return;
    }
    const std::string operand = text.substr((cursor + 1u), (close - cursor - 1u));
    includes_new = includes_new || ((opening == '<') && (operand == "new"));

    // Treat both separator spellings consistently, including when Linux CI
    // checks an include that would be accepted by the Windows compiler.
    std::string project_operand = operand;
    std::replace(project_operand.begin(), project_operand.end(), '\\', '/');
    std::vector<fs::path> candidates = {
        root / fs::path(project_operand),
        root / "foundation" / fs::path(project_operand)
    };
    if (opening == '"')
    {
        candidates.insert(candidates.begin(), absolute_path.parent_path() / fs::path(project_operand));
    }
    // Module-relative includes belong to the including file's module, even
    // when that source is compiled into the test executable.
    const fs::path component = *relative_path.begin();
    if ((component == "host") || (component == "executive") || (component == "rendering") ||
        (component == "audio") || (component == "platform"))
    {
        candidates.push_back(root / component / fs::path(project_operand));
    }
    std::set<std::string> resolved;
    bool resolved_vendor = false;
    for (const fs::path& candidate : candidates)
    {
        std::error_code error;
        if (fs::is_regular_file(candidate, error))
        {
            if ((in_foundation || in_framework) && !layer_include_target_is_allowed(root, candidate, in_framework))
            {
                const std::string allowed_layers = in_foundation ? "foundation and SuiteUTF" : "foundation, framework and SuiteUTF";
                add_diagnostic(diagnostics, relative_path, include_line, column,
                    ESeverity::error, boundary_rule, layer_name + " include " + operand +
                        " resolves outside " + allowed_layers + ": " + normalise_relative_path(candidate));
                return;
            }
            const fs::path canonical = fs::weakly_canonical(candidate, error);
            if (error || !is_within_repository(root, canonical))
            {
                add_diagnostic(diagnostics, relative_path, include_line, column,
                    ESeverity::error, "INC001", "include destination must resolve inside the repository");
                return;
            }
            const fs::path canonical_root = fs::weakly_canonical(root, error);
            const std::string destination = normalise_relative_path(canonical.lexically_relative(canonical_root));
            const std::string destination_key = lower_copy(destination);
            const bool external = destination_key.rfind("external/", 0u) == 0u;
#ifdef _WIN32
            const bool suite_utf = destination_key.rfind("external/suiteutf/", 0u) == 0u;
#else
            const bool suite_utf = destination.rfind("external/SuiteUTF/", 0u) == 0u;
#endif
            if (external && !suite_utf)
            {
                resolved_vendor = true;
                bool allowed = false;
                for (const SIncludeRule& rule : policy.include_rules)
                {
                    if (((rule.scope == "all") || (rule.scope == scope)) &&
                        path_matches(source_path, rule.match_kind, rule.path) &&
                        fs::equivalent((root / rule.operand), canonical, error) && !error)
                    {
                        usage.includes.insert(rule.line);
                        allowed = true;
                    }
                }
                if (!allowed)
                {
                    add_diagnostic(diagnostics, relative_path, include_line, column,
                        ESeverity::error, "INC001", "vendor include resolves outside its approved adapter: " + destination);
                    return;
                }
            }
            resolved.insert(normalise_relative_path(canonical));
        }
    }

    if (opening == '<')
    {
        if (!resolved_vendor && !include_is_allowed(policy, operand, scope, source_path, usage))
        {
            add_diagnostic(diagnostics, relative_path, include_line, column,
                ESeverity::error, "INC001", "direct non-project include <" + operand +
                    "> is not allowed in this scope/path");
        }
        if ((operand == "windows.h") &&
            (normalise_relative_path(relative_path) != "foundation/platform/windows_include.hpp"))
        {
            add_diagnostic(diagnostics, relative_path, include_line, column,
                ESeverity::error, "INC003", "windows.h must be included through the project wrapper");
        }
        return;
    }

    if (resolved.empty())
    {
        add_diagnostic(diagnostics, relative_path, include_line, column,
            ESeverity::error, "INC004", "project include \"" + operand + "\" does not resolve");
    }
    else if (resolved.size() > 1u)
    {
        add_diagnostic(diagnostics, relative_path, include_line, column,
            ESeverity::warning, "INC005", "project include \"" + operand + "\" resolves ambiguously");
    }
}

static void check_macro_token(
    const fs::path& relative_path,
    const SPolicy& policy,
    SPolicyUsage& usage,
    const SToken& token,
    std::vector<SDiagnostic>& diagnostics)
{
    if (macro_is_known(policy, token.text) &&
        !macro_is_allowed(policy, token.text, normalise_relative_path(relative_path), usage))
    {
        const std::string rule = macro_diagnostic_rule(policy, token.text);
        add_diagnostic(diagnostics, relative_path, token.line, token.column,
            ESeverity::error, rule,
            "global/system identity macro " + token.text + " is outside its approved surface");
    }
}

static void check_library_token(
    const fs::path& relative_path,
    const SToken& token,
    std::vector<SDiagnostic>& diagnostics)
{
    if (token.text == "stable_sort")
    {
        add_diagnostic(diagnostics, relative_path, token.line, token.column,
            ESeverity::error, "LIB001",
            "stable_sort is prohibited: temporary-allocation failure behaviour requires an engine-compatible alternative");
    }
}

static void check_memory_tokens(
    const fs::path& relative_path,
    const SPolicy& policy,
    SPolicyUsage& usage,
    const std::vector<SToken>& tokens,
    std::vector<SDiagnostic>& diagnostics)
{
    const std::string path = normalise_relative_path(relative_path);
    for (std::size_t index = 0u; index < tokens.size(); ++index)
    {
        const SToken& token = tokens[index];
        if (token.text == "CMemoryToken")
        {
            usage.memory_tokens.insert(path);
            if ((path.rfind("foundation/", 0u) != 0u) && (path.rfind("framework/", 0u) != 0u) &&
                (policy.memory_token_paths.find(path) == policy.memory_token_paths.end()))
            {
                add_diagnostic(diagnostics, relative_path, token.line, token.column,
                    ESeverity::error, "MEM004",
                    "CMemoryToken use is restricted to Foundation, Framework and approved tests; use a provided owner or container");
            }
        }
        else if (token.text == "new")
        {
            if ((index > 0u) && (tokens[index - 1u].text == "operator"))
            {
                usage.allocations.insert(path);
                if (policy.allocation_paths.find(path) == policy.allocation_paths.end())
                {
                    add_diagnostic(diagnostics, relative_path, token.line, token.column,
                        ESeverity::warning, "MEM003", "operator new use is outside approved allocation infrastructure");
                }
                continue;
            }

            const bool parenthesised = ((index + 1u) < tokens.size()) && (tokens[index + 1u].text == "(");
            bool nothrow_argument = false;
            if (parenthesised)
            {
                std::size_t depth = 0u;
                for (std::size_t cursor = index + 1u; cursor < tokens.size(); ++cursor)
                {
                    if (tokens[cursor].text == "(")
                    {
                        ++depth;
                    }
                    else if (tokens[cursor].text == ")")
                    {
                        if (--depth == 0u)
                        {
                            break;
                        }
                    }
                    else if (tokens[cursor].text == "nothrow")
                    {
                        nothrow_argument = true;
                    }
                }
            }

            if (!parenthesised || nothrow_argument)
            {
                add_diagnostic(diagnostics, relative_path, token.line, token.column,
                    ESeverity::error, "MEM001", "ordinary naked new-expression is prohibited");
            }
            else
            {
                usage.placements.insert(path);
                if (policy.placement_paths.find(path) == policy.placement_paths.end())
                {
                    add_diagnostic(diagnostics, relative_path, token.line, token.column,
                        ESeverity::warning, "MEM002", "parenthesised new-expression requires placement classification");
                }
            }
        }
        else if ((token.text == "malloc") || (token.text == "calloc") ||
            (token.text == "realloc") || (token.text == "_aligned_malloc"))
        {
            usage.allocations.insert(path);
            if (policy.allocation_paths.find(path) == policy.allocation_paths.end())
            {
                add_diagnostic(diagnostics, relative_path, token.line, token.column,
                    ESeverity::warning, "MEM003", token.text +
                        " use is outside approved allocation infrastructure");
            }
        }
    }
}

[[nodiscard]] static bool new_include_has_supporting_symbol(const std::vector<SToken>& tokens)
{
    return std::any_of(tokens.begin(), tokens.end(), [](const SToken& token)
    {
        return (token.text == "new") || (token.text == "align_val_t") ||
            (token.text == "nothrow") || (token.text == "launder");
    });
}

static void apply_decorations(
    const fs::path& relative_path,
    const SLexedFile& lexed,
    std::vector<SDiagnostic>& diagnostics)
{
    static const std::unordered_set<std::string> suppressible_rules = {
        "MEM001", "MEM002", "MEM003", "MEM004", "GID001", "GID002", "GID003",
        "INC001", "INC002", "INC003", "INC004", "INC005", "INC006", "INC101", "INC102", "LIB001"
    };

    for (const SDecoration& decoration : lexed.decorations)
    {
        if (decoration.malformed ||
            (suppressible_rules.find(decoration.rule) == suppressible_rules.end()))
        {
            add_diagnostic(diagnostics, relative_path, decoration.line, decoration.column,
                ESeverity::error, "SUP001", "malformed or unknown policy suppression");
            continue;
        }

        const auto target = std::find_if(lexed.tokens.begin(), lexed.tokens.end(),
            [&](const SToken& token) { return token.line > decoration.line; });
        std::optional<std::size_t> target_line;
        if (target != lexed.tokens.end()) target_line = target->line;
        for (const SDirective& directive : lexed.directives)
        {
            if ((directive.line > decoration.line) && (!target_line || (directive.line < *target_line)))
            {
                target_line = directive.line;
            }
        }
        if (!target_line)
        {
            add_diagnostic(diagnostics, relative_path, decoration.line, decoration.column,
                ESeverity::error, "SUP001", "stale policy suppression has no following source line");
            continue;
        }

        const auto diagnostic = std::find_if(diagnostics.begin(), diagnostics.end(),
            [&](const SDiagnostic& item)
            {
                return !item.suppressed && (item.rule == decoration.rule) &&
                    (item.line == *target_line);
            });
        if (diagnostic == diagnostics.end())
        {
            add_diagnostic(diagnostics, relative_path, decoration.line, decoration.column,
                ESeverity::error, "SUP001", "stale policy suppression does not match a diagnostic");
        }
        else
        {
            diagnostic->suppressed = true;
            diagnostic->message += " [suppressed: " + decoration.reason + ']';
        }
    }
}

static void scan_source_file(
    const fs::path& root,
    const fs::path& absolute_path,
    const std::string& scope,
    const SPolicy& policy,
    SPolicyUsage& usage,
    std::vector<SDiagnostic>& all_diagnostics)
{
    const fs::path relative_path = absolute_path.lexically_relative(root);
    const SSourceText source = splice_source(read_file(absolute_path));
    SLexedFile lexed = lex_source(source.text);
    std::vector<SDiagnostic> diagnostics;

    bool includes_new = false;
    bool macro_supports_new = false;
    for (const SDirective& directive : lexed.directives)
    {
        check_include(root, absolute_path, relative_path, scope, policy, usage, includes_new, directive, diagnostics);
        SLexedFile directive_lexed;
        directive_lexed.tokens = directive_tokens(directive);
        for (const SToken& token : directive_lexed.tokens)
        {
            check_macro_token(relative_path, policy, usage, token, diagnostics);
        }
        if (!directive_lexed.tokens.empty() && (directive_lexed.tokens.front().text == "define"))
        {
            std::size_t replacement_begin = std::min(std::size_t{ 2u }, directive_lexed.tokens.size());
            if ((directive_lexed.tokens.size() > 2u) && (directive_lexed.tokens[2u].text == "(") &&
                (directive_lexed.tokens[2u].line == directive_lexed.tokens[1u].line) &&
                (directive_lexed.tokens[2u].column == (directive_lexed.tokens[1u].column + directive_lexed.tokens[1u].text.size())))
            {
                std::size_t depth = 0u;
                for (std::size_t cursor = 2u; cursor < directive_lexed.tokens.size(); ++cursor)
                {
                    if (directive_lexed.tokens[cursor].text == "(") ++depth;
                    else if ((directive_lexed.tokens[cursor].text == ")") && (--depth == 0u))
                    {
                        replacement_begin = cursor + 1u;
                        break;
                    }
                }
            }
            for (const SToken& token : directive_lexed.tokens)
            {
                check_library_token(relative_path, token, diagnostics);
            }
            std::vector<SToken> replacement(
                (directive_lexed.tokens.begin() + replacement_begin), directive_lexed.tokens.end());
            check_memory_tokens(relative_path, policy, usage, replacement, diagnostics);
            macro_supports_new = macro_supports_new || new_include_has_supporting_symbol(replacement);
        }
    }
    for (const SToken& token : lexed.tokens)
    {
        check_macro_token(relative_path, policy, usage, token, diagnostics);
        check_library_token(relative_path, token, diagnostics);
    }
    check_memory_tokens(relative_path, policy, usage, lexed.tokens, diagnostics);
    if (includes_new && !macro_supports_new && !new_include_has_supporting_symbol(lexed.tokens))
    {
        add_diagnostic(diagnostics, relative_path, 1u, 1u, ESeverity::warning,
            "INC101", "<new> appears to have no direct supporting symbol use");
    }
    for (SDiagnostic& diagnostic : diagnostics)
    {
        restore_position(source, diagnostic.line, diagnostic.column);
    }
    for (SToken& token : lexed.tokens)
    {
        restore_position(source, token.line, token.column);
    }
    for (SDirective& directive : lexed.directives)
    {
        restore_position(source, directive.line, directive.column);
    }
    for (SDecoration& decoration : lexed.decorations)
    {
        restore_position(source, decoration.line, decoration.column);
    }
    apply_decorations(relative_path, lexed, diagnostics);
    all_diagnostics.insert(all_diagnostics.end(), diagnostics.begin(), diagnostics.end());
}

[[nodiscard]] static std::size_t line_at_offset(const std::string& text, const std::size_t offset)
{
    return 1u + static_cast<std::size_t>(std::count(text.begin(), text.begin() + offset, '\n'));
}

[[nodiscard]] static std::optional<std::string> attribute_value(
    const std::string& text,
    const std::string& attribute)
{
    const std::string marker = attribute + "=\"";
    const std::size_t begin = text.find(marker);
    if (begin == std::string::npos)
    {
        return std::nullopt;
    }
    const std::size_t value_begin = begin + marker.size();
    const std::size_t end = text.find('"', value_begin);
    return (end == std::string::npos)
        ? std::nullopt
        : std::optional<std::string>(text.substr(value_begin, end - value_begin));
}

[[nodiscard]] static std::optional<std::string> tag_value(
    const std::string& text,
    const std::string& tag)
{
    const std::string opening = '<' + tag;
    const std::size_t element = text.find(opening);
    if (element == std::string::npos)
    {
        return std::nullopt;
    }
    const std::size_t begin = text.find('>', element + opening.size());
    if (begin == std::string::npos)
    {
        return std::nullopt;
    }
    const std::string closing = "</" + tag + '>';
    const std::size_t end = text.find(closing, begin + 1u);
    return (end == std::string::npos)
        ? std::nullopt
        : std::optional<std::string>(trim(text.substr(begin + 1u, end - begin - 1u)));
}

[[nodiscard]] static std::vector<SProjectConfiguration> parse_project_configurations(
    const std::string& xml)
{
    std::vector<SProjectConfiguration> configurations;
    std::size_t cursor = 0u;
    static const std::string marker = "<ProjectConfiguration ";
    while ((cursor = xml.find(marker, cursor)) != std::string::npos)
    {
        const std::size_t end = xml.find('>', cursor);
        if (end == std::string::npos)
        {
            break;
        }
        const auto include = attribute_value(xml.substr(cursor, end - cursor + 1u), "Include");
        if (include)
        {
            const std::size_t separator = include->find('|');
            if (separator != std::string::npos)
            {
                configurations.push_back({
                    include->substr(0u, separator), include->substr(separator + 1u) });
            }
        }
        cursor = end + 1u;
    }
    return configurations;
}

[[nodiscard]] static std::vector<SCompilerGroup> parse_compiler_groups(const std::string& xml)
{
    std::vector<SCompilerGroup> groups;
    std::size_t cursor = 0u;
    static const std::string marker = "<ItemDefinitionGroup";
    static const std::string closing = "</ItemDefinitionGroup>";
    while ((cursor = xml.find(marker, cursor)) != std::string::npos)
    {
        const std::size_t open_end = xml.find('>', cursor);
        const std::size_t group_end = xml.find(closing, open_end);
        if ((open_end == std::string::npos) || (group_end == std::string::npos))
        {
            break;
        }
        const std::string opening = xml.substr(cursor, open_end - cursor + 1u);
        const std::string body = xml.substr(open_end + 1u, group_end - open_end - 1u);
        const std::size_t compile_begin = body.find("<ClCompile");
        const std::size_t compile_end = body.find("</ClCompile>", compile_begin);
        if ((compile_begin != std::string::npos) && (compile_end != std::string::npos))
        {
            const std::string compiler = body.substr(
                compile_begin, compile_end - compile_begin + std::string("</ClCompile>").size());
            SCompilerGroup group;
            group.condition = attribute_value(opening, "Condition").value_or("");
            group.language_standard = tag_value(compiler, "LanguageStandard");
            group.exception_handling = tag_value(compiler, "ExceptionHandling");
            group.additional_options = tag_value(compiler, "AdditionalOptions");
            group.line = line_at_offset(xml, cursor);
            groups.push_back(std::move(group));
        }
        cursor = group_end + closing.size();
    }
    return groups;
}

[[nodiscard]] static std::string remove_spaces(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](const unsigned char character)
    {
        return std::isspace(character) != 0;
    }), value.end());
    return value;
}

[[nodiscard]] static std::string unquote(std::string value)
{
    value = trim(value);
    if ((value.size() >= 2u) &&
        (((value.front() == '\'') && (value.back() == '\'')) ||
         ((value.front() == '"') && (value.back() == '"'))))
    {
        return value.substr(1u, value.size() - 2u);
    }
    return value;
}

[[nodiscard]] static bool group_matches(
    const std::string& condition,
    const SProjectConfiguration& configuration,
    bool& understood)
{
    understood = true;
    if (trim(condition).empty())
    {
        return true;
    }
    const std::string compact = remove_spaces(condition);
    const std::size_t equals = compact.find("==");
    if (equals == std::string::npos)
    {
        understood = false;
        return false;
    }
    const std::string left = unquote(compact.substr(0u, equals));
    const std::string right = unquote(compact.substr(equals + 2u));
    if (left == "$(Configuration)|$(Platform)")
    {
        return right == (configuration.configuration + '|' + configuration.platform);
    }
    if (left == "$(Configuration)")
    {
        return right == configuration.configuration;
    }
    if (left == "$(Platform)")
    {
        return right == configuration.platform;
    }
    understood = false;
    return false;
}

static void check_project(
    const fs::path& root,
    const SProjectRule& project,
    std::vector<SDiagnostic>& diagnostics)
{
    const fs::path absolute_path = root / project.path;
    const std::string xml = read_file(absolute_path);
    const std::vector<SProjectConfiguration> configurations = parse_project_configurations(xml);
    const std::vector<SCompilerGroup> groups = parse_compiler_groups(xml);
    if (configurations.empty())
    {
        add_diagnostic(diagnostics, project.path, 1u, 1u, ESeverity::error,
            "PRJ001", "project has no configurations");
        return;
    }

    bool warned_unknown_condition = false;
    for (const SProjectConfiguration& configuration : configurations)
    {
        std::optional<std::string> language_standard;
        std::optional<std::string> exception_handling;
        std::string additional_options;
        std::size_t setting_line = 1u;
        for (const SCompilerGroup& group : groups)
        {
            bool understood = true;
            if (group_matches(group.condition, configuration, understood))
            {
                setting_line = group.line;
                if (group.language_standard)
                {
                    language_standard = group.language_standard;
                }
                if (group.exception_handling)
                {
                    exception_handling = group.exception_handling;
                }
                if (group.additional_options)
                {
                    additional_options += ' ' + *group.additional_options;
                }
            }
            else if (!understood && !warned_unknown_condition)
            {
                add_diagnostic(diagnostics, project.path, group.line, 1u, ESeverity::warning,
                    "PRJ101", "ItemDefinitionGroup condition was not evaluated: " + group.condition);
                warned_unknown_condition = true;
            }
        }

        const std::string label = configuration.configuration + '|' + configuration.platform;
        if (!language_standard || (*language_standard != "stdcpp17"))
        {
            add_diagnostic(diagnostics, project.path, setting_line, 1u, ESeverity::error,
                "PRJ001", label + " must explicitly select stdcpp17");
        }
        if (project.scope == "engine")
        {
            if (!exception_handling || (lower_copy(*exception_handling) != "false"))
            {
                add_diagnostic(diagnostics, project.path, setting_line, 1u, ESeverity::error,
                    "PRJ002", label + " must explicitly disable exception handling");
            }
        }
        else if (project.scope == "tool")
        {
            if (!exception_handling)
            {
                add_diagnostic(diagnostics, project.path, setting_line, 1u, ESeverity::error,
                    "PRJ002", label + " tooling exception policy must be explicit");
            }
        }

        std::istringstream options(lower_copy(additional_options));
        std::string option;
        bool exception_override = false;
        bool standard_override = false;
        while (options >> option)
        {
            if ((option.size() >= 2u) && (option.front() == '"') && (option.back() == '"'))
            {
                option = option.substr(1u, option.size() - 2u);
            }
            if (!option.empty() && (option.front() == '-')) option.front() = '/';
            exception_override = exception_override || (option.rfind("/eh", 0u) == 0u);
            standard_override = standard_override || ((option.rfind("/std:", 0u) == 0u) && (option != "/std:c++17"));
        }
        if ((project.scope == "engine") && exception_override)
        {
            add_diagnostic(diagnostics, project.path, setting_line, 1u, ESeverity::error,
                "PRJ003", label + " AdditionalOptions contains an exception-handling override");
        }
        if (standard_override)
        {
            add_diagnostic(diagnostics, project.path, setting_line, 1u, ESeverity::error,
                "PRJ003", label + " AdditionalOptions contradicts the C++17 setting");
        }
    }
}

[[nodiscard]] static std::string diagnostic_text(const fs::path& root, const SDiagnostic& diagnostic)
{
    const fs::path absolute = (root / diagnostic.path).lexically_normal();
    const char* severity = diagnostic.suppressed
        ? "suppressed"
        : ((diagnostic.severity == ESeverity::error) ? "error" : "warning");
    std::ostringstream text;
    text << absolute.string() << '(' << diagnostic.line << ',' << diagnostic.column << "): "
         << severity << ' ' << diagnostic.rule << ": " << diagnostic.message;
    return text.str();
}

[[nodiscard]] static std::string safe_filename_component(std::string value)
{
    if (value.empty())
    {
        return value;
    }
    for (char& character : value)
    {
        if (!(std::isalnum(static_cast<unsigned char>(character)) != 0 ||
            character == '-' || character == '_'))
        {
            character = '_';
        }
    }
    return value;
}

static void write_report(
    const fs::path& root,
    const SOptions& options,
    const std::vector<SDiagnostic>& diagnostics,
    const std::size_t errors,
    const std::size_t warnings,
    const std::size_t suppressed)
{
    const fs::path directory = root / "development" / "logical-roots" / "logs" / "policy_validator";
    fs::create_directories(directory);
    std::string filename = safe_filename_component(options.project);
    if (!options.configuration.empty())
    {
        filename += '.' + safe_filename_component(options.configuration);
    }
    if (!options.platform.empty())
    {
        filename += '.' + safe_filename_component(options.platform);
    }
    filename += ".policy.log";
    const fs::path destination = directory / filename;

    std::ostringstream thread_id;
    thread_id << std::this_thread::get_id();
    const fs::path temporary = destination.string() + ".tmp." + thread_id.str();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("cannot create policy report " + temporary.string());
        }
        output << "Morphic Policy Validator\n"
               << "Project: " << options.project << '\n'
               << "Configuration: " << options.configuration << '\n'
               << "Platform: " << options.platform << "\n\n";
        for (const SDiagnostic& diagnostic : diagnostics)
        {
            output << diagnostic_text(root, diagnostic) << '\n';
        }
        output << "\nSummary: " << errors << " error(s), " << warnings
               << " warning(s), " << suppressed << " suppressed diagnostic(s)\n";
        if (!output)
        {
            throw std::runtime_error("cannot write policy report " + temporary.string());
        }
    }
    std::error_code error;
    fs::remove(destination, error);
    error.clear();
    fs::rename(temporary, destination, error);
    if (error)
    {
        fs::remove(temporary);
        throw std::runtime_error("cannot publish policy report " + destination.string() +
            ": " + error.message());
    }
}

fs::path discover_repository_root(const fs::path& start)
{
    fs::path candidate = fs::absolute(start).lexically_normal();
    for (;;)
    {
        if (fs::is_regular_file(candidate / "MorphicEngine.sln") &&
            fs::is_regular_file(candidate / "policy" / "morphic_policy.cfg"))
        {
            return candidate;
        }
        const fs::path parent = candidate.parent_path();
        if (parent.empty() || parent == candidate)
        {
            throw std::invalid_argument("could not discover Morphic Engine repository root");
        }
        candidate = parent;
    }
}

int run(const SOptions& requested_options)
{
    SOptions options = requested_options;
    options.repository_root = fs::absolute(options.repository_root).lexically_normal();
    const fs::path root = options.repository_root;
    if (!fs::is_regular_file(root / "MorphicEngine.sln"))
    {
        throw std::invalid_argument("--root is not a Morphic Engine checkout: " + root.string());
    }

    const SPolicy policy = load_policy(root / "policy" / "morphic_policy.cfg");
    std::vector<SDiagnostic> diagnostics;
    SPolicyUsage usage;
    audit_policy_targets(root, policy, diagnostics);
    audit_project_coverage(root, policy, diagnostics);
    const bool valid_targets = diagnostics.empty();
    if (valid_targets)
    {
        for (const SScanRoot& scan_root : policy.scan_roots)
        {
            const fs::path absolute_scan_root = root / scan_root.path;
            for (const fs::directory_entry& entry : fs::recursive_directory_iterator(absolute_scan_root))
            {
                if (entry.is_regular_file() && is_source_file(entry.path()))
                {
                    scan_source_file(root, entry.path(), scan_root.scope, policy, usage, diagnostics);
                }
            }
        }
        for (const SProjectRule& project : policy.projects)
        {
            check_project(root, project, diagnostics);
        }
        audit_policy_usage(policy, usage, diagnostics);
    }

    std::sort(diagnostics.begin(), diagnostics.end(), [](const SDiagnostic& lhs, const SDiagnostic& rhs)
    {
        const std::string lhs_path = normalise_relative_path(lhs.path);
        const std::string rhs_path = normalise_relative_path(rhs.path);
        if (lhs_path != rhs_path) return lhs_path < rhs_path;
        if (lhs.line != rhs.line) return lhs.line < rhs.line;
        if (lhs.column != rhs.column) return lhs.column < rhs.column;
        return lhs.rule < rhs.rule;
    });

    std::size_t errors = 0u;
    std::size_t warnings = 0u;
    std::size_t suppressed = 0u;
    for (const SDiagnostic& diagnostic : diagnostics)
    {
        std::cout << diagnostic_text(root, diagnostic) << '\n';
        if (diagnostic.suppressed)
        {
            ++suppressed;
        }
        else if (diagnostic.severity == ESeverity::error)
        {
            ++errors;
        }
        else
        {
            ++warnings;
        }
    }
    std::cout << "MorphicPolicyValidator: " << errors << " error(s), " << warnings
              << " warning(s), " << suppressed << " suppressed diagnostic(s)\n";

    if (options.write_report)
    {
        write_report(root, options, diagnostics, errors, warnings, suppressed);
    }
    return (errors == 0u) ? 0 : 1;
}

}   //  namespace morphic::policy
