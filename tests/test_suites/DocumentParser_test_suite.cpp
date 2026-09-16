
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    DocumentParser_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#include "tests/test_suites/DocumentParser_test_suite.hpp"

#include <charconv>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include "data_model/baked_document.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"
#include "data_model/live_document.hpp"
#include "text/text_linter.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"

namespace document_parser_tests
{

using tests::TTestContext;

static CDocumentReport parse_text(const CStringView& source, CLiveDocument& destination,
    const CDocumentParseOptions& options = {}, CTextLintReport* const linter_report = nullptr)
{
    return document_parser::parse(CByteConstView{ source.string(), source.length() }, destination, options, linter_report);
}

//  Helpers have file-local linkage; the namespace groups this suite's names.
//  Grammar and construction tests opt into every supported feature.
static CDocumentReport parse(const std::string& text, CLiveDocument& destination)
{
    return parse_text(CStringView{ text.data(), text.size() }, destination, { document_policy::k_all_supported });
}

static std::string write(TTestContext& ctx, const CLiveDocument& document, const EDocumentWriteMode mode = EDocumentWriteMode::morphic)
{
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(document, block));
    CDocumentWriteOptions options;
    options.mode = mode;
    options.pretty_print = false;
    options.trailing_line_ending = false;
    options.escape_non_ascii = true;
    const auto result = document_writer::write(block.document(), options);
    TEST_EXPECT(ctx, result.report.succeeded());
    return result.report.succeeded() ? std::string(reinterpret_cast<const char*>(result.output.data()), result.report.logical_text_byte_size) : std::string{};
}

static CNodeKey member(const CLiveDocument& document, const CNodeKey object, const CStringView& name)
{
    for (CNodeKey child = document.first_child(object); child.is_valid(); child = document.next_sibling(child))
    {
        if (document.name(child) == name)
        {
            return child;
        }
    }
    return {};
}

static void test_findings_and_policy_contract(TTestContext& ctx)
{
    static_assert(sizeof(EDocumentFinding) == sizeof(std::uint32_t));
    static_assert(document_finding_bit(EDocumentFinding::reserved_undefined_cp1252_byte) ==
        text_source_finding_bit(ETextSourceFinding::undefined_cp1252_byte));
    constexpr std::uint32_t groups[]{ document_findings::k_source,
        document_findings::k_relaxed, document_findings::k_morphic, document_findings::k_semantic };
    std::uint32_t combined = 0u;
    for (const std::uint32_t group : groups)
    {
        TEST_EXPECT(ctx, (combined & group) == 0u);
        std::uint32_t range = group;
        while ((range != 0u) && ((range & 1u) == 0u))
        {
            range >>= 1u;
        }
        TEST_EXPECT(ctx, (range != 0u) && ((range & (range + 1u)) == 0u));
        combined |= group;
    }
    TEST_EXPECT(ctx, combined == document_findings::k_all);
    TEST_EXPECT(ctx, document_findings::k_relaxed == (EDocumentFinding::comments |
        EDocumentFinding::unquoted_names | EDocumentFinding::unquoted_strings | EDocumentFinding::single_quotes |
        EDocumentFinding::trailing_commas | EDocumentFinding::raw_quoted_line_breaks | EDocumentFinding::raw_quoted_controls |
        EDocumentFinding::name_collision_extension | EDocumentFinding::implicit_body));
    TEST_EXPECT(ctx, document_findings::k_morphic == (EDocumentFinding::explicit_plus |
        EDocumentFinding::binary | EDocumentFinding::hexadecimal | EDocumentFinding::alternate_hexadecimal_prefix));
    TEST_EXPECT(ctx, document_findings::k_semantic == (EDocumentFinding::empty_member_name |
        EDocumentFinding::singleton_normalization | EDocumentFinding::logical_nul));
    TEST_EXPECT(ctx, document_findings::k_source ==
        (k_text_source_encoding_features | k_text_source_observations |
            text_source_finding_bit(ETextSourceFinding::utf8_attempt_failed)));
    TEST_EXPECT(ctx, (document_findings::k_all & text_source_finding_bit(ETextSourceFinding::undefined_cp1252_byte)) == 0u);
    TEST_EXPECT(ctx, !CDocumentPolicyResult{}.accepted());
    TEST_EXPECT(ctx, document_policy::evaluate(0u).accepted());
    TEST_EXPECT(ctx, document_policy::evaluate(0u, { document_policy::k_ascii }).accepted());

    constexpr std::uint32_t default_features = EDocumentFinding::non_ascii_utf8 |
        EDocumentFinding::modified_nul | EDocumentFinding::cesu8_pair | EDocumentFinding::cp1252 |
        EDocumentFinding::explicit_plus | EDocumentFinding::hexadecimal | EDocumentFinding::binary |
        EDocumentFinding::alternate_hexadecimal_prefix;
    TEST_EXPECT(ctx, CDocumentParseOptions{}.allowed_features == default_features);
    const auto default_result = document_policy::evaluate(document_findings::k_all);
    TEST_EXPECT(ctx, default_result.status == EDocumentPolicyStatus::rejected);
    TEST_EXPECT(ctx, default_result.disallowed_features == document_findings::k_relaxed);
    TEST_EXPECT(ctx, document_policy::evaluate(document_findings::k_all, { document_policy::k_all_supported }).accepted());

    //  Every permission can be selected individually; every other bit is an
    //  invalid policy option, including known informational findings.
    for (unsigned index = 0u; index < 32u; ++index)
    {
        const std::uint32_t bit = 1u << index;
        const auto selected = document_policy::evaluate(bit, { bit });
        const auto excluded = document_policy::evaluate(bit, { document_policy::k_ascii });
        if ((bit & document_findings::k_acceptance_features) != 0u)
        {
            TEST_EXPECT(ctx, selected.accepted());
            TEST_EXPECT(ctx, selected.unknown_policy_bits == 0u);
            TEST_EXPECT(ctx, excluded.status == EDocumentPolicyStatus::rejected);
            TEST_EXPECT(ctx, excluded.disallowed_features == bit);
            const auto default_bit = document_policy::evaluate(bit);
            TEST_EXPECT(ctx, default_bit.accepted() == ((default_features & bit) != 0u));
        }
        else
        {
            TEST_EXPECT(ctx, selected.status == EDocumentPolicyStatus::invalid_options);
            TEST_EXPECT(ctx, selected.unknown_policy_bits == bit);
            TEST_EXPECT(ctx, !selected.accepted());
            //  This evaluates feature permissions only, not processing success.
            TEST_EXPECT(ctx, excluded.accepted());
        }
    }

    const EDocumentFinding compatibility_forms[]{ EDocumentFinding::modified_nul, EDocumentFinding::cesu8_pair };
    for (const EDocumentFinding form : compatibility_forms)
    {
        const auto result = document_policy::evaluate(EDocumentFinding::non_ascii_utf8 | form,
            { document_finding_bit(form) });
        TEST_EXPECT(ctx, result.accepted());
        TEST_EXPECT(ctx, result.effective_allowed_features == (EDocumentFinding::non_ascii_utf8 | form));
        TEST_EXPECT(ctx, document_policy::evaluate(document_findings::k_relaxed, { document_finding_bit(form) }).disallowed_features ==
            document_findings::k_relaxed);
    }
    TEST_EXPECT(ctx, document_policy::evaluate(document_policy::k_modified_utf8,
        { document_policy::k_modified_utf8 }).accepted());
    const auto narrower = document_policy::evaluate(EDocumentFinding::hexadecimal | EDocumentFinding::comments,
        { document_finding_bit(EDocumentFinding::hexadecimal) });
    TEST_EXPECT(ctx, narrower.disallowed_features == document_finding_bit(EDocumentFinding::comments));
    const auto invalid = document_policy::evaluate(default_features, { default_features | (1u << 31) });
    TEST_EXPECT(ctx, invalid.status == EDocumentPolicyStatus::invalid_options);
    TEST_EXPECT(ctx, invalid.unknown_policy_bits == (1u << 31));

    //  Either hexadecimal spelling needs the base permission; # needs both.
    const auto alternate_hex = EDocumentFinding::hexadecimal | EDocumentFinding::alternate_hexadecimal_prefix;
    TEST_EXPECT(ctx, document_policy::evaluate(document_finding_bit(EDocumentFinding::hexadecimal)).accepted());
    TEST_EXPECT(ctx, document_policy::evaluate(alternate_hex).accepted());
    TEST_EXPECT(ctx, document_policy::evaluate(alternate_hex,
        { document_finding_bit(EDocumentFinding::hexadecimal) }).disallowed_features ==
        document_finding_bit(EDocumentFinding::alternate_hexadecimal_prefix));
    TEST_EXPECT(ctx, document_policy::evaluate(alternate_hex, { alternate_hex }).accepted());
    TEST_EXPECT(ctx, document_policy::evaluate(alternate_hex,
        { document_finding_bit(EDocumentFinding::alternate_hexadecimal_prefix) }).disallowed_features ==
            document_finding_bit(EDocumentFinding::hexadecimal));

    //  The same permission admits either implicit body; root kind belongs to the document.
    const auto implicit_body = document_finding_bit(EDocumentFinding::implicit_body);
    TEST_EXPECT(ctx, document_policy::evaluate(implicit_body).disallowed_features == implicit_body);
    TEST_EXPECT(ctx, document_policy::evaluate(implicit_body, { implicit_body }).accepted());
}

static void test_late_policy_acceptance(TTestContext& ctx)
{
    struct CCase { const char* source; std::uint32_t disallowed; };
    const CCase cases[]{
        { "", 0u }, { "{}", 0u }, { "[]", 0u }, { "42", 0u }, { "\"text\"", 0u },
        { "{\"\":\"\\u0000\\u00e9\"}", 0u }, { "[{\"x\":1}]", 0u },
        { "[+1,0b10,0x10,#10]", 0u },
        { "/*comment*/{}", document_finding_bit(EDocumentFinding::comments) },
        { "{name:1}", document_finding_bit(EDocumentFinding::unquoted_names) },
        { "[text]", document_finding_bit(EDocumentFinding::unquoted_strings) },
        { "['text']", document_finding_bit(EDocumentFinding::single_quotes) },
        { "[1,]", document_finding_bit(EDocumentFinding::trailing_commas) },
        { "[\"raw\nline\"]", document_finding_bit(EDocumentFinding::raw_quoted_line_breaks) },
        { "[\"raw\tcontrol\"]", document_finding_bit(EDocumentFinding::raw_quoted_controls) },
        { "{\"x\":1,\"x\":2}", document_finding_bit(EDocumentFinding::name_collision_extension) },
        { "\"x\":1", document_finding_bit(EDocumentFinding::implicit_body) },
        { "1,2", document_finding_bit(EDocumentFinding::implicit_body) },
        { ";comment only", document_finding_bit(EDocumentFinding::comments) },
        { "/*all*/ 'x':word,'x':+0x10,", EDocumentFinding::comments | EDocumentFinding::single_quotes |
            EDocumentFinding::unquoted_strings | EDocumentFinding::name_collision_extension |
            EDocumentFinding::trailing_commas | EDocumentFinding::implicit_body }
    };
    for (const auto& item : cases)
    {
        for (const bool request_linter : { false, true })
        {
            CLiveDocument destination;
            TEST_EXPECT(ctx, parse("{\"keep\":7}", destination).accepted());
            const CNodeKey keep = destination.first_child(destination.root());
            const std::uint64_t allocation_size = destination.memory_attribution().allocation_size;
            const CStringView source{ item.source };
            const CByteConstView bytes{ source.string(), source.length() };
            CTextLintReport linter_report;
            CTextLintReport* const details = request_linter ? &linter_report : nullptr;
            CDocumentReport report = document_parser::parse(bytes, destination, {}, details);
            TEST_CASE_EXPECT_TRUE(ctx, item.source, report.accepted() == (item.disallowed == 0u));
            TEST_EXPECT(ctx, report.processing_succeeded());
            TEST_EXPECT(ctx, linter_report.success == request_linter);
            TEST_EXPECT(ctx, report.policy.disallowed_features == item.disallowed && report.policy.unknown_policy_bits == 0u);
            TEST_EXPECT(ctx, report.policy.effective_allowed_features == document_policy::k_default);
            TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::none && report.failure.reason == EDocumentFailureReason::none);
            TEST_EXPECT(ctx, !report.failure.element_start.available && !report.failure.location.available);
            if (item.disallowed != 0u)
            {
                TEST_EXPECT(ctx, (report.processing_succeeded() && report.policy.status == EDocumentPolicyStatus::rejected));
                TEST_EXPECT(ctx, report.policy.status == EDocumentPolicyStatus::rejected);
                TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);
                TEST_EXPECT(ctx, destination.memory_attribution().allocation_size == allocation_size);
                TEST_EXPECT(ctx, write(ctx, destination) == "{\"keep\":7}");
            }
            else
            {
                TEST_EXPECT(ctx, report.state == EDocumentProcessingState::success && report.policy.accepted());
            }
            const std::uint32_t findings = report.findings;
            const CDocumentParseOptions options{ document_policy::k_all_supported };
            report = document_parser::parse(bytes, destination, options, details);
            TEST_EXPECT(ctx, report.accepted() && report.policy.accepted() && report.findings == findings);
            TEST_EXPECT(ctx, destination.is_complete() && destination.check_integrity());
            const auto narrower = document_policy::evaluate(report.findings);
            TEST_EXPECT(ctx, narrower.disallowed_features == item.disallowed);
        }
    }
    CLiveDocument destination;
    auto report = parse_text(CStringView{ "{\"x\":1,\"x\":2}" }, destination);
    TEST_EXPECT(ctx, (report.processing_succeeded() && report.policy.status == EDocumentPolicyStatus::rejected) && !destination.is_ready());
    TEST_EXPECT(ctx, report.processing_succeeded() && report.policy.disallowed_features ==
        document_finding_bit(EDocumentFinding::name_collision_extension));
    report = parse_text(CStringView{ "[{\"x\":1}]" }, destination, { document_policy::k_ascii });
    TEST_EXPECT(ctx, report.accepted() && report.policy.accepted());
    TEST_EXPECT(ctx, report.findings == document_finding_bit(EDocumentFinding::singleton_normalization));
    //  Invalid options are diagnosed after processing too, including known
    //  informational bits. No policy outcome invents a terminal failure.
    for (const std::uint32_t bit : { 1u << 31, document_finding_bit(EDocumentFinding::logical_nul) })
    {
        report = parse_text(CStringView{ "/*comment*/{}" }, destination, { bit });
        TEST_EXPECT(ctx, (report.processing_succeeded() && report.policy.status == EDocumentPolicyStatus::invalid_options));
        TEST_EXPECT(ctx, report.policy.status == EDocumentPolicyStatus::invalid_options && report.policy.unknown_policy_bits == bit);
        TEST_EXPECT(ctx, report.failure.reason == EDocumentFailureReason::none && !report.failure.location.available);
        TEST_EXPECT(ctx, write(ctx, destination) == "[{\"x\":1}]");
    }
    //  The source can alias the destination on either acceptance or rejection.
    TEST_EXPECT(ctx, parse("{\"s\":\"/*comment*/[]\"}", destination).accepted());
    const CStringView alias = destination.string_value(destination.first_child(destination.root()));
    report = parse_text(alias, destination);
    TEST_EXPECT(ctx, (report.processing_succeeded() && report.policy.status == EDocumentPolicyStatus::rejected));
    TEST_EXPECT(ctx, destination.string_value(destination.first_child(destination.root())) == alias);
    report = parse_text(alias, destination, { document_policy::k_all_supported });
    TEST_EXPECT(ctx, report.accepted() && write(ctx, destination) == "[]");
}

static void test_policy_processing_precedence(TTestContext& ctx)
{
    struct CCase { const char* source; EDocumentFailureStage stage; EDocumentFailureReason reason; };
    const CCase cases[]{
        { "\x81", EDocumentFailureStage::linter, EDocumentFailureReason::undefined_cp1252_byte },
        { "\xef\xbb\xbf\xff", EDocumentFailureStage::linter, EDocumentFailureReason::utf8_decode },
        { "/*comment*/{a:[1}", EDocumentFailureStage::structure, EDocumentFailureReason::mismatched_delimiter },
        { "{a:1e9999,bad:}", EDocumentFailureStage::structure, EDocumentFailureReason::missing_value },
        { "/*comment*/[+1e9999]", EDocumentFailureStage::parser, EDocumentFailureReason::numeric_out_of_range },
        { "{\"a\":1,\"a\":2,\"b\":18446744073709551616}", EDocumentFailureStage::parser, EDocumentFailureReason::numeric_out_of_range },
    };
    CLiveDocument destination;
    TEST_EXPECT(ctx, parse("{\"keep\":7}", destination).accepted());
    for (const auto& item : cases)
    {
        for (const std::uint32_t permissions : { document_policy::k_ascii, 1u << 31, document_policy::k_all_supported })
        {
            const auto report = parse_text(CStringView{ item.source }, destination, { permissions });
            TEST_CASE_EXPECT_TRUE(ctx, item.source, report.failure.stage == item.stage && report.failure.reason == item.reason);
            TEST_EXPECT(ctx, !report.accepted() && !report.processing_succeeded());
            TEST_EXPECT(ctx, report.policy.status == EDocumentPolicyStatus::unexamined);
            TEST_EXPECT(ctx, report.policy.disallowed_features == 0u && report.policy.unknown_policy_bits == 0u);
            TEST_EXPECT(ctx, report.failure.location.available);
            TEST_EXPECT(ctx, write(ctx, destination) == "{\"keep\":7}");
        }
    }
}

static void test_policy_source_acceptance(TTestContext& ctx)
{
    CTextLintReport linter_report;
    struct CCase { const char* source; std::uint32_t encoding; const char* output; };
    const CCase cases[]{
        { "{\"s\":\"\\u00e9\\u0000\"}", 0u, "{\"s\":\"\\u00e9\\u0000\"}" },
        { "{\"s\":\"\xc3\xa9\"}", document_finding_bit(EDocumentFinding::non_ascii_utf8), "{\"s\":\"\\u00e9\"}" },
        { "{\"s\":\"\xc0\x80\"}", document_finding_bit(EDocumentFinding::modified_nul), "{\"s\":\"\\u0000\"}" },
        { "{\"s\":\"\xed\xa0\x80\xed\xb0\x80\"}", document_finding_bit(EDocumentFinding::cesu8_pair), "{\"s\":\"\\ud800\\udc00\"}" },
        { "{\"s\":\"\x80\"}", document_finding_bit(EDocumentFinding::cp1252), "{\"s\":\"\\u20ac\"}" }
    };
    for (const auto& item : cases)
    {
        CLiveDocument destination;
        TEST_EXPECT(ctx, parse("{\"keep\":7}", destination).accepted());
        auto report = parse_text(CStringView{ item.source }, destination, { document_policy::k_ascii }, &linter_report);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, report.accepted() == (item.encoding == 0u));
        TEST_EXPECT(ctx, linter_report.success && report.processing_succeeded());
        TEST_EXPECT(ctx, report.policy.disallowed_features == (report.findings & document_findings::k_encoding_features));
        if (item.encoding != 0u)
        {
            TEST_EXPECT(ctx, (report.processing_succeeded() && report.policy.status == EDocumentPolicyStatus::rejected) && report.policy.disallowed_features != 0u);
            TEST_EXPECT(ctx, write(ctx, destination) == "{\"keep\":7}");
        }
        report = parse_text(CStringView{ item.source }, destination, { item.encoding });
        TEST_EXPECT(ctx, report.accepted() && report.policy.accepted());
        TEST_CASE_EXPECT_TRUE(ctx, item.output, write(ctx, destination, EDocumentWriteMode::strict_json) == item.output);
        report = parse_text(CStringView{ item.source }, destination);
        TEST_EXPECT(ctx, report.accepted() && report.policy.accepted());
        if (item.encoding == document_finding_bit(EDocumentFinding::cp1252))
        {
            report = parse_text(CStringView{ item.source }, destination, { document_policy::k_utf8 }, &linter_report);
            TEST_EXPECT(ctx, (report.processing_succeeded() && report.policy.status == EDocumentPolicyStatus::rejected));
            TEST_EXPECT(ctx, report.policy.disallowed_features == item.encoding);
            TEST_EXPECT(ctx, linter_report.recovered_as_cp1252 && linter_report.utf8_attempt_errors != 0u);
            TEST_EXPECT(ctx, !linter_report.first_failure.present && report.failure.reason == EDocumentFailureReason::none);
        }
    }
    CLiveDocument destination;
    const char evidence[]{ '\xef', '\xbb', '\xbf', '{', '"', 's', '"', ':', '"', 0, '"', '}', 0, 0 };
    const auto report = parse_text(CStringView{ evidence, sizeof(evidence) }, destination, { document_policy::k_ascii });
    TEST_EXPECT(ctx, report.accepted() && report.policy.accepted());
    const std::uint32_t observations = EDocumentFinding::leading_bom | EDocumentFinding::stripped_utf8_bom |
        EDocumentFinding::stripped_terminal_zeros | EDocumentFinding::literal_source_nul | EDocumentFinding::logical_nul;
    TEST_EXPECT(ctx, (report.findings & observations) == observations);
    TEST_EXPECT(ctx, write(ctx, destination) == "{\"s\":\"\\u0000\"}");
    //  Numeric feature permissions are independent and both hexadecimal
    //  permissions are required for the alternate spelling.
    const auto hexadecimal = document_finding_bit(EDocumentFinding::hexadecimal);
    auto numeric = parse_text(CStringView{ "[0x10,#10,+1,0b1]" }, destination, { hexadecimal });
    TEST_EXPECT(ctx, (numeric.processing_succeeded() && numeric.policy.status == EDocumentPolicyStatus::rejected));
    TEST_EXPECT(ctx, numeric.policy.disallowed_features == (EDocumentFinding::alternate_hexadecimal_prefix |
        EDocumentFinding::explicit_plus | EDocumentFinding::binary));
    numeric = parse_text(CStringView{ "[0x10]" }, destination, { hexadecimal });
    TEST_EXPECT(ctx, numeric.accepted());
}

static void test_policy_source_provenance(TTestContext& ctx)
{
    const auto escaped = text_linter::lint(CStringView{ "{\"text\":\"\\u00e9\\u0000\"}" });
    TEST_EXPECT(ctx, escaped.report.success);
    TEST_EXPECT(ctx, (escaped.report.source_findings & k_text_source_encoding_features) == 0u);
    TEST_EXPECT(ctx, document_policy::evaluate(document_findings::from_source(escaped.report.source_findings),
        { document_policy::k_ascii }).accepted());

    const std::uint8_t cp1252[]{ 0x80u };
    const auto converted = text_linter::lint(CByteConstView{ cp1252, sizeof(cp1252) });
    TEST_EXPECT(ctx, converted.report.success && converted.report.recovered_as_cp1252);
    const auto converted_findings = document_findings::from_source(converted.report.source_findings);
    const auto rejected = document_policy::evaluate(converted_findings, { document_policy::k_utf8 });
    TEST_EXPECT(ctx, rejected.disallowed_features == document_finding_bit(EDocumentFinding::cp1252));
    TEST_EXPECT(ctx, document_policy::evaluate(converted_findings).accepted());
    TEST_EXPECT(ctx, (converted_findings & document_finding_bit(EDocumentFinding::utf8_attempt_failed)) != 0u);
    TEST_EXPECT(ctx, !converted.report.first_failure.present);

    const std::uint8_t undefined[]{ 0x81u };
    const auto failed = text_linter::lint(CByteConstView{ undefined, sizeof(undefined) });
    TEST_EXPECT(ctx, !failed.report.success && failed.report.first_failure.present);
    TEST_EXPECT(ctx, failed.report.first_failure.reason == ETextLintFailure::undefined_cp1252_byte);
    const auto failed_findings = document_findings::from_source(failed.report.source_findings);
    TEST_EXPECT(ctx, (failed.report.source_findings & text_source_finding_bit(ETextSourceFinding::undefined_cp1252_byte)) != 0u);
    TEST_EXPECT(ctx, (failed_findings & text_source_finding_bit(ETextSourceFinding::undefined_cp1252_byte)) == 0u);
    TEST_EXPECT(ctx, (failed_findings & document_finding_bit(EDocumentFinding::utf8_attempt_failed)) != 0u);

    const std::uint8_t nul[]{ 'a', 0u, 'b' };
    const auto literal = text_linter::lint(CByteConstView{ nul, sizeof(nul) });
    TEST_EXPECT(ctx, literal.report.success);
    TEST_EXPECT(ctx, (literal.report.source_findings & document_finding_bit(EDocumentFinding::literal_source_nul)) != 0u);
    const auto literal_findings = document_findings::from_source(literal.report.source_findings);
    TEST_EXPECT(ctx, document_policy::evaluate(literal_findings, { document_policy::k_ascii }).accepted());
    const auto logical = document_policy::evaluate(literal_findings | EDocumentFinding::logical_nul,
        { document_policy::k_ascii });
    TEST_EXPECT(ctx, logical.accepted());
}

static void test_composed_findings(TTestContext& ctx)
{
    CLiveDocument destination;
    TEST_EXPECT(ctx, destination.initialise());
    const CNodeKey keep = destination.create_null(CStringView{ "keep" });
    TEST_EXPECT(ctx, destination.append_child(destination.root(), keep).succeeded());
    const auto failed = parse("a:1,a:2,b:[{n:1}],bad:18446744073709551616", destination);
    TEST_EXPECT(ctx, failed.failure.reason == EDocumentFailureReason::numeric_out_of_range);
    TEST_EXPECT(ctx, failed.failure.stage == EDocumentFailureStage::parser && !failed.processing_succeeded());
    TEST_EXPECT(ctx, failed.findings == (EDocumentFinding::unquoted_names | EDocumentFinding::implicit_body |
        EDocumentFinding::name_collision_extension | EDocumentFinding::singleton_normalization));
    TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep && destination.check_integrity());

    const std::uint8_t cp1252[]{ '{', 's', ':', '\'', 0x80u, '\'', ',', 'n', ':', '}' };
    CTextLintReport linter_report;
    const auto malformed = document_parser::parse(CByteConstView{ cp1252, sizeof(cp1252) }, destination, {}, &linter_report);
    TEST_EXPECT(ctx, malformed.state == EDocumentProcessingState::failure);
    TEST_EXPECT(ctx, linter_report.success && !malformed.processing_succeeded());
    TEST_EXPECT(ctx, malformed.findings == (EDocumentFinding::cp1252 | EDocumentFinding::utf8_attempt_failed |
        EDocumentFinding::unquoted_names | EDocumentFinding::single_quotes));
    TEST_EXPECT(ctx, malformed.failure.stage == EDocumentFailureStage::structure && malformed.failure.reason == EDocumentFailureReason::missing_value);
    TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep && destination.check_integrity());

    const std::uint8_t undefined[]{ 0x81u };
    const auto undecodable = document_parser::parse(CByteConstView{ undefined, sizeof(undefined) }, destination, {}, &linter_report);
    TEST_EXPECT(ctx, undecodable.state == EDocumentProcessingState::failure);
    TEST_EXPECT(ctx, !linter_report.success && undecodable.failure.stage == EDocumentFailureStage::linter);
    TEST_EXPECT(ctx, undecodable.findings == document_findings::from_source(linter_report.source_findings));
    TEST_EXPECT(ctx, (undecodable.findings & document_finding_bit(EDocumentFinding::reserved_undefined_cp1252_byte)) == 0u);
    TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);

    const auto empty_name = parse("{\"\":1}", destination);
    TEST_EXPECT(ctx, empty_name.accepted());
    TEST_EXPECT(ctx, empty_name.findings == document_finding_bit(EDocumentFinding::empty_member_name));

    const auto success = parse_text(CStringView{ "{\"s\":\"\\u00e9\\u0000\"}" }, destination);
    TEST_EXPECT(ctx, success.accepted());
    TEST_EXPECT(ctx, success.findings == document_finding_bit(EDocumentFinding::logical_nul));
}

static void test_shared_failures(TTestContext& ctx)
{
    CDocumentReport report;
    TEST_EXPECT(ctx, report.state == EDocumentProcessingState::unprocessed && !report.accepted());
    TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::none && report.failure.reason == EDocumentFailureReason::none);
    CLiveDocument destination;
    TEST_EXPECT(ctx, parse("{\"keep\":7}", destination).accepted());
    const CNodeKey keep = destination.first_child(destination.root());
    struct CCase
    {
        const char* source;
        EDocumentFailureStage stage;
        EDocumentFailureReason reason;
    };
    const CCase cases[]{
        { "{:1}", EDocumentFailureStage::structure, EDocumentFailureReason::missing_name },
        { "{\"a\" 1}", EDocumentFailureStage::structure, EDocumentFailureReason::missing_colon },
        { "{\"a\":}", EDocumentFailureStage::structure, EDocumentFailureReason::missing_value },
        { "[1 2]", EDocumentFailureStage::structure, EDocumentFailureReason::missing_separator },
        { "[1}", EDocumentFailureStage::structure, EDocumentFailureReason::mismatched_delimiter },
        { "[1", EDocumentFailureStage::structure, EDocumentFailureReason::unexpected_end },
        { "[] true", EDocumentFailureStage::structure, EDocumentFailureReason::trailing_content },
        { "/*", EDocumentFailureStage::structure, EDocumentFailureReason::unterminated_comment },
        { "\"unfinished", EDocumentFailureStage::structure, EDocumentFailureReason::unterminated_string },
        { "\"\\q\"", EDocumentFailureStage::structure, EDocumentFailureReason::invalid_escape },
        { "\"\\uD800\"", EDocumentFailureStage::structure, EDocumentFailureReason::invalid_surrogate_pair },
        { "{\"a\\nb\":1}", EDocumentFailureStage::structure, EDocumentFailureReason::newline_in_name },
        { "{n:1e9999,bad:}", EDocumentFailureStage::structure, EDocumentFailureReason::missing_value },
        { "18446744073709551616", EDocumentFailureStage::parser, EDocumentFailureReason::numeric_out_of_range },
        { "n:1e9999,r:{$morphic:{v:2,type:'recovered-array',values:[]}}", EDocumentFailureStage::parser, EDocumentFailureReason::numeric_out_of_range }
    };
    for (const auto& item : cases)
    {
        report = parse(item.source, destination);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, !report.accepted());
        TEST_CASE_EXPECT_EQ(ctx, item.source, report.failure.stage, item.stage);
        TEST_CASE_EXPECT_EQ(ctx, item.source, report.failure.reason, item.reason);
        TEST_EXPECT(ctx, report.failure.element_start.available && report.failure.location.available);
        TEST_EXPECT(ctx, !report.processing_succeeded() && destination.first_child(destination.root()) == keep);
    }

    CTextLintReport linter_report;
    const CCase decoding[]{
        { "\x81", EDocumentFailureStage::linter, EDocumentFailureReason::undefined_cp1252_byte },
        { "\xef\xbb\xbf\xff", EDocumentFailureStage::linter, EDocumentFailureReason::utf8_decode }
    };
    for (const auto& item : decoding)
    {
        report = parse_text(CStringView{ item.source }, destination, {}, &linter_report);
        TEST_EXPECT(ctx, report.state == EDocumentProcessingState::failure && report.failure.stage == item.stage);
        TEST_EXPECT(ctx, report.failure.reason == item.reason && linter_report.first_failure.present);
        TEST_EXPECT(ctx, !report.processing_succeeded() && !report.failure.element_start.available);

        TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);
    }

    report = parse_text(CStringView{ "{\"s\":\"\x80\"}" }, destination, {}, &linter_report);
    TEST_EXPECT(ctx, report.accepted() && linter_report.recovered_as_cp1252 && linter_report.utf8_attempt_errors != 0u);
    TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::none && report.failure.reason == EDocumentFailureReason::none);
    TEST_EXPECT(ctx, !linter_report.first_failure.present && report.failure.reason == EDocumentFailureReason::none);
    report = parse("[]", destination);
    TEST_EXPECT(ctx, report.accepted() && report.failure.stage == EDocumentFailureStage::none && report.failure.reason == EDocumentFailureReason::none);
}

static void test_unquoted_strings(TTestContext& ctx)
{
    struct CCase
    {
        const char* source;
        const char* value;
    };
    const CCase cases[]{
        { "hello", "hello" }, { "123abc", "123abc" }, { "1e+", "1e+" },
        { "01", "01" }, { "-01", "-01" }, { ".5", ".5" }, { "1.", "1." },
        { "+", "+" }, { "--1", "--1" }, { "+0x", "+0x" }, { "#", "#" },
        { "0b2", "0b2" }, { "0x1g", "0x1g" }, { "1e99999x", "1e99999x" },
        { "NaN", "NaN" }, { "Infinity", "Infinity" }, { "True", "True" },
        { "tr\\u0075e", "true" }, { "\\u0031", "1" }, { "\\u002B0x10", "+0x10" },
        { "a\\u002Cb", "a,b" }, { "a\\u0020b", "a b" }, { "a\\u003Ab", "a:b" },
        { "\\u007B\\u005B\\u005D\\u007D", "{[]}" }, { "\\\"quoted\\\"", "\"quoted\"" },
        { "don't", "don't" }, { "path//part", "path//part" }, { "path/*part*/", "path/*part*/" },
        { "/", "/" }, { "\\/\\/text", "//text" }, { "\xc3\xa9", "\xc3\xa9" },
        { "a\\nb", "a\nb" }, { "alpha;beta", "alpha;beta" }
    };
    CLiveDocument document;
    for (const auto& item : cases)
    {
        const auto report = parse(std::string("{\"s\":") + item.source + "}", document);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, report.accepted());
        TEST_EXPECT(ctx, report.findings == (document_finding_bit(EDocumentFinding::unquoted_strings) |
            ((static_cast<unsigned char>(item.source[0]) >= 0x80u) ? document_finding_bit(EDocumentFinding::non_ascii_utf8) : 0u)));
        const CNodeKey value = member(document, document.root(), CStringView{ "s" });
        TEST_EXPECT(ctx, document.value_type(value) == ELiveValueType::string);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, document.string_value(value) == CStringView{ item.value });
    }
    const CCase names[]{ { "0x10", "0x10" }, { "+#F", "+#F" }, { "0b10", "0b10" },
        { "1e99999", "1e99999" }, { "true", "true" }, { "tr\\u0075e", "true" },
        { "\\u0031", "1" }, { "a\\u003Ab", "a:b" }, { "\xc3\xa9", "\xc3\xa9" },
        { "name/*text*/", "name/*text*/" }, { "alpha;beta", "alpha;beta" } };
    for (const auto& item : names)
    {
        const auto report = parse(std::string("{") + item.source + ":null}", document);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, report.accepted());
        TEST_EXPECT(ctx, report.findings == (document_finding_bit(EDocumentFinding::unquoted_names) |
            ((static_cast<unsigned char>(item.source[0]) >= 0x80u) ? document_finding_bit(EDocumentFinding::non_ascii_utf8) : 0u)));
        const CNodeKey value = member(document, document.root(), CStringView{ item.value });
        TEST_EXPECT(ctx, value.is_valid() && document.value_type(value) == ELiveValueType::null_value);
    }
    const auto typed = parse(R"({"n":+#F,"b":true,"z":null,"f":1e2})", document);
    TEST_EXPECT(ctx, typed.accepted());
    TEST_EXPECT(ctx, typed.findings == (EDocumentFinding::explicit_plus | EDocumentFinding::hexadecimal |
        EDocumentFinding::alternate_hexadecimal_prefix));
    TEST_EXPECT(ctx, document.value_type(member(document, document.root(), CStringView{ "n" })) == ELiveValueType::integer);
    TEST_EXPECT(ctx, document.value_type(member(document, document.root(), CStringView{ "b" })) == ELiveValueType::boolean);
    TEST_EXPECT(ctx, document.value_type(member(document, document.root(), CStringView{ "f" })) == ELiveValueType::floating_point);

    const auto comments = parse(R"({"s":abc/*literal*/,"n":1 /*comment*/})", document);
    TEST_EXPECT(ctx, comments.accepted());
    TEST_EXPECT(ctx, comments.findings == (EDocumentFinding::unquoted_strings | EDocumentFinding::comments));
    TEST_EXPECT(ctx, document.string_value(member(document, document.root(), CStringView{ "s" })) == CStringView{ "abc/*literal*/" });
    const char* const semicolon_comments[]{ ";head\n{\"s\":\"value\"}", "{\"s\":;note\n\"value\"}",
        "{\"s\":;\"'{}[]:,/* \\q\n\"value\"};tail" };
    for (const char* source : semicolon_comments)
    {
        const auto report = parse(source, document);
        TEST_CASE_EXPECT_TRUE(ctx, source, report.accepted());
        TEST_EXPECT(ctx, report.findings == document_finding_bit(EDocumentFinding::comments));
        TEST_EXPECT(ctx, document.string_value(member(document, document.root(), CStringView{ "s" })) == CStringView{ "value" });
    }
    const char* const comment_only[]{ ";", ";note", ";\"'{}[]:,/* \\q\n;tail" };
    for (const char* source : comment_only)
    {
        const auto report = parse(source, document);
        TEST_CASE_EXPECT_TRUE(ctx, source, report.accepted());
        TEST_EXPECT(ctx, report.findings == document_finding_bit(EDocumentFinding::comments));
        TEST_EXPECT(ctx, !document.first_child(document.root()).is_valid());
    }
    const auto quotes = parse(R"({"s":'say "hello", don\'t'})", document);
    TEST_EXPECT(ctx, quotes.accepted() && quotes.findings == document_finding_bit(EDocumentFinding::single_quotes));
    TEST_EXPECT(ctx, document.string_value(member(document, document.root(), CStringView{ "s" })) == CStringView{ "say \"hello\", don't" });

    const CNodeKey keep = document.first_child(document.root());
    const char* const malformed[]{ R"({"s":abc"tail"})", R"({"s":abc[]})", R"({"s":abc{}})",
        R"({"s":abc def})", R"({"s":abc\q})", R"({"s":abc\,def})", R"({"s":abc\ def})",
        R"({"s":abc\x41})", R"({"s":abc\'def})", R"({"s":'abc\"def'})",
        R"({"s":abc\uD800})", R"({"s":abc\uDC00})", R"({"s":abc\u12})",
        "{\"s\":;note\n}", "{\"s\":;note" };
    for (const char* source : malformed)
    {
        const auto report = parse(source, document);
        TEST_CASE_EXPECT_TRUE(ctx, source, report.state == EDocumentProcessingState::failure);
        TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::structure && !report.processing_succeeded());
        TEST_EXPECT(ctx, document.first_child(document.root()) == keep && document.check_integrity());
    }
    const std::string nul = std::string("{s:a") + '\0' + "b}";
    const auto embedded = parse(nul, document);
    TEST_EXPECT(ctx, embedded.accepted());
    TEST_EXPECT(ctx, embedded.findings == (EDocumentFinding::unquoted_names |
        EDocumentFinding::unquoted_strings | EDocumentFinding::logical_nul | EDocumentFinding::literal_source_nul));
    const std::uint8_t canonical[]{ 'a', 0xc0u, 0x80u, 'b' };
    TEST_EXPECT(ctx, document.string_value(member(document, document.root(), CStringView{ "s" })) ==
        (CStringView{ canonical, sizeof(canonical) }));
}

static void test_construction_and_features(TTestContext& ctx)
{
    CLiveDocument document;
    const auto report = parse("/*start*/ n:+0X7f, a:[null,true,false,1.5,'text',{},[],{x:2},], o:{b:-#80},", document);
    TEST_EXPECT(ctx, report.accepted());
    TEST_EXPECT(ctx, (report.findings & ~document_findings::k_semantic) == (EDocumentFinding::comments | EDocumentFinding::single_quotes |
        EDocumentFinding::unquoted_names | EDocumentFinding::trailing_commas | EDocumentFinding::implicit_body |
        EDocumentFinding::explicit_plus | EDocumentFinding::hexadecimal | EDocumentFinding::alternate_hexadecimal_prefix));
    TEST_EXPECT(ctx, document.is_ready() && document.is_complete() && document.check_integrity());
    TEST_EXPECT(ctx, write(ctx, document) == "{\"n\":+0x7f,\"a\":[null,true,false,1.5,\"text\",{},[],{\"x\":2}],\"o\":{\"b\":-#80}}");
    const CNodeKey array = member(document, document.root(), CStringView{ "a" });
    TEST_EXPECT(ctx, document.child_count(array) == 8u);
    const CNodeKey singleton = document.last_child(array);
    TEST_EXPECT(ctx, document.value_type(singleton) == ELiveValueType::integer && document.is_object_entry(singleton));
    TEST_EXPECT(ctx, document.name(singleton) == CStringView{ "x" });
    //  Strict syntax needs neither relaxation nor numeric-extension flags.
    const auto strict = parse("{\"true\":false,\"z\":null,\"a\":[1,2]}", document);
    TEST_EXPECT(ctx, strict.accepted());
    TEST_EXPECT(ctx, strict.findings == 0u);
    TEST_EXPECT(ctx, write(ctx, document) == "{\"true\":false,\"z\":null,\"a\":[1,2]}");
}

static void test_strings_and_ingestion(TTestContext& ctx)
{
    //  Escaped names and values require independent scratch lifetimes.
    const std::string text = "{\"n\\u0000\\u00e9\":\"\\u0000\\uD834\\uDD1E\\n\\t\\\\\\\"\\/\",e:'',s:'can\\'t',a:'raw\nline',b:'\\b\\f\\r'}";
    CLiveDocument document;
    const auto report = parse(text, document);
    TEST_EXPECT(ctx, report.accepted());
    const std::uint8_t name[]{ 'n', 0xc0u, 0x80u, 0xc3u, 0xa9u };
    const CNodeKey value = member(document, document.root(), CStringView{ name, sizeof(name) });
    TEST_EXPECT(ctx, value.is_valid());
    const std::uint8_t expected[]{ 0xc0u, 0x80u, 0xf0u, 0x9du, 0x84u, 0x9eu, '\n', '\t', '\\', '"', '/' };
    TEST_EXPECT(ctx, document.string_value(value) == (CStringView{ expected, sizeof(expected) }));
    TEST_EXPECT(ctx, document.string_value(member(document, document.root(), CStringView{ "e" })).length() == 0u);
    TEST_EXPECT(ctx, document.check_integrity());

    const std::uint8_t cp1252[]{ '{', 'n', ':', '"', 0xe9u, 0u, '\r', '\n', '"', '}' };
    const auto linted = text_linter::lint(CByteConstView{ cp1252, sizeof(cp1252) }, k_document_text_lint_line_endings);
    TEST_EXPECT(ctx, linted.report.success && linted.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, linted.report.embedded_nul_count == 1u && linted.report.normalised_line_endings == text_line_ending_bit(ETextLineEnding::crlf));
    const auto ingested = document_parser::parse(CByteConstView{ cp1252, sizeof(cp1252) }, document, { document_policy::k_all_supported });
    TEST_EXPECT(ctx, ingested.accepted());
    TEST_EXPECT(ctx, ingested.findings == (EDocumentFinding::unquoted_names |
        EDocumentFinding::raw_quoted_line_breaks | EDocumentFinding::logical_nul | EDocumentFinding::literal_source_nul |
        EDocumentFinding::cp1252 | EDocumentFinding::utf8_attempt_failed));
    TEST_EXPECT(ctx, write(ctx, document) == "{\"n\":\"\\u00e9\\u0000\n\"}");

    const std::uint8_t modified[]{ '{', 'n', ':', '"', 0xc0u, 0x80u, '"', '}' };
    const auto normalized = text_linter::lint(CByteConstView{ modified, sizeof(modified) }, k_document_text_lint_line_endings);
    TEST_EXPECT(ctx, normalized.report.success && normalized.report.modified_utf8_nul_count == 1u);
    TEST_EXPECT(ctx, document_parser::parse(CByteConstView{ modified, sizeof(modified) }, document, { document_policy::k_all_supported }).accepted());
    TEST_EXPECT(ctx, write(ctx, document) == "{\"n\":\"\\u0000\"}");

    const std::uint8_t offset_input[]{ 0xefu, 0xbbu, 0xbfu, '{', 'a', ':', '"', 0xc3u, 0xa9u, '"', ',', 'n', ':', '1', ' ', '2', '}', 0u };
    const auto shifted = text_linter::lint(CByteConstView{ offset_input, sizeof(offset_input) }, k_document_text_lint_line_endings);
    TEST_EXPECT(ctx, shifted.report.success && shifted.report.leading_utf8_bom_stripped);
    const auto failure = document_parser::parse(CByteConstView{ offset_input, sizeof(offset_input) }, document);
    TEST_EXPECT(ctx, failure.state == EDocumentProcessingState::failure);
    TEST_EXPECT(ctx, failure.failure.location.code_point_column_1_based == 12u);

    const std::uint8_t cp1252_error[]{ '{', 'a', ':', '"', 0xe9u, '"', ',', 'n', ':', '1', ' ', '2', '}' };
    const auto expanded = text_linter::lint(CByteConstView{ cp1252_error, sizeof(cp1252_error) }, k_document_text_lint_line_endings);
    TEST_EXPECT(ctx, expanded.report.success && expanded.report.recovered_as_cp1252);
    const auto expanded_failure = document_parser::parse(CByteConstView{ cp1252_error, sizeof(cp1252_error) }, document);
    TEST_EXPECT(ctx, expanded_failure.state == EDocumentProcessingState::failure);
    TEST_EXPECT(ctx, expanded_failure.failure.location.code_point_column_1_based == 12u);
}

static void test_empty_names(TTestContext& ctx)
{
    struct CCase
    {
        const char* source;
        ELiveValueType type;
    };
    const CCase cases[]{ { "null", ELiveValueType::null_value }, { "true", ELiveValueType::boolean },
        { "1", ELiveValueType::integer }, { "1.5", ELiveValueType::floating_point }, { "\"\"", ELiveValueType::string },
        { "{}", ELiveValueType::object }, { "[]", ELiveValueType::array } };
    CLiveDocument document;
    for (const auto& item : cases)
    {
        for (const char quote : { '"', '\'' })
        {
            const std::string source = std::string("{") + quote + quote + ":" + item.source + "}";
            const auto report = parse(source, document);
            TEST_CASE_EXPECT_TRUE(ctx, source.c_str(), report.accepted());
            const std::uint32_t findings = document_finding_bit(EDocumentFinding::empty_member_name) |
                ((quote == '\'') ? document_finding_bit(EDocumentFinding::single_quotes) : 0u);
            TEST_EXPECT(ctx, report.findings == findings);
            const CNodeKey node = document.object_child(document.root(), CStringView{ "" });
            TEST_EXPECT(ctx, node.is_valid() && document.is_object_entry(node));
            TEST_EXPECT(ctx, document.value_type(node) == item.type);
            TEST_EXPECT(ctx, !document.name(node).empty() && document.name(node).length() == 0u);
            TEST_EXPECT(ctx, !document.object_child(document.root(), CStringView{}).is_valid());
            TEST_EXPECT(ctx, write(ctx, document, EDocumentWriteMode::strict_json) == std::string("{\"\":") + item.source + "}");
            CBakedDocumentBlock block;
            TEST_EXPECT(ctx, document_translation::bake(document, block));
            CLiveDocument promoted;
            TEST_EXPECT(ctx, document_translation::promote(block.document(), promoted));
            const CNodeKey copy = promoted.object_child(promoted.root(), CStringView{ "" });
            TEST_EXPECT(ctx, copy.is_valid() && promoted.is_object_entry(copy) && promoted.value_type(copy) == item.type);
            TEST_EXPECT(ctx, promoted.name(copy).length() == 0u && !promoted.name(copy).empty());
        }
    }
    const auto nested = parse("{\"a\":[{\"\":\"line\nbreak\"}]}", document);
    TEST_EXPECT(ctx, nested.accepted());
    TEST_EXPECT(ctx, nested.findings == (EDocumentFinding::empty_member_name |
        EDocumentFinding::raw_quoted_line_breaks | EDocumentFinding::singleton_normalization));
    const CNodeKey child = document.first_child(document.object_child(document.root(), CStringView{ "a" }));
    TEST_EXPECT(ctx, document.is_object_entry(child) && !document.name(child).empty() && document.name(child).length() == 0u);
    TEST_EXPECT(ctx, document.string_value(child) == CStringView{ "line\nbreak" } && document.suppresses_newline_escaping(child));
    TEST_EXPECT(ctx, write(ctx, document) == "{\"a\":[{\"\":\"line\nbreak\"}]}");
}

static void test_newline_metadata(TTestContext& ctx)
{
    CLiveDocument document;
    const auto report = parse("{\"literal\":\"a\nb\",\"escaped\":\"a\\nb\",\"mixed\":'a\nb\\nc',\"plain\":\"\\\\n\",\"unquoted\":a\\nb}", document);
    TEST_EXPECT(ctx, report.accepted());
    TEST_EXPECT(ctx, report.findings == (EDocumentFinding::raw_quoted_line_breaks |
        EDocumentFinding::single_quotes | EDocumentFinding::unquoted_strings));
    const CNodeKey literal = member(document, document.root(), CStringView{ "literal" });
    const CNodeKey escaped = member(document, document.root(), CStringView{ "escaped" });
    const CNodeKey unquoted = member(document, document.root(), CStringView{ "unquoted" });
    TEST_EXPECT(ctx, document.string_value_id(literal) == document.string_value_id(escaped));
    TEST_EXPECT(ctx, document.string_value_id(literal) == document.string_value_id(unquoted));
    TEST_EXPECT(ctx, document.suppresses_newline_escaping(literal));
    TEST_EXPECT(ctx, !document.suppresses_newline_escaping(escaped) && !document.suppresses_newline_escaping(unquoted));
    TEST_EXPECT(ctx, document.suppresses_newline_escaping(member(document, document.root(), CStringView{ "mixed" })));
    TEST_EXPECT(ctx, !document.suppresses_newline_escaping(member(document, document.root(), CStringView{ "plain" })));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(document, block));
    CLiveDocument promoted;
    TEST_EXPECT(ctx, document_translation::promote(block.document(), promoted));
    TEST_EXPECT(ctx, promoted.suppresses_newline_escaping(member(promoted, promoted.root(), CStringView{ "literal" })));
    TEST_EXPECT(ctx, !promoted.suppresses_newline_escaping(member(promoted, promoted.root(), CStringView{ "escaped" })));
    const std::string strict = write(ctx, document, EDocumentWriteMode::strict_json);
    TEST_EXPECT(ctx, strict == "{\"literal\":\"a\\nb\",\"escaped\":\"a\\nb\",\"mixed\":\"a\\nb\\nc\",\"plain\":\"\\\\n\",\"unquoted\":\"a\\nb\"}");
    CLiveDocument reparsed;
    TEST_EXPECT(ctx, parse(strict, reparsed).accepted());
    TEST_EXPECT(ctx, !reparsed.suppresses_newline_escaping(member(reparsed, reparsed.root(), CStringView{ "literal" })));
    TEST_EXPECT(ctx, document.suppresses_newline_escaping(literal));
    const std::string morphic = write(ctx, promoted);
    TEST_EXPECT(ctx, morphic == "{\"literal\":\"a\nb\",\"escaped\":\"a\\nb\",\"mixed\":\"a\nb\nc\",\"plain\":\"\\\\n\",\"unquoted\":\"a\\nb\"}");
    TEST_EXPECT(ctx, parse(morphic, reparsed).accepted());
    TEST_EXPECT(ctx, reparsed.suppresses_newline_escaping(member(reparsed, reparsed.root(), CStringView{ "literal" })));
    TEST_EXPECT(ctx, !reparsed.suppresses_newline_escaping(member(reparsed, reparsed.root(), CStringView{ "escaped" })));

    const char* const breaks[]{ "\n", "\r", "\r\n", "\n\r", "\v", "\f", "\xc2\x85", "\xe2\x80\xa8", "\xe2\x80\xa9" };
    for (const char* line_break : breaks)
    {
        const std::string source = std::string("{\"s\":\"a") + line_break + "b\"}";
        const auto ingested = parse_text(CStringView{ source.data(), source.size() }, document, { document_policy::k_all_supported });
        TEST_EXPECT(ctx, ingested.accepted());
        const CNodeKey value = document.first_child(document.root());
        TEST_EXPECT(ctx, document.string_value(value) == CStringView{ "a\nb" } && document.suppresses_newline_escaping(value));
        TEST_EXPECT(ctx, (ingested.findings & document_finding_bit(EDocumentFinding::raw_quoted_line_breaks)) != 0u);

        const std::string invalid = std::string("{\"a") + line_break + "b\":1}";
        const auto failed = parse_text(CStringView{ invalid.data(), invalid.size() }, document);
        TEST_EXPECT(ctx, failed.state == EDocumentProcessingState::failure);
        TEST_EXPECT(ctx, failed.failure.reason == EDocumentFailureReason::newline_in_name);
        TEST_EXPECT(ctx, failed.failure.stage == EDocumentFailureStage::structure && !failed.processing_succeeded());
        TEST_EXPECT(ctx, document.first_child(document.root()) == value && document.suppresses_newline_escaping(value));
    }
}

static void test_root_inference(TTestContext& ctx)
{
    struct CCase
    {
        const char* source;
        const char* output;
        std::uint32_t findings;
    };
    const CCase cases[]{
        { "", "{}", 0u }, { " \n\t", "{}", 0u },
        { ";empty", "{}", document_finding_bit(EDocumentFinding::comments) },
        { "{}", "{}", 0u }, { "[]", "[]", 0u },
        { "[1,true,null]", "[1,true,null]", 0u },
        { "{\"a\":1}", "{\"a\":1}", 0u },
        { "\"a\":1", "{\"a\":1}", document_finding_bit(EDocumentFinding::implicit_body) },
        { "123:1", "{\"123\":1}", EDocumentFinding::implicit_body | EDocumentFinding::unquoted_names },
        { "0x10:1", "{\"0x10\":1}", EDocumentFinding::implicit_body | EDocumentFinding::unquoted_names },
        { "\"\":1", "{\"\":1}", EDocumentFinding::implicit_body | EDocumentFinding::empty_member_name },
        { "42", "[42]", 0u }, { "1.5", "[1.5]", 0u }, { "true", "[true]", 0u },
        { "false", "[false]", 0u }, { "null", "[null]", 0u }, { "\"hello\"", "[\"hello\"]", 0u },
        { "1,true,\"hello\"", "[1,true,\"hello\"]", document_finding_bit(EDocumentFinding::implicit_body) },
        { "1,", "[1]", document_finding_bit(EDocumentFinding::trailing_commas) },
        { "+#F", "[+#f]", EDocumentFinding::explicit_plus | EDocumentFinding::hexadecimal | EDocumentFinding::alternate_hexadecimal_prefix },
        { "a\\u003Ab", "[\"a:b\"]", document_finding_bit(EDocumentFinding::unquoted_strings) },
        { "a\\u003Ab:1", "{\"a:b\":1}", EDocumentFinding::implicit_body | EDocumentFinding::unquoted_names },
        { "\"a\" /* : ignored */ :1", "{\"a\":1}", EDocumentFinding::implicit_body | EDocumentFinding::comments },
        { "\"a\" /* : ignored */", "[\"a\"]", document_finding_bit(EDocumentFinding::comments) },
        { "\"a\";ignored\n:1", "{\"a\":1}", EDocumentFinding::implicit_body | EDocumentFinding::comments },
        { "\"line\nbreak\"", "[\"line\nbreak\"]", document_finding_bit(EDocumentFinding::raw_quoted_line_breaks) }
    };
    CLiveDocument document;
    for (const auto& item : cases)
    {
        const auto report = parse(item.source, document);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, report.accepted() && report.processing_succeeded());
        TEST_CASE_EXPECT_EQ(ctx, item.source, report.findings, item.findings);
        const ELiveValueType root_type = (*item.output == '[') ? ELiveValueType::array : ELiveValueType::object;
        TEST_EXPECT(ctx, document.value_type(document.root()) == root_type);
        TEST_EXPECT(ctx, !document.is_object_entry(document.root()) && document.name(document.root()).empty());
        TEST_EXPECT(ctx, !document.parent(document.root()).is_valid() && document.check_integrity());
        TEST_CASE_EXPECT_TRUE(ctx, item.source, write(ctx, document) == item.output);
        CBakedDocumentBlock block;
        TEST_EXPECT(ctx, document_translation::bake(document, block));
        CLiveDocument promoted;
        TEST_EXPECT(ctx, document_translation::promote(block.document(), promoted));
        TEST_EXPECT(ctx, promoted.value_type(promoted.root()) == root_type);
        TEST_EXPECT(ctx, write(ctx, promoted) == item.output);
        CLiveDocument reparsed;
        TEST_EXPECT(ctx, parse(item.output, reparsed).accepted());
        TEST_EXPECT(ctx, reparsed.value_type(reparsed.root()) == root_type);
        TEST_EXPECT(ctx, write(ctx, reparsed) == item.output);
        if (item.findings == 0u)
        {
            TEST_EXPECT(ctx, document_policy::evaluate(report.findings).accepted());
        }
    }
    const auto normalized = parse("0,{\"\":\"line\nbreak\"},{},[]", document);
    TEST_EXPECT(ctx, normalized.accepted());
    TEST_EXPECT(ctx, normalized.findings == (EDocumentFinding::implicit_body | EDocumentFinding::empty_member_name |
        EDocumentFinding::raw_quoted_line_breaks | EDocumentFinding::singleton_normalization));
    const CNodeKey named = document.next_sibling(document.first_child(document.root()));
    TEST_EXPECT(ctx, document.is_object_entry(named) && !document.name(named).empty() && document.name(named).length() == 0u);
    TEST_EXPECT(ctx, document.suppresses_newline_escaping(named));
    TEST_EXPECT(ctx, write(ctx, document) == "[0,{\"\":\"line\nbreak\"},{},[]]");
    const auto explicit_array = parse("[{\"x\":1},{},{\"x\":1,\"y\":2}]", document);
    TEST_EXPECT(ctx, explicit_array.accepted() && explicit_array.findings == document_finding_bit(EDocumentFinding::singleton_normalization));
    TEST_EXPECT(ctx, document.is_object_entry(document.first_child(document.root())));
    TEST_EXPECT(ctx, write(ctx, document) == "[{\"x\":1},{},{\"x\":1,\"y\":2}]");

    const CNodeKey keep = document.first_child(document.root());
    const std::uint64_t allocation_size = document.memory_attribution().allocation_size;
    const char* const malformed[]{ "1 2", "[1}", "[1", "[] true", "{} ,1", "1,a:2", "\"a\":1,2", "[a:1]", "1,,2", "," };
    for (const char* source : malformed)
    {
        const auto failed = parse(source, document);
        TEST_CASE_EXPECT_TRUE(ctx, source, failed.state == EDocumentProcessingState::failure);
        TEST_EXPECT(ctx, failed.failure.stage == EDocumentFailureStage::structure && !failed.processing_succeeded());
        TEST_EXPECT(ctx, document.value_type(document.root()) == ELiveValueType::array && document.first_child(document.root()) == keep);
        TEST_EXPECT(ctx, document.memory_attribution().allocation_size == allocation_size && document.check_integrity());
    }
    const auto range = parse("18446744073709551616", document);
    TEST_EXPECT(ctx, range.failure.reason == EDocumentFailureReason::numeric_out_of_range);
    TEST_EXPECT(ctx, range.failure.stage == EDocumentFailureStage::parser && !range.processing_succeeded());
    TEST_EXPECT(ctx, range.findings == 0u && range.failure.location.code_point_column_1_based == 1u);
    TEST_EXPECT(ctx, document.first_child(document.root()) == keep && document.value_type(document.root()) == ELiveValueType::array);
    const auto ingested = parse_text(CStringView{ "\xef\xbb\xbf;head\r\n[1,true]" }, document, { document_policy::k_all_supported });
    TEST_EXPECT(ctx, ingested.accepted());
    TEST_EXPECT(ctx, document.value_type(document.root()) == ELiveValueType::array && write(ctx, document) == "[1,true]");
    const auto nul = parse("\\u0000", document);
    TEST_EXPECT(ctx, nul.accepted() && nul.findings == (EDocumentFinding::logical_nul | EDocumentFinding::unquoted_strings));
    const std::uint8_t canonical_nul[]{ 0xc0u, 0x80u };
    TEST_EXPECT(ctx, document.value_type(document.root()) == ELiveValueType::array);
    TEST_EXPECT(ctx, document.string_value(document.first_child(document.root())) == (CStringView{ canonical_nul, sizeof(canonical_nul) }));
    const auto terminal = parse(std::string(1u, '\0'), document);
    TEST_EXPECT(ctx, terminal.accepted() && terminal.findings == document_finding_bit(EDocumentFinding::stripped_terminal_zeros));
    TEST_EXPECT(ctx, document.value_type(document.root()) == ELiveValueType::object && document.value_count() == 1u);
}

static void test_integer_metadata(TTestContext& ctx)
{
    struct CCase
    {
        const char* spelling;
        bool signed_value;
        std::uint64_t magnitude;
        bool negative;
        EIntegerNotation notation;
        EIntegerPrefix prefix;
    };
    const CCase cases[]{
        { "0", false, 0u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+0", true, 0u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-0", true, 0u, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+127", true, 127u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+128", true, 128u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-128", true, 128u, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-129", true, 129u, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "255", false, 255u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "256", false, 256u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+32767", true, 32767u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-32769", true, 32769u, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "65536", false, 65536u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+2147483648", true, 2147483648u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "4294967296", false, 4294967296u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+9223372036854775807", true, 9223372036854775807ull, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-9223372036854775808", true, 9223372036854775808ull, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "18446744073709551615", false, 18446744073709551615ull, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "0XFF", false, 255u, false, EIntegerNotation::hexadecimal, EIntegerPrefix::standard },
        { "-#80", true, 128u, true, EIntegerNotation::hexadecimal, EIntegerPrefix::alternate },
        { "+#7FFF", true, 32767u, false, EIntegerNotation::hexadecimal, EIntegerPrefix::alternate },
        { "-0x8000000000000000", true, 9223372036854775808ull, true, EIntegerNotation::hexadecimal, EIntegerPrefix::standard },
        { "0xffffffffffffffff", false, 18446744073709551615ull, false, EIntegerNotation::hexadecimal, EIntegerPrefix::standard },
        { "+0B100000000", true, 256u, false, EIntegerNotation::binary, EIntegerPrefix::standard } };
    for (const auto& item : cases)
    {
        CLiveDocument document;
        TEST_CASE_EXPECT_TRUE(ctx, item.spelling, parse(std::string("n:") + item.spelling, document).accepted());
        const CNodeKey node = document.first_child(document.root());
        CIntegerMetadata metadata;
        TEST_EXPECT(ctx, document.integer_metadata(node, metadata));
        TEST_EXPECT(ctx, metadata.domain == (item.signed_value ? EIntegerDomain::signed_value : EIntegerDomain::unsigned_value));
        TEST_EXPECT(ctx, metadata.notation == item.notation && metadata.prefix == item.prefix);
        if (item.signed_value)
        {
            const std::int64_t expected = item.negative ? ((item.magnitude == 9223372036854775808ull) ? std::numeric_limits<std::int64_t>::min() :
                -static_cast<std::int64_t>(item.magnitude)) : static_cast<std::int64_t>(item.magnitude);
            std::int64_t value = 0;
            TEST_EXPECT(ctx, document.signed_integer_value(node, value) && value == expected);
            TEST_EXPECT(ctx, metadata.width == live_signed_integer_smallest_width(expected));
        }
        else
        {
            std::uint64_t value = 0u;
            TEST_EXPECT(ctx, document.unsigned_integer_value(node, value) && value == item.magnitude);
            TEST_EXPECT(ctx, metadata.width == live_unsigned_integer_smallest_width(item.magnitude));
        }
    }
}

static void expect_float(TTestContext& ctx, const char* spelling, const double expected)
{
    CLiveDocument document;
    TEST_CASE_EXPECT_TRUE(ctx, spelling, parse(std::string("f:") + spelling, document).accepted());
    double value = 0.0;
    TEST_EXPECT(ctx, document.floating_point_value(document.first_child(document.root()), value));
    TEST_EXPECT(ctx, live_floating_point_bits(value) == live_floating_point_bits(expected));
}

static void test_floats(TTestContext& ctx)
{
    expect_float(ctx, "-0.0", -0.0);
    expect_float(ctx, "+0.0", 0.0);
    expect_float(ctx, "0e999999999999999999999999999999", 0.0);
    expect_float(ctx, "0.1", 0.1);
    expect_float(ctx, "+1.2345678901234567", 1.2345678901234567);
    expect_float(ctx, "5e-324", std::numeric_limits<double>::denorm_min());
    expect_float(ctx, "2.2250738585072014e-308", std::numeric_limits<double>::min());
    expect_float(ctx, "1.7976931348623157e308", std::numeric_limits<double>::max());
    std::uint64_t bits = 0xa0761d6478bd642full;
    for (unsigned i = 0u; i < 256u; ++i)
    {
        bits ^= bits << 13u;
        bits ^= bits >> 7u;
        bits ^= bits << 17u;
        const double value = live_floating_point_from_bits(bits);
        if (!live_floating_point_is_finite(value))
        {
            continue;
        }
        char digits[64];
        const auto converted = std::to_chars(digits, digits + sizeof(digits), value);
        TEST_EXPECT(ctx, converted.ec == std::errc{});
        if (converted.ec != std::errc{})
        {
            continue;
        }
        std::string spelling(digits, converted.ptr);
        if (spelling.find_first_of(".e") == std::string::npos)
        {
            spelling += ".0";
        }
        expect_float(ctx, spelling.c_str(), value);
    }
}

static void test_failure_publication(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, parse("keep:7", document).accepted());
    const CNodeKey root = document.root();
    const CNodeKey keep = document.first_child(root);
    const std::uint64_t allocation_size = document.memory_attribution().allocation_size;
    struct CCase
    {
        const char* text;
        EDocumentFailureReason reason;
        std::size_t offset;
        bool structural_success;
    };
    const CCase cases[]{
        { "{n:1 2}", EDocumentFailureReason::missing_separator, 5u, false },
        { "n:18446744073709551616", EDocumentFailureReason::numeric_out_of_range, 2u, true },
        { "n:+9223372036854775808", EDocumentFailureReason::numeric_out_of_range, 2u, true },
        { "n:-9223372036854775809", EDocumentFailureReason::numeric_out_of_range, 2u, true },
        { "n:1e99999", EDocumentFailureReason::numeric_out_of_range, 2u, true },
        { "n:1e-99999", EDocumentFailureReason::numeric_out_of_range, 2u, true },
        { "good:1,n:0x10000000000000000", EDocumentFailureReason::numeric_out_of_range, 9u, true },
        { "good:1,'':", EDocumentFailureReason::missing_value, 10u, false },
        { "{\"a\\u2028b\":1}", EDocumentFailureReason::newline_in_name, 3u, false },
        { "a\\nb:1", EDocumentFailureReason::newline_in_name, 1u, false } };
    for (const auto& item : cases)
    {
        const auto report = parse(item.text, document);
        TEST_CASE_EXPECT_EQ(ctx, item.text, report.failure.reason, item.reason);
        TEST_EXPECT(ctx, report.state == EDocumentProcessingState::failure);
        TEST_EXPECT(ctx, report.failure.location.available && report.failure.location.code_point_column_1_based == item.offset + 1u);
        TEST_EXPECT(ctx, report.failure.stage == (item.structural_success ? EDocumentFailureStage::parser : EDocumentFailureStage::structure));
        TEST_EXPECT(ctx, document.root() == root && document.first_child(root) == keep);
        TEST_EXPECT(ctx, document.memory_attribution().allocation_size == allocation_size);
        TEST_EXPECT(ctx, document.check_integrity());
        TEST_EXPECT(ctx, write(ctx, document) == "{\"keep\":7}");
    }
    TEST_EXPECT(ctx, parse("$morphicx:1,s:'$morphic'", document).accepted());
    TEST_EXPECT(ctx, parse("", document).accepted());
    TEST_EXPECT(ctx, document.is_ready() && document.value_count() == 1u && document.child_count(document.root()) == 0u);
    //  Publication must happen after all reads, even when source aliases the
    //  destination's old string storage.
    TEST_EXPECT(ctx, parse("source:'new_value:42'", document).accepted());
    const CStringView alias = document.string_value(document.first_child(document.root()));
    TEST_EXPECT(ctx, parse_text(alias, document, { document_policy::k_all_supported }).accepted());
    TEST_EXPECT(ctx, write(ctx, document) == "{\"new_value\":42}");
}

static std::string array_body(const std::string& values)
{
    return "[" + values + "]";
}

static void test_collision_arrays(TTestContext& ctx)
{
    struct CCase { const char* source; const char* output; };
    const CCase cases[]{
        { "{\"x\":1,\"x\":2,\"x\":3}", "{\"x\":[1,2,3]}" },
        { "{\"x\":1,\"x\":[2,3]}", "{\"x\":[1,[2,3]]}" },
        { "{\"x\":[1,2],\"x\":3}", "{\"x\":[1,2,3]}" },
        { "{\"x\":[1,2],\"x\":[3,4]}", "{\"x\":[[1,2],[3,4]]}" },
        { "{\"x\":[1,2],\"x\":3,\"x\":[4,5],\"x\":6}", "{\"x\":[[1,2,3],[4,5],6]}" },
        { "{\"x\":[],\"x\":[]}", "{\"x\":[[],[]]}" },
        { "{\"x\":[],\"x\":null}", "{\"x\":[null]}" },
        { "{\"x\":null,\"x\":[]}", "{\"x\":[null,[]]}" },
        { "{\"first\":0,\"x\":true,\"middle\":1,\"x\":\"two\",\"x\":null,\"last\":2}",
          "{\"first\":0,\"x\":[true,\"two\",null],\"middle\":1,\"last\":2}" },
        { "{\"x\":{\"a\":1},\"x\":{\"b\":2}}", "{\"x\":[{\"a\":1},{\"b\":2}]}" },
        { "{\"x\":1.5,\"x\":-0.0}", "{\"x\":[1.5,-0.0]}" },
        { "{\"\":1,\"\":2}", "{\"\":[1,2]}" },
        { "{\"a\\u0000\":1,\"a\\u0000\":2}", "{\"a\\u0000\":[1,2]}" },
        { "{\"\\u0024morphic\":1,\"$morphic\":2,\"$$morphic\":3}", "{\"$morphic\":[1,2],\"$$morphic\":3}" },
        { "{\"x\":{\"d\":1,\"d\":2},\"x\":{\"d\":3}}", "{\"x\":[{\"d\":[1,2]},{\"d\":3}]}" }
    };
    for (const auto& item : cases)
    {
        CLiveDocument document;
        TEST_EXPECT(ctx, parse("{\"keep\":7}", document).accepted());
        const auto rejected = parse_text(CStringView{ item.source }, document);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, (rejected.processing_succeeded() && rejected.policy.status == EDocumentPolicyStatus::rejected));
        TEST_EXPECT(ctx, rejected.processing_succeeded() && rejected.policy.disallowed_features ==
            document_finding_bit(EDocumentFinding::name_collision_extension));
        TEST_EXPECT(ctx, write(ctx, document) == "{\"keep\":7}");
        const auto report = parse_text(CStringView{ item.source }, document,
            { document_policy::k_all_supported });
        TEST_EXPECT(ctx, report.accepted() && document.check_integrity());
        TEST_EXPECT(ctx, (report.findings & document_finding_bit(EDocumentFinding::name_collision_extension)) != 0u);
        CBakedDocumentBlock block;
        CLiveDocument promoted;
        TEST_EXPECT(ctx, document_translation::bake(document, block));
        TEST_EXPECT(ctx, document_translation::promote(block.document(), promoted));
        for (const EDocumentWriteMode mode : { EDocumentWriteMode::morphic, EDocumentWriteMode::strict_json })
        {
            TEST_CASE_EXPECT_TRUE(ctx, item.source, write(ctx, promoted, mode) == item.output);
            CLiveDocument reparsed;
            const auto round_trip = parse_text(CStringView{ item.output }, reparsed);
            TEST_EXPECT(ctx, round_trip.accepted());
            TEST_EXPECT(ctx, (round_trip.findings & document_finding_bit(EDocumentFinding::name_collision_extension)) == 0u);
            TEST_EXPECT(ctx, write(ctx, reparsed, mode) == item.output);
        }
    }
}

static void test_former_protocol_as_data(TTestContext& ctx)
{
    const char* const sources[]{
        "{\"$morphic\":null}", "{\"$morphic\":[]}", "{\"$morphic\":{}}",
        "{\"$morphic\":{\"v\":1,\"type\":\"recovered-array\",\"values\":[]}}",
        "{\"$morphic\":{\"v\":2,\"type\":\"unknown\",\"extra\":true}}",
        "{\"$morphic\":{\"v\":true,\"type\":null,\"values\":{}}}",
        "{\"$morphic\":{\"\":0,\"$$morphic\":1}}",
        "{\"extra\":0,\"$morphic\":{\"values\":[{\"n\":1},[2,3]]}}",
        "{\"$morphic\":0,\"$$morphic\":1,\"$$$morphic\":2,\"$morphicx\":3}",
        "[{\"$morphic\":{\"v\":1,\"type\":\"recovered-array\",\"values\":[]}}]"
    };
    for (const char* source : sources)
    {
        CLiveDocument document;
        const auto report = parse_text(CStringView{ source }, document);
        TEST_CASE_EXPECT_TRUE(ctx, source, report.accepted());
        TEST_EXPECT(ctx, report.failure.reason == EDocumentFailureReason::none);
        for (const EDocumentWriteMode mode : { EDocumentWriteMode::morphic, EDocumentWriteMode::strict_json })
        {
            TEST_CASE_EXPECT_TRUE(ctx, source, write(ctx, document, mode) == source);
        }
    }
    CLiveDocument document;
    auto report = parse_text(CStringView{ "{\"$morphic\":{\"v\":1,\"v\":2}}" }, document,
        { document_policy::k_all_supported });
    TEST_EXPECT(ctx, report.accepted());
    TEST_EXPECT(ctx, write(ctx, document) == "{\"$morphic\":{\"v\":[1,2]}}");
    report = parse_text(CStringView{ "{\"$morphic\":{\"v\":1e9999}}" }, document);
    TEST_EXPECT(ctx, report.state == EDocumentProcessingState::failure);
    TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::parser &&
        report.failure.reason == EDocumentFailureReason::numeric_out_of_range);
}

static void test_singleton_contexts(TTestContext& ctx)
{
    CLiveDocument document;
    const std::string text = "a:[{n:null},{b:true},{s:'text'},{i:+1},{f:-0.0},{o:{}},{a:[]},{r:" + array_body("") + "},"
        "{d:1,d:2},{},{x:1,y:2}," + array_body("{n:1},[{a:2}]," + array_body("{b:3}")) + "],o:{one:1}";
    const auto report = parse(text, document);
    TEST_EXPECT(ctx, report.accepted());
    const CNodeKey array = member(document, document.root(), CStringView{ "a" });
    const ELiveValueType types[]{ ELiveValueType::null_value, ELiveValueType::boolean, ELiveValueType::string,
        ELiveValueType::integer, ELiveValueType::floating_point, ELiveValueType::object, ELiveValueType::array,
        ELiveValueType::array, ELiveValueType::array, ELiveValueType::object,
        ELiveValueType::object, ELiveValueType::array };
    CNodeKey child = document.first_child(array);
    for (unsigned i = 0u; i < 12u; ++i)
    {
        TEST_EXPECT(ctx, document.value_type(child) == types[i]);
        TEST_EXPECT(ctx, document.is_object_entry(child) == (i < 9u));
        child = document.next_sibling(child);
    }
    TEST_EXPECT(ctx, !child.is_valid());
    const CNodeKey transport = document.last_child(array);
    TEST_EXPECT(ctx, document.value_type(document.first_child(transport)) == ELiveValueType::integer);
    TEST_EXPECT(ctx, document.value_type(document.first_child(document.last_child(transport))) == ELiveValueType::integer);
    TEST_EXPECT(ctx, document.value_type(member(document, document.root(), CStringView{ "o" })) == ELiveValueType::object);
    TEST_EXPECT(ctx, document.value_type(document.root()) == ELiveValueType::object && document.check_integrity());
}

static void attach(TTestContext& ctx, CLiveDocument& document, const CNodeKey parent, const CNodeKey child)
{
    TEST_EXPECT(ctx, child.is_valid());
    TEST_EXPECT(ctx, document.append_child(parent, child).succeeded());
}

static void expect_same_semantics(TTestContext& ctx, const CLiveDocument& source, const CLiveDocument& parsed, const bool strict)
{
    struct CPair
    {
        CNodeKey source;
        CNodeKey parsed;
    };
    std::vector<CPair> pending{ { source.root(), parsed.root() } };
    while (!pending.empty())
    {
        CPair pair = pending.back();
        pending.pop_back();
        if ((source.value_type(source.parent(pair.source)) == ELiveValueType::array) &&
            (source.value_type(pair.source) == ELiveValueType::object) && !source.is_object_entry(pair.source) &&
            (source.child_count(pair.source) == 1u))
        {
            pair.source = source.first_child(pair.source);
        }
        const ELiveValueType original_type = source.value_type(pair.source);
        const ELiveValueType type = (original_type == ELiveValueType::empty) ? ELiveValueType::null_value : original_type;
        TEST_EXPECT(ctx, parsed.value_type(pair.parsed) == type);
        TEST_EXPECT(ctx, source.name(pair.source) == parsed.name(pair.parsed));
        TEST_EXPECT(ctx, source.is_object_entry(pair.source) == parsed.is_object_entry(pair.parsed));
        switch (type)
        {
            case ELiveValueType::null_value:
            {
                break;
            }
            case ELiveValueType::boolean:
            {
                bool before = false;
                bool after = false;
                TEST_EXPECT(ctx, source.boolean_value(pair.source, before) && parsed.boolean_value(pair.parsed, after) && before == after);
                break;
            }
            case ELiveValueType::integer:
            {
                CIntegerMetadata before;
                CIntegerMetadata after;
                TEST_EXPECT(ctx, source.integer_metadata(pair.source, before) && parsed.integer_metadata(pair.parsed, after));
                std::int64_t signed_value = 0;
                std::uint64_t unsigned_value = 0u;
                if (before.domain == EIntegerDomain::signed_value)
                {
                    TEST_EXPECT(ctx, source.signed_integer_value(pair.source, signed_value));
                }
                else
                {
                    TEST_EXPECT(ctx, source.unsigned_integer_value(pair.source, unsigned_value));
                }
                if (strict)
                {
                    before.notation = EIntegerNotation::decimal;
                    before.prefix = EIntegerPrefix::standard;
                    if ((before.domain == EIntegerDomain::signed_value) && (signed_value >= 0))
                    {
                        before.domain = EIntegerDomain::unsigned_value;
                        unsigned_value = static_cast<std::uint64_t>(signed_value);
                    }
                }
                TEST_EXPECT(ctx, before.domain == after.domain && before.notation == after.notation && before.prefix == after.prefix);
                if (before.domain == EIntegerDomain::signed_value)
                {
                    std::int64_t value = 0;
                    TEST_EXPECT(ctx, parsed.signed_integer_value(pair.parsed, value) && value == signed_value);
                    TEST_EXPECT(ctx, after.width == live_signed_integer_smallest_width(signed_value));
                }
                else
                {
                    std::uint64_t value = 0u;
                    TEST_EXPECT(ctx, parsed.unsigned_integer_value(pair.parsed, value) && value == unsigned_value);
                    TEST_EXPECT(ctx, after.width == live_unsigned_integer_smallest_width(unsigned_value));
                }
                break;
            }
            case ELiveValueType::floating_point:
            {
                double before = 0.0;
                double after = 0.0;
                TEST_EXPECT(ctx, source.floating_point_value(pair.source, before) && parsed.floating_point_value(pair.parsed, after));
                TEST_EXPECT(ctx, live_floating_point_bits(before) == live_floating_point_bits(after));
                break;
            }
            case ELiveValueType::string:
            {
                TEST_EXPECT(ctx, source.string_value(pair.source) == parsed.string_value(pair.parsed));
                break;
            }
            case ELiveValueType::object:
            case ELiveValueType::array:
            {
                TEST_EXPECT(ctx, source.child_count(pair.source) == parsed.child_count(pair.parsed));
                CNodeKey before = source.first_child(pair.source);
                CNodeKey after = parsed.first_child(pair.parsed);
                while (before.is_valid() && after.is_valid())
                {
                    pending.push_back(CPair{ before, after });
                    before = source.next_sibling(before);
                    after = parsed.next_sibling(after);
                }
                TEST_EXPECT(ctx, !before.is_valid() && !after.is_valid());
                break;
            }
            default:
            {
                TEST_EXPECT(ctx, false);
                break;
            }
        }
    }
}

static void test_round_trips(TTestContext& ctx)
{
    CLiveDocument source;
    TEST_EXPECT(ctx, source.initialise());
    const CNodeKey array = source.create_array(CStringView{ "items" });
    attach(ctx, source, source.root(), array);
    attach(ctx, source, array, source.create_empty(CStringView{ "placeholder" }));
    attach(ctx, source, array, source.create_null(CStringView{ "null" }));
    attach(ctx, source, array, source.create_boolean(true, CStringView{ "bool" }));
    attach(ctx, source, array, source.create_signed_integer(7, CStringView{ "signed" }));
    attach(ctx, source, array, source.create_signed_integer(std::numeric_limits<std::int64_t>::min(), CStringView{ "minimum" }));
    attach(ctx, source, array, source.create_unsigned_integer(std::numeric_limits<std::uint64_t>::max(), CStringView{ "maximum" }));
    CIntegerMetadata hexadecimal;
    hexadecimal.domain = EIntegerDomain::signed_value;
    hexadecimal.notation = EIntegerNotation::hexadecimal;
    hexadecimal.prefix = EIntegerPrefix::alternate;
    hexadecimal.width = live_signed_integer_smallest_width(-128);
    attach(ctx, source, array, source.create_signed_integer(-128, hexadecimal, CStringView{ "hex" }));
    CIntegerMetadata binary;
    binary.domain = EIntegerDomain::unsigned_value;
    binary.notation = EIntegerNotation::binary;
    binary.width = live_unsigned_integer_smallest_width(256u);
    attach(ctx, source, array, source.create_unsigned_integer(256u, binary, CStringView{ "binary" }));
    attach(ctx, source, array, source.create_floating_point(-0.0, CStringView{ "float" }));
    const std::uint8_t unicode[]{ 'x', 0u, 0xc3u, 0xa9u, 0xf0u, 0x9du, 0x84u, 0x9eu, '\n' };
    attach(ctx, source, array, source.create_string(CStringView{ unicode, sizeof(unicode) }, CStringView{ unicode, sizeof(unicode) - 1u }));
    attach(ctx, source, array, source.create_string(CStringView{ "" }, CStringView{ "empty" }));
    attach(ctx, source, array, source.create_array(CStringView{ "array" }));
    const CNodeKey object = source.create_object(CStringView{ "object" });
    attach(ctx, source, array, object);
    attach(ctx, source, object, source.create_null(CStringView{ "$morphic" }));
    const CNodeKey redundant = source.create_object();
    attach(ctx, source, array, redundant);
    attach(ctx, source, redundant, source.create_boolean(false, CStringView{ "singleton" }));
    attach(ctx, source, array, source.create_object());
    const CNodeKey multi = source.create_object();
    attach(ctx, source, array, multi);
    attach(ctx, source, multi, source.create_null(CStringView{ "a" }));
    attach(ctx, source, multi, source.create_null(CStringView{ "b" }));

    for (unsigned count = 0u; count < 4u; ++count)
    {
        const std::string name = std::string(count + 1u, '$') + "morphic";
        const CNodeKey recovered = source.create_array(CStringView{ name.data(), name.size() });
        attach(ctx, source, array, recovered);
        for (unsigned i = 0u; i < count; ++i)
        {
            const CNodeKey competitor = source.create_object();
            attach(ctx, source, recovered, competitor);
            attach(ctx, source, competitor, source.create_unsigned_integer(i, CStringView{ "n" }));
        }
    }
    const CNodeKey nested_arrays = source.create_array();
    attach(ctx, source, array, nested_arrays);
    attach(ctx, source, nested_arrays, source.create_array());
    const CNodeKey competitor_array = source.create_array();
    attach(ctx, source, nested_arrays, competitor_array);
    attach(ctx, source, competitor_array, source.create_array(CStringView{ "nested" }));
    const CNodeKey lookalike = source.create_object(CStringView{ "$morphic" });
    attach(ctx, source, source.root(), lookalike);
    attach(ctx, source, lookalike, source.create_unsigned_integer(2u, CStringView{ "v" }));
    attach(ctx, source, lookalike, source.create_string(CStringView{ "array" }, CStringView{ "type" }));
    attach(ctx, source, lookalike, source.create_array(CStringView{ "values" }));

    const CNodeKey numbers = source.create_array(CStringView{ "numbers" });
    attach(ctx, source, source.root(), numbers);
    for (const double value : { std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::min(), std::numeric_limits<double>::max() })
    {
        attach(ctx, source, numbers, source.create_floating_point(value));
    }
    std::uint64_t bits = 0xa0761d6478bd642full;
    for (unsigned i = 0u; i < 256u; ++i)
    {
        bits ^= bits << 13u;
        bits ^= bits >> 7u;
        bits ^= bits << 17u;
        const double value = live_floating_point_from_bits(bits);
        if (live_floating_point_is_finite(value))
        {
            attach(ctx, source, numbers, source.create_floating_point(value));
        }
    }
    TEST_EXPECT(ctx, source.check_integrity());
    CBakedDocumentBlock baked;
    TEST_EXPECT(ctx, document_translation::bake(source, baked));
    for (unsigned variant = 0u; variant < 8u; ++variant)
    {
        CDocumentWriteOptions options;
        const bool strict = (variant & 1u) != 0u;
        options.mode = strict ? EDocumentWriteMode::strict_json : EDocumentWriteMode::morphic;
        options.escape_non_ascii = (variant & 2u) != 0u;
        options.pretty_print = (variant & 4u) != 0u;
        const auto written = document_writer::write(baked.document(), options);
        TEST_EXPECT(ctx, written.report.succeeded());
        const auto linted = text_linter::lint(CByteConstView{ written.output.data(), written.output.size() }, k_document_text_lint_line_endings);
        TEST_EXPECT(ctx, linted.report.success && linted.report.normalised_line_endings == linted.report.encountered_line_endings);
        CLiveDocument parsed;
        const auto report = parse_text(CStringView{ linted.output.data(), linted.report.logical_text_byte_size }, parsed, { document_policy::k_all_supported });
        TEST_EXPECT(ctx, report.accepted());
        if (!report.accepted())
        {
            continue;
        }
        TEST_EXPECT(ctx, parsed.check_integrity() && parsed.is_complete());
        TEST_EXPECT(ctx, (report.findings & document_finding_bit(EDocumentFinding::singleton_normalization)) != 0u);
        TEST_EXPECT(ctx, (report.findings & document_finding_bit(EDocumentFinding::name_collision_extension)) == 0u);
        TEST_EXPECT(ctx, (report.findings & document_findings::k_relaxed) == 0u);
        TEST_EXPECT(ctx, ((report.findings & document_findings::k_morphic) == 0u) == strict);
        expect_same_semantics(ctx, source, parsed, strict);
        CBakedDocumentBlock rebaked;
        TEST_EXPECT(ctx, document_translation::bake(parsed, rebaked));
        const auto rewritten = document_writer::write(rebaked.document(), options);
        TEST_EXPECT(ctx, rewritten.report.succeeded());
        TEST_EXPECT(ctx, rewritten.report.logical_text_byte_size == written.report.logical_text_byte_size);
        if (rewritten.report.logical_text_byte_size == written.report.logical_text_byte_size)
        {
            TEST_EXPECT(ctx, std::memcmp(rewritten.output.data(), written.output.data(), written.report.logical_text_byte_size) == 0);
        }
    }
}

struct CAllocatorState
{
    std::size_t attempts{ 0u };
    std::size_t fail_on{ 0u };
    bool failed{ false };
};

static void* MV_STD_ABI_CALL allocate(void* const state, const std::size_t alignment, const std::size_t size) noexcept
{
    auto& fixture = *static_cast<CAllocatorState*>(state);
    if (fixture.attempts++ == fixture.fail_on)
    {
        fixture.failed = true;
        return nullptr;
    }
    return tests::allocate_test_memory(nullptr, alignment, size);
}

static void test_policy_allocation(TTestContext& ctx)
{
    for (const std::uint32_t permissions : { document_policy::k_default, 1u << 31 })
    {
        CLiveDocument destination;
        TEST_EXPECT(ctx, parse("[7]", destination).accepted());
        const CNodeKey keep = destination.first_child(destination.root());
        const std::uint64_t allocation_size = destination.memory_attribution().allocation_size;
        bool completed = false;
        for (std::size_t fail_on = 0u; (fail_on < 256u) && !completed; ++fail_on)
        {
            CAllocatorState fixture{ 0u, fail_on, false };
            memory::CMemoryAllocator allocator{ &fixture, &allocate, &tests::deallocate_test_memory };
            memory::CMemoryContext context{ allocator };
            {
                tests::TMemoryContextScope scope{ &context };
                CTextLintReport linter_report;
                const auto report = parse_text(CStringView{ "/*comment*/{\"a\":\"\\u00e9\",\"a\":[1,2]}" },
                    destination, { permissions }, &linter_report);
                completed = report.processing_succeeded();
                TEST_EXPECT(ctx, linter_report.success == (report.failure.stage != EDocumentFailureStage::linter));
                TEST_EXPECT(ctx, !report.accepted());
                if (completed)
                {
                    TEST_EXPECT(ctx, !fixture.failed && report.failure.reason == EDocumentFailureReason::none);
                    TEST_EXPECT(ctx, report.policy.status == ((permissions == document_policy::k_default) ?
                        EDocumentPolicyStatus::rejected : EDocumentPolicyStatus::invalid_options));
                }
                else
                {
                    TEST_EXPECT(ctx, fixture.failed && report.policy.status == EDocumentPolicyStatus::unexamined);
                    TEST_EXPECT(ctx, report.failure.reason == EDocumentFailureReason::allocation_failed ||
                        report.failure.reason == EDocumentFailureReason::construction_failed);
                }
                TEST_EXPECT(ctx, destination.value_type(destination.root()) == ELiveValueType::array);
                TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);
                TEST_EXPECT(ctx, destination.memory_attribution().allocation_size == allocation_size && destination.check_integrity());
            }
            //  Rejected temporary documents and linter output must release all
            //  attributed storage, even after construction completed.
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }
        TEST_EXPECT(ctx, completed);
    }
}

static void test_root_allocation(TTestContext& ctx)
{
    const char* const sources[]{ "[]", "42", "\"\":1", "[\"a\\nb\",{\"\":\"x\n y\"},[1]]", "1,{\"x\":2},[3]" };
    for (const char* source : sources)
    {
        CLiveDocument destination;
        TEST_EXPECT(ctx, parse("[7]", destination).accepted());
        const CNodeKey keep = destination.first_child(destination.root());
        const std::uint64_t allocation_size = destination.memory_attribution().allocation_size;
        bool completed = false;
        for (std::size_t fail_on = 0u; (fail_on < 256u) && !completed; ++fail_on)
        {
            CAllocatorState fixture{ 0u, fail_on, false };
            memory::CMemoryAllocator allocator{ &fixture, &allocate, &tests::deallocate_test_memory };
            memory::CMemoryContext context{ allocator };
            {
                tests::TMemoryContextScope scope{ &context };
                const auto report = parse(source, destination);
                completed = report.accepted();
                if (completed)
                {
                    TEST_EXPECT(ctx, !fixture.failed && report.processing_succeeded() && destination.check_integrity());
                    destination.deallocate();
                }
                else
                {
                    TEST_EXPECT(ctx, fixture.failed && !report.processing_succeeded());
                    TEST_EXPECT(ctx, report.state == EDocumentProcessingState::failure);
                    TEST_EXPECT(ctx, (report.failure.stage == EDocumentFailureStage::linter) || (report.failure.stage == EDocumentFailureStage::structure) || (report.failure.stage == EDocumentFailureStage::parser));
                    TEST_EXPECT(ctx, (report.failure.reason == EDocumentFailureReason::construction_failed) ||
                        (report.failure.reason == EDocumentFailureReason::allocation_failed));
                    TEST_EXPECT(ctx, destination.value_type(destination.root()) == ELiveValueType::array);
                    TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);
                    TEST_EXPECT(ctx, destination.memory_attribution().allocation_size == allocation_size && destination.check_integrity());
                }
            }
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }
        TEST_CASE_EXPECT_TRUE(ctx, source, completed);
    }
}

static void test_depth_and_allocation(TTestContext& ctx)
{
    CLiveDocument destination;
    TEST_EXPECT(ctx, parse("keep:7", destination).accepted());
    const CNodeKey keep = destination.first_child(destination.root());
    const std::string text = "'':'literal\nbreak',u\\u0020name:unquoted\\u0020value,'n\\u0000':'\\u0000\\u00e9',a:[{s:'\\uD834\\uDD1E'},+0x80,{},[],{d:1,d:2}],"
        "r:{$morphic:{values:[{n:1},[{a:2}]," + array_body("false") + "],type:'recovered-\\u0061rray',v:1}},"
        "r:{x:1,x:2},r:" + array_body("") + ",$$morphic:0,'\\u0024$morphic':1,b:'last'";
    bool completed = false;
    bool failed_linting = false;
    bool failed_structure = false;
    bool failed_construction = false;
    for (std::size_t fail_on = 0u; (fail_on < 256u) && !completed; ++fail_on)
    {
        CAllocatorState fixture{ 0u, fail_on, false };
        memory::CMemoryAllocator allocator{ &fixture, &allocate, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            const auto report = parse(text, destination);
            completed = report.accepted();
            if (completed)
            {
                TEST_EXPECT(ctx, !fixture.failed);
                TEST_EXPECT(ctx, destination.check_integrity());
                destination.deallocate();
            }
            else
            {
                TEST_EXPECT(ctx, fixture.failed);
                failed_linting |= report.failure.stage == EDocumentFailureStage::linter;
                failed_structure |= report.failure.stage == EDocumentFailureStage::structure;
                failed_construction |= report.failure.stage == EDocumentFailureStage::parser;
                TEST_EXPECT(ctx, report.state == EDocumentProcessingState::failure);
                TEST_EXPECT(ctx, (report.failure.stage == EDocumentFailureStage::linter) || (report.failure.stage == EDocumentFailureStage::structure) || (report.failure.stage == EDocumentFailureStage::parser));
                TEST_EXPECT(ctx, (report.failure.reason == EDocumentFailureReason::construction_failed) ||
                        (report.failure.reason == EDocumentFailureReason::allocation_failed));
                TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, completed && failed_linting && failed_structure && failed_construction);
    constexpr std::size_t depth = 2048u;
    const std::string deep = "a:" + std::string(depth, '[') + "null" + std::string(depth, ']');
    const auto report = parse(deep, destination);
    TEST_EXPECT(ctx, report.accepted());
    TEST_EXPECT(ctx, destination.check_integrity());
    TEST_EXPECT(ctx, destination.value_count() == depth + 2u);
    const std::string expected = "{\"a\":" + std::string(depth, '[') + "null" + std::string(depth, ']') + "}";
    TEST_EXPECT(ctx, write(ctx, destination) == expected);

    constexpr std::size_t array_depth = 512u;
    std::string recovered = "null";
    for (std::size_t i = 0u; i < array_depth; ++i)
    {
        recovered = array_body(recovered);
    }
    const auto nested = parse("r:" + recovered, destination);
    TEST_EXPECT(ctx, nested.accepted());
    TEST_EXPECT(ctx, destination.value_count() == array_depth + 2u && destination.check_integrity());
    TEST_EXPECT(ctx, write(ctx, destination) == "{\"r\":" + recovered + "}");
}

static void test_composed_linter_diagnostics(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, parse("keep:7", document).accepted());
    const CNodeKey keep = document.first_child(document.root());
    CDocumentReport report = parse("{a:[1}", document);
    TEST_EXPECT(ctx, report.failure.element_start.available && report.failure.location.available);
    TEST_EXPECT(ctx, report.failure.element_start.code_point_column_1_based == 4u);
    TEST_EXPECT(ctx, report.failure.location.code_point_column_1_based == 6u);
    CTextLintReport linter_report;
    const std::string failures[]{ "\x81", "\xef\xbb\xbf\xff",
        "\xef\xbb\xbf\r\n\t\xc3\xa9\xcc\x81\xed\xa0\x80\xed\xb0\x80\xff",
        "\x93\r\n\xe9\t\x81" };
    for (const auto& source : failures)
    {
        const CByteConstView bytes{ reinterpret_cast<const std::uint8_t*>(source.data()), source.size() };
        report = document_parser::parse(bytes, document, {}, &linter_report);
        TEST_EXPECT(ctx, report.state == EDocumentProcessingState::failure);
        TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::linter && !linter_report.success);
        TEST_EXPECT(ctx, linter_report.first_failure.present && !report.failure.element_start.available);
        TEST_EXPECT(ctx, report.policy.status == EDocumentPolicyStatus::unexamined && !report.accepted());
        TEST_EXPECT(ctx, report.failure.location.available == linter_report.first_failure.location.available);
        TEST_EXPECT(ctx, report.failure.location.line_1_based == linter_report.first_failure.location.line_1_based);
        TEST_EXPECT(ctx, report.failure.location.code_point_column_1_based == linter_report.first_failure.location.code_point_column_1_based);
        TEST_EXPECT(ctx, document.first_child(document.root()) == keep);
        const auto without_details = document_parser::parse(bytes, document);
        TEST_EXPECT(ctx, without_details.state == report.state && without_details.findings == report.findings);
        TEST_EXPECT(ctx, without_details.failure.stage == report.failure.stage && without_details.failure.reason == report.failure.reason);
        TEST_EXPECT(ctx, without_details.failure.location.available == report.failure.location.available &&
            without_details.failure.location.line_1_based == report.failure.location.line_1_based &&
            without_details.failure.location.code_point_column_1_based == report.failure.location.code_point_column_1_based);
    }
    const std::uint8_t source[]{ '{', '"', 's', '"', ':', '"', 0xedu, 0xa0u, 0x80u,
        0xedu, 0xb0u, 0x80u, '\r', '\n', 'x', '"', '}' };
    tests::TAllocatorFixture fixture;
    fixture.reject_allocation = true;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_test_memory, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        report = document_parser::parse(CByteConstView{ source, sizeof(source) }, document, { document_policy::k_all_supported }, &linter_report);
        TEST_EXPECT(ctx, report.state == EDocumentProcessingState::failure);
        TEST_EXPECT(ctx, linter_report.first_failure.reason == ETextLintFailure::allocation_failed);
        TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::linter && report.failure.reason == EDocumentFailureReason::allocation_failed);
        TEST_EXPECT(ctx, linter_report.first_failure.before_output);
        TEST_EXPECT(ctx, !report.failure.element_start.available && report.failure.location.available);
        TEST_EXPECT(ctx, document.first_child(document.root()) == keep);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    report = document_parser::parse(CByteConstView{ source, sizeof(source) }, document, { document_policy::k_all_supported }, &linter_report);
    TEST_EXPECT(ctx, report.accepted() && linter_report.success && !linter_report.first_failure.present);
    TEST_EXPECT(ctx, linter_report.cesu8_pair_count == 1u);
    TEST_EXPECT(ctx, !report.failure.element_start.available && !report.failure.location.available);
    const std::uint8_t expected[]{ 0xf0u, 0x90u, 0x80u, 0x80u, '\n', 'x' };
    TEST_EXPECT(ctx, document.string_value(document.first_child(document.root())) == (CStringView{ expected, sizeof(expected) }));

    for (const std::uint32_t permissions : { document_policy::k_default, 1u << 31 })
    {
        report = parse_text(CStringView{ "/*comment*/{}" }, document, { permissions }, &linter_report);
        TEST_EXPECT(ctx, report.processing_succeeded() && !report.accepted());
        TEST_EXPECT(ctx, report.policy.status == ((permissions == document_policy::k_default) ?
            EDocumentPolicyStatus::rejected : EDocumentPolicyStatus::invalid_options));
        TEST_EXPECT(ctx, linter_report.success && !linter_report.first_failure.present);
        TEST_EXPECT(ctx, linter_report.logical_text_byte_size == 13u);
    }
    report = parse_text(CStringView{ "1e9999" }, document, {}, &linter_report);
    TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::parser && report.failure.reason == EDocumentFailureReason::numeric_out_of_range);
    TEST_EXPECT(ctx, linter_report.success && report.policy.status == EDocumentPolicyStatus::unexamined);
    report = parse_text(CStringView{ "{\"missing\":}" }, document, {}, &linter_report);
    TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::structure && report.failure.reason == EDocumentFailureReason::missing_value);
    TEST_EXPECT(ctx, linter_report.success && report.policy.status == EDocumentPolicyStatus::unexamined);

    const char alias_source[] = "{\"alias\":1}";
    TEST_EXPECT(ctx, parse("s:'{\"alias\":1}'", document).accepted());
    const CStringView alias = document.string_value(document.first_child(document.root()));
    TEST_EXPECT(ctx, document_parser::parse(CByteConstView{ alias.string(), alias.length() }, document).accepted());
    TEST_EXPECT(ctx, write(ctx, document) == alias_source);
}

static void test_empty_byte_input(TTestContext& ctx)
{
    static_assert(std::is_same_v<std::underlying_type_t<EDocumentProcessingState>, std::int8_t>);
    static_assert(static_cast<std::int8_t>(EDocumentProcessingState::failure) == -1);
    static_assert(static_cast<std::int8_t>(EDocumentProcessingState::unprocessed) == 0);
    static_assert(static_cast<std::int8_t>(EDocumentProcessingState::success) == 1);
    CDocumentReport initial;
    TEST_EXPECT(ctx, !initial.processing_succeeded() && !initial.accepted());
    initial.policy = document_policy::evaluate(0u);
    TEST_EXPECT(ctx, !initial.accepted());
    initial.state = EDocumentProcessingState::failure;
    TEST_EXPECT(ctx, !initial.processing_succeeded() && !initial.accepted());

    const std::uint8_t terminal = 0u;
    CByteBuffer buffer;
    TEST_EXPECT(ctx, buffer.reallocate(0u, 1u));
    const CByteConstView empty_views[]{ {}, { &terminal, 0u }, buffer.const_view() };
    for (const auto& bytes : empty_views)
    {
        CLiveDocument document;
        TEST_EXPECT(ctx, parse("keep:7", document).accepted());
        CTextLintReport linter_report;
        linter_report.first_failure.present = true;
        const auto report = document_parser::parse(bytes, document, {}, &linter_report);
        TEST_EXPECT(ctx, report.processing_succeeded() && report.accepted() && report.findings == 0u);
        TEST_EXPECT(ctx, linter_report.success && linter_report.input_is_empty && !linter_report.first_failure.present);
        TEST_EXPECT(ctx, linter_report.input_byte_size == 0u && linter_report.logical_text_byte_size == 0u);
        TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::none && report.failure.reason == EDocumentFailureReason::none);
        TEST_EXPECT(ctx, !report.failure.location.available && !report.failure.element_start.available);
        TEST_EXPECT(ctx, document.value_type(document.root()) == ELiveValueType::object && document.value_count() == 1u);
        TEST_EXPECT(ctx, document.check_integrity());
    }
}

}   //  namespace document_parser_tests

int run_document_parser_tests()
{
    tests::TTestContext ctx;
    document_parser_tests::test_empty_byte_input(ctx);
    document_parser_tests::test_findings_and_policy_contract(ctx);
    document_parser_tests::test_late_policy_acceptance(ctx);
    document_parser_tests::test_policy_processing_precedence(ctx);
    document_parser_tests::test_policy_source_acceptance(ctx);
    document_parser_tests::test_composed_findings(ctx);
    document_parser_tests::test_shared_failures(ctx);
    document_parser_tests::test_policy_source_provenance(ctx);
    document_parser_tests::test_construction_and_features(ctx);
    document_parser_tests::test_unquoted_strings(ctx);
    document_parser_tests::test_strings_and_ingestion(ctx);
    document_parser_tests::test_empty_names(ctx);
    document_parser_tests::test_newline_metadata(ctx);
    document_parser_tests::test_root_inference(ctx);
    document_parser_tests::test_integer_metadata(ctx);
    document_parser_tests::test_floats(ctx);
    document_parser_tests::test_failure_publication(ctx);
    document_parser_tests::test_singleton_contexts(ctx);
    document_parser_tests::test_collision_arrays(ctx);
    document_parser_tests::test_former_protocol_as_data(ctx);
    document_parser_tests::test_round_trips(ctx);
    document_parser_tests::test_depth_and_allocation(ctx);
    document_parser_tests::test_root_allocation(ctx);
    document_parser_tests::test_policy_allocation(ctx);
    document_parser_tests::test_composed_linter_diagnostics(ctx);
    std::cout << "DocumentParser: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
