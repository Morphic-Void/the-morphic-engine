
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    ExecutionFixture_test_cases.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    24 Nov 26
//
//  Fixture regression groups shared by ordinary and focused execution suites.

#pragma once

#include "tests/support/test_context.hpp"

void run_lifecycle_fixture_tests(tests::TTestContext& ctx);
void run_lifecycle_executive_fixture_tests(tests::TTestContext& ctx);
void run_lifecycle_service_fixture_tests(tests::TTestContext& ctx);
void run_pump_fixture_tests(tests::TTestContext& ctx);
void run_input_fixture_tests(tests::TTestContext& ctx);
