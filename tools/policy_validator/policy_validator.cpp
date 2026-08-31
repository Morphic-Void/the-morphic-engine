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
namespace
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
};

struct SMacroRule
{
    std::string name;
    std::string diagnostic_rule;
    std::string match_kind;
    std::string path;
};

struct SProjectRule
{
    fs::path path;
    std::string scope;
};

struct SPolicy
{
    std::vector<SScanRoot> scan_roots;
    std::vector<SIncludeRule> include_rules;
    std::vector<SMacroRule> macro_rules;
    std::vector<SProjectRule> projects;
    std::unordered_set<std::string> placement_paths;
    std::unordered_set<std::string> allocation_paths;
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

[[nodiscard]] bool is_identifier_start(const char value)
{
    return std::isalpha(static_cast<unsigned char>(value)) != 0 || value == '_';
}

[[nodiscard]] bool is_identifier_continue(const char value)
{
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

[[nodiscard]] std::string trim(std::string value)
{
    const auto not_space = [](const unsigned char character)
    {
        return std::isspace(character) == 0;
    };
    const auto begin = std::find_if(value.begin(), value.end(), not_space);
    const auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return (begin < end) ? std::string(begin, end) : std::string{};
}

[[nodiscard]] std::string lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character)
    {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] std::string normalise_relative_path(const fs::path& path)
{
    std::string result = path.lexically_normal().generic_string();
    while (result.rfind("./", 0u) == 0u)
    {
        result.erase(0u, 2u);
    }
    return result;
}

[[nodiscard]] std::string read_file(const fs::path& path)
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

[[nodiscard]] std::vector<std::string> split_fields(const std::string& line)
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

[[nodiscard]] bool path_matches(
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

[[nodiscard]] SPolicy load_policy(const fs::path& path)
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

        if ((fields[0] == "version") && (fields.size() == 2u))
        {
            if (fields[1] != "1")
            {
                throw std::invalid_argument("unsupported policy version: " + fields[1]);
            }
            version_seen = true;
        }
        else if ((fields[0] == "scan") && (fields.size() == 3u))
        {
            policy.scan_roots.push_back({ fields[1], fields[2] });
        }
        else if ((fields[0] == "include") && (fields.size() == 6u))
        {
            policy.include_rules.push_back({
                fields[1], fields[2], fields[3], fields[4], fields[5] });
        }
        else if ((fields[0] == "placement") && (fields.size() == 2u))
        {
            policy.placement_paths.insert(fields[1]);
        }
        else if ((fields[0] == "allocation") && (fields.size() == 2u))
        {
            policy.allocation_paths.insert(fields[1]);
        }
        else if ((fields[0] == "macro") && (fields.size() == 5u))
        {
            policy.macro_rules.push_back({
                fields[1], fields[2], fields[3], fields[4] });
        }
        else if ((fields[0] == "project") && (fields.size() == 3u))
        {
            policy.projects.push_back({ fields[1], fields[2] });
        }
        else
        {
            invalid();
        }
    }
    if (!version_seen || policy.scan_roots.empty() || policy.projects.empty())
    {
        throw std::invalid_argument("policy is missing required version, scan, or project records");
    }
    return policy;
}

[[nodiscard]] bool begins_raw_string(const std::string& source, const std::size_t index)
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

[[nodiscard]] std::size_t raw_prefix_length(const std::string& source, const std::size_t index)
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

void update_position(
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

[[nodiscard]] std::optional<SDecoration> parse_decoration(
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

[[nodiscard]] SLexedFile lex_source(const std::string& source)
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
            for (;;)
            {
                const std::size_t newline = source.find('\n', index);
                if (newline == std::string::npos)
                {
                    index = source.size();
                    break;
                }
                std::size_t previous = newline;
                while ((previous > begin) &&
                    ((source[previous - 1u] == '\r') || (source[previous - 1u] == ' ') ||
                     (source[previous - 1u] == '\t')))
                {
                    --previous;
                }
                const bool continued = (previous > begin) && (source[previous - 1u] == '\\');
                index = newline + 1u;
                ++line;
                line_start = index;
                if (!continued)
                {
                    break;
                }
            }
            result.directives.push_back({
                source.substr(begin, index - begin), directive_line, directive_column });
            only_whitespace_on_line = true;
            continue;
        }
        only_whitespace_on_line = false;

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

[[nodiscard]] std::vector<SToken> directive_identifiers(const SDirective& directive)
{
    std::vector<SToken> tokens;
    std::size_t line = directive.line;
    std::size_t column = directive.column;
    for (std::size_t index = 0u; index < directive.text.size();)
    {
        const char character = directive.text[index];
        if (character == '\n')
        {
            ++line;
            column = 1u;
            ++index;
            continue;
        }
        if (is_identifier_start(character))
        {
            const std::size_t begin = index;
            const std::size_t token_column = column;
            ++index;
            ++column;
            while ((index < directive.text.size()) && is_identifier_continue(directive.text[index]))
            {
                ++index;
                ++column;
            }
            tokens.push_back({ directive.text.substr(begin, index - begin), line, token_column });
        }
        else
        {
            ++index;
            ++column;
        }
    }
    return tokens;
}

[[nodiscard]] bool is_source_file(const fs::path& path)
{
    static const std::set<std::string> extensions = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".inl", ".def"
    };
    return extensions.find(lower_copy(path.extension().string())) != extensions.end();
}

[[nodiscard]] bool include_is_allowed(
    const SPolicy& policy,
    const std::string& operand,
    const std::string& scope,
    const std::string& relative_path)
{
    return std::any_of(policy.include_rules.begin(), policy.include_rules.end(),
        [&](const SIncludeRule& rule)
        {
            return (rule.operand == operand) &&
                ((rule.scope == "all") || (rule.scope == scope)) &&
                path_matches(relative_path, rule.match_kind, rule.path);
        });
}

[[nodiscard]] bool macro_is_known(const SPolicy& policy, const std::string& name)
{
    return std::any_of(policy.macro_rules.begin(), policy.macro_rules.end(),
        [&](const SMacroRule& rule) { return rule.name == name; });
}

[[nodiscard]] bool macro_is_allowed(
    const SPolicy& policy,
    const std::string& name,
    const std::string& relative_path)
{
    return std::any_of(policy.macro_rules.begin(), policy.macro_rules.end(),
        [&](const SMacroRule& rule)
        {
            return (rule.name == name) &&
                path_matches(relative_path, rule.match_kind, rule.path);
        });
}

[[nodiscard]] std::string macro_diagnostic_rule(
    const SPolicy& policy,
    const std::string& name)
{
    const auto found = std::find_if(policy.macro_rules.begin(), policy.macro_rules.end(),
        [&](const SMacroRule& rule) { return rule.name == name; });
    return (found != policy.macro_rules.end()) ? found->diagnostic_rule : "GID001";
}

void add_diagnostic(
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

void check_include(
    const fs::path& root,
    const fs::path& absolute_path,
    const fs::path& relative_path,
    const std::string& scope,
    const SPolicy& policy,
    const SDirective& directive,
    std::vector<SDiagnostic>& diagnostics)
{
    std::string text = trim(directive.text);
    if (text.empty() || text[0] != '#')
    {
        return;
    }
    text = trim(text.substr(1u));
    if (text.rfind("include", 0u) != 0u ||
        ((text.size() > 7u) && is_identifier_continue(text[7u])))
    {
        return;
    }
    text = trim(text.substr(7u));
    if (text.size() < 3u)
    {
        return;
    }

    const char opening = text[0];
    const char closing = (opening == '<') ? '>' : ((opening == '"') ? '"' : '\0');
    if (closing == '\0')
    {
        add_diagnostic(diagnostics, relative_path, directive.line, directive.column,
            ESeverity::warning, "INC102", "macro-expanded include operand was not evaluated");
        return;
    }
    const std::size_t close = text.find(closing, 1u);
    if (close == std::string::npos)
    {
        add_diagnostic(diagnostics, relative_path, directive.line, directive.column,
            ESeverity::error, "INC001", "unterminated include operand");
        return;
    }
    const std::string operand = text.substr(1u, close - 1u);
    const std::size_t column = directive.column + directive.text.find(operand);

    if (opening == '<')
    {
        if (!include_is_allowed(policy, operand, scope, normalise_relative_path(relative_path)))
        {
            add_diagnostic(diagnostics, relative_path, directive.line, column,
                ESeverity::error, "INC001", "direct non-project include <" + operand +
                    "> is not allowed in this scope/path");
        }
        if ((operand == "windows.h") &&
            (normalise_relative_path(relative_path) != "core/platform/windows_include.hpp"))
        {
            add_diagnostic(diagnostics, relative_path, directive.line, column,
                ESeverity::error, "INC003", "windows.h must be included through the project wrapper");
        }
        return;
    }

    std::vector<fs::path> candidates = {
        absolute_path.parent_path() / fs::path(operand),
        root / fs::path(operand),
        root / "core" / fs::path(operand)
    };
    std::set<std::string> resolved;
    for (const fs::path& candidate : candidates)
    {
        std::error_code error;
        if (fs::is_regular_file(candidate, error))
        {
            resolved.insert(normalise_relative_path(candidate.lexically_normal()));
        }
    }
    if (resolved.empty())
    {
        add_diagnostic(diagnostics, relative_path, directive.line, column,
            ESeverity::error, "INC004", "project include \"" + operand + "\" does not resolve");
    }
    else if (resolved.size() > 1u)
    {
        add_diagnostic(diagnostics, relative_path, directive.line, column,
            ESeverity::warning, "INC005", "project include \"" + operand + "\" resolves ambiguously");
    }
}

void check_macro_token(
    const fs::path& relative_path,
    const SPolicy& policy,
    const SToken& token,
    std::vector<SDiagnostic>& diagnostics)
{
    if (macro_is_known(policy, token.text) &&
        !macro_is_allowed(policy, token.text, normalise_relative_path(relative_path)))
    {
        const std::string rule = macro_diagnostic_rule(policy, token.text);
        add_diagnostic(diagnostics, relative_path, token.line, token.column,
            ESeverity::error, rule,
            "global/system identity macro " + token.text + " is outside its approved surface");
    }
}

void check_library_token(
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

void check_memory_tokens(
    const fs::path& relative_path,
    const SPolicy& policy,
    const std::vector<SToken>& tokens,
    std::vector<SDiagnostic>& diagnostics)
{
    const std::string path = normalise_relative_path(relative_path);
    for (std::size_t index = 0u; index < tokens.size(); ++index)
    {
        const SToken& token = tokens[index];
        if (token.text == "new")
        {
            if ((index > 0u) && (tokens[index - 1u].text == "operator"))
            {
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
            else if (policy.placement_paths.find(path) == policy.placement_paths.end())
            {
                add_diagnostic(diagnostics, relative_path, token.line, token.column,
                    ESeverity::warning, "MEM002", "parenthesised new-expression requires placement classification");
            }
        }
        else if ((token.text == "malloc") || (token.text == "calloc") ||
            (token.text == "realloc") || (token.text == "_aligned_malloc"))
        {
            if (policy.allocation_paths.find(path) == policy.allocation_paths.end())
            {
                add_diagnostic(diagnostics, relative_path, token.line, token.column,
                    ESeverity::warning, "MEM003", token.text +
                        " use is outside approved allocation infrastructure");
            }
        }
    }
}

[[nodiscard]] bool new_include_has_supporting_symbol(const std::vector<SToken>& tokens)
{
    return std::any_of(tokens.begin(), tokens.end(), [](const SToken& token)
    {
        return (token.text == "new") || (token.text == "align_val_t") ||
            (token.text == "nothrow") || (token.text == "launder");
    });
}

void apply_decorations(
    const fs::path& relative_path,
    const SLexedFile& lexed,
    std::vector<SDiagnostic>& diagnostics)
{
    static const std::unordered_set<std::string> suppressible_rules = {
        "MEM001", "MEM002", "MEM003", "GID001", "GID002", "GID003",
        "INC001", "INC002", "INC003", "INC004", "INC005", "INC101", "INC102", "LIB001"
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
        if (target == lexed.tokens.end())
        {
            add_diagnostic(diagnostics, relative_path, decoration.line, decoration.column,
                ESeverity::error, "SUP001", "stale policy suppression has no following source line");
            continue;
        }

        const auto diagnostic = std::find_if(diagnostics.begin(), diagnostics.end(),
            [&](const SDiagnostic& item)
            {
                return !item.suppressed && (item.rule == decoration.rule) &&
                    (item.line == target->line);
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

void scan_source_file(
    const fs::path& root,
    const fs::path& absolute_path,
    const std::string& scope,
    const SPolicy& policy,
    std::vector<SDiagnostic>& all_diagnostics)
{
    const fs::path relative_path = absolute_path.lexically_relative(root);
    const SLexedFile lexed = lex_source(read_file(absolute_path));
    std::vector<SDiagnostic> diagnostics;

    bool includes_new = false;
    for (const SDirective& directive : lexed.directives)
    {
        check_include(root, absolute_path, relative_path, scope, policy, directive, diagnostics);
        if (directive.text.find("<new>") != std::string::npos)
        {
            includes_new = true;
        }
        for (const SToken& token : directive_identifiers(directive))
        {
            check_macro_token(relative_path, policy, token, diagnostics);
        }
        // Reuse the lexer for macro definitions so comments and literals are ignored.
        std::string definition = directive.text;
        definition[0] = ' ';
        const SLexedFile directive_lexed = lex_source(definition);
        if (!directive_lexed.tokens.empty() && directive_lexed.tokens.front().text == "define")
        {
            for (SToken token : directive_lexed.tokens)
            {
                if (token.line == 1u)
                {
                    token.column += directive.column - 1u;
                }
                token.line += directive.line - 1u;
                check_library_token(relative_path, token, diagnostics);
            }
        }
    }
    for (const SToken& token : lexed.tokens)
    {
        check_macro_token(relative_path, policy, token, diagnostics);
        check_library_token(relative_path, token, diagnostics);
    }
    check_memory_tokens(relative_path, policy, lexed.tokens, diagnostics);
    if (includes_new && !new_include_has_supporting_symbol(lexed.tokens))
    {
        add_diagnostic(diagnostics, relative_path, 1u, 1u, ESeverity::warning,
            "INC101", "<new> appears to have no direct supporting symbol use");
    }
    apply_decorations(relative_path, lexed, diagnostics);
    all_diagnostics.insert(all_diagnostics.end(), diagnostics.begin(), diagnostics.end());
}

[[nodiscard]] std::size_t line_at_offset(const std::string& text, const std::size_t offset)
{
    return 1u + static_cast<std::size_t>(std::count(text.begin(), text.begin() + offset, '\n'));
}

[[nodiscard]] std::optional<std::string> attribute_value(
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

[[nodiscard]] std::optional<std::string> tag_value(
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

[[nodiscard]] std::vector<SProjectConfiguration> parse_project_configurations(
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

[[nodiscard]] std::vector<SCompilerGroup> parse_compiler_groups(const std::string& xml)
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

[[nodiscard]] std::string remove_spaces(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](const unsigned char character)
    {
        return std::isspace(character) != 0;
    }), value.end());
    return value;
}

[[nodiscard]] std::string unquote(std::string value)
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

[[nodiscard]] bool group_matches(
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

void check_project(
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

        const std::string lowered_options = lower_copy(additional_options);
        if ((project.scope == "engine") && (lowered_options.find("/eh") != std::string::npos))
        {
            add_diagnostic(diagnostics, project.path, setting_line, 1u, ESeverity::error,
                "PRJ003", label + " AdditionalOptions contains an exception-handling override");
        }
        const std::size_t standard_option = lowered_options.find("/std:");
        if ((standard_option != std::string::npos) &&
            (lowered_options.find("/std:c++17", standard_option) == std::string::npos))
        {
            add_diagnostic(diagnostics, project.path, setting_line, 1u, ESeverity::error,
                "PRJ003", label + " AdditionalOptions contradicts the C++17 setting");
        }
    }
}

[[nodiscard]] std::string diagnostic_text(const fs::path& root, const SDiagnostic& diagnostic)
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

[[nodiscard]] std::string safe_filename_component(std::string value)
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

void write_report(
    const fs::path& root,
    const SOptions& options,
    const std::vector<SDiagnostic>& diagnostics,
    const std::size_t errors,
    const std::size_t warnings,
    const std::size_t suppressed)
{
    const fs::path directory = root / "logs" / "policy_validator";
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

}   //  namespace

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
    for (const SScanRoot& scan_root : policy.scan_roots)
    {
        const fs::path absolute_scan_root = root / scan_root.path;
        if (!fs::is_directory(absolute_scan_root))
        {
            add_diagnostic(diagnostics, scan_root.path, 1u, 1u, ESeverity::error,
                "SRC001", "configured source root does not exist");
            continue;
        }
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(absolute_scan_root))
        {
            if (entry.is_regular_file() && is_source_file(entry.path()))
            {
                scan_source_file(root, entry.path(), scan_root.scope, policy, diagnostics);
            }
        }
    }
    for (const SProjectRule& project : policy.projects)
    {
        check_project(root, project, diagnostics);
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
