# Testing framework consolidation discussion

Updated 20 October 2026. These discussion points concern reducing intrusive
testing, improving functional coverage and simplifying shared test code. They
are not an approved implementation plan or authority to begin implementation.

Bundle work and
[message and ownership consolidation](../project/completed_milestones.md#message-and-ownership-consolidation)
are complete. The testing-framework and debug-support reviews can now build on
those implemented contracts; their scope and implementation remain separate
decisions.

The aim is a smaller maintenance surface with tests that protect observable
contracts and remain useful when implementation details change. Fewer lines or
fewer tests are useful only where coverage and failure diagnosis remain clear.

## Functional testing and observable behaviour

Prefer tests of results, failure handling, ownership and lifecycle guarantees
over tests tied to private representation. Establish the behaviour each existing
test protects before deciding whether to replace, combine or remove it.

Functional coverage should include unsuccessful operations as well as successful
ones: rejection, allocation failure, cleanup and ownership preservation can be
part of the observable contract. A functional focus does not require every test
to exercise the complete engine; focused component tests through supported
interfaces can provide clearer diagnosis.

## Intrusive tests and access to internals

Review each test-only friendship or internal access hook individually. The
preferred direction is to remove intrusive tests and friend visibility where
public behaviour can provide adequate coverage.

For each candidate, discuss:

- Which contract or invariant the test currently protects.
- Whether a functional test can expose a violation through supported interfaces.
- Whether removing internal access would leave an important invariant untested.
- Whether the access exists solely to assert an implementation choice that no
  longer needs a dedicated test.

Some low-level components may warrant narrowly scoped invariant checks where
corruption is difficult to expose through a functional result. Any retained
exception should have a specific justification rather than preserve internal
access by default. Do not widen production interfaces solely to replace a friend
declaration with an equivalent test-only access path.

## Consolidation and shared patterns

Look for repeated mechanisms across suites, particularly:

- Setup and teardown of test environments and component contexts.
- Allocation-failure injection and verification of unsuccessful operations.
- Memory attribution and ownership checks.
- Transport exercises, including rejection and transfer behaviour.
- Cleanup and lifecycle verification.

These are review candidates, not findings from a completed audit. Share stable
mechanisms where doing so removes repeated rules and makes corrections apply
consistently across suites. Keep each test's purpose, action and expected
behaviour visible at the call site.

Consider whether related tests can be aggregated around a common contract, and
whether repeated setup can be reduced without coupling otherwise independent
tests. Avoid a general test abstraction whose configuration is harder to follow
than the repetition it replaces. Shared helpers should retain useful failure
context and should not merely reproduce the implementation under test.

## Discussion outcomes

The initial review should identify:

- Intrusive tests that can be replaced by functional coverage.
- Any narrowly justified internal invariant checks to retain.
- Repeated patterns worth sharing and the suites that would use them.
- Tests or scaffolding that no longer protect a distinct requirement.
- A proposed order of changes that preserves coverage and keeps failures easy
  to diagnose.

Reassess these choices after message consolidation, so the test structure follows
the selected ownership and messaging contracts. Substantive design choices and
implementation scope remain for discussion before work begins.
