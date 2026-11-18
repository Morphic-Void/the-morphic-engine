
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SchemaReference_test_cases.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    17 Nov 26
//
//  Portable reference-format, ownership and transaction regression entry points.

#pragma once

namespace tests { struct TTestContext; }
void run_schema_reference_cases(tests::TTestContext& context);
void run_schema_reference_validation_cases(tests::TTestContext& context);
int run_schema_reference_tests();
