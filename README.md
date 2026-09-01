# The Morphic Engine

A C++17 game engine developed by Morphic Void. It is being built from first principles with an emphasis on clear structure, modular systems, and long-term maintainability.

This repository currently contains the initial project structure and test infrastructure. Additional systems and documentation will be added as the engine evolves.

## Tests

`MorphicTests` is the standalone Core test executable. It is built by default
with `MorphicEngine.sln`; running `MorphicEngine.exe` does not run it. Invoke
`MorphicTests.exe -t1` for the ordinary suites, `-t2` for the moderate harness
work (the default), or `-t3` for the full expensive modes. Test data and log
paths are resolved from the repository, so the executable can be launched from
Visual Studio, the repository root, or its build-output directory.

Every test log name contains the process ID. An optional validated tag can be
supplied for human or automation reconciliation, for example
`MorphicTests.exe -t1 --log-tag=parallel-a`. The runner prints the resulting
absolute log-path pattern at startup. Concurrent invocations therefore remain
isolated even when the tag is omitted or accidentally reused.

Test output defaults to `tests/data/output`. Pass
`--output-directory=<path>` to use another location; logs are written beneath
that directory's `logs` child.

### Sandboxed Windows builds

Windows sandboxing can prevent MSBuild's SDK locator and native file tracker
from inspecting per-user directories. It can also expose both `PATH` and `Path`,
which .NET Framework build tasks reject when launching the compiler. Use the
repository helper to select an installed Windows SDK explicitly, disable file
tracking, normalise the child-process environment, and keep test logs beneath
the writable build tree:

```powershell
.\tools\invoke_sandbox_build.ps1 -RunTests -TestMode 1
```

The helper defaults to `Debug|x64`, discovers the newest installed Windows SDK,
and writes test logs under `build/sandbox-test-output`. Configuration, platform,
SDK version, MSBuild path, test mode, and log tag can all be overridden through
the script parameters. Because sandbox-safe file tracking is disabled, a build
invocation recompiles sources rather than relying on MSBuild's tracked inputs.

The helper first runs `tools/check_line_endings.ps1`. This verifies the effective
Git attributes, rejects mixed or incorrectly checked-out line endings, and
requires a final newline in every non-empty tracked text file. It can also be
run directly from PowerShell when checking a working tree without building.

The Executive-driven TGA load/decode/encode/save path remains a Host integration
exercise. The separate direct TGA round-trip helper is compiled into
`MorphicTests` as a manual test but is intentionally not registered or invoked.

This repository is developed using a mixed human/AI workflow. The project's
direction, specifications, architectural decisions, and final acceptance are
owned by Ritchie Brannan / Morphic Void Limited. AI tools are used in varying
degrees for drafting, refactoring, review, testing, and documentation. File
attribution is intended to reflect where the substantive ideas and content came
from, not merely which party produced the final wording or keystrokes.

This project treats human and AI contribution as complementary rather than
zero-sum. They are not the same, and their strengths, limits, and failure
modes are not the same, but there are meaningful overlaps in how both operate
within constraint. Human engineering is creative work, but it is not creation
from nothing: it is shaped by language, prior art, tools, hardware, shared
practice, and the accumulated influence of other people's ideas. AI operates
under different constraints and by different mechanisms, but it is likewise not
a source of pure independent novelty. In both cases, much of the value lies in
selection, combination, rejection, refinement, and the imposition of useful
constraints.

## License

Licensed under the MIT License. See the LICENSE file for details.

Copyright (c) 2010-2026 Ritchie Brannan.

Attribution is appreciated where practical.

## Why write another engine?

### The usual advice is mostly right

The usual advice is simple: do not write your own game engine.

I have often given that advice to other people, and I still think it was
usually the right advice. In the situations people brought to me, writing an
engine was almost never the best path to the game they wanted to ship.

That does not mean there are no valid reasons to write one. It means the
valid reasons are narrower, more specific, and usually more expensive than
people expect.

Writing a game engine is not just writing a renderer, a physics system, or
a few containers. It quickly becomes a pile of interacting disciplines:
runtime architecture, memory management, asset loading, editor tooling,
build systems, input, rendering, physics, audio, platform integration,
debugging, profiling, packaging, documentation, testing, and long-term
maintenance.

Most people considering writing an engine are not just underestimating the
amount of work. They are often underestimating the number of different
skills involved, and in many cases they do not yet know enough to know which
skills will be needed. That is not a criticism; it is just the shape of the
problem. A mature engine hides a vast amount of accumulated knowledge.

For most developers, especially anyone trying to ship a first game, writing
an engine is the wrong trade. It turns one hard project into several hard
projects, and it will usually steal time from the thing that matters most:
finishing the game.

### But "do not write an engine" is not an absolute rule

The advice is good because it applies to most cases, not because it applies
to every case.

There are situations where writing a custom engine, runtime, framework, or
toolchain can make sense: unusual technical requirements, research, platform
constraints, education, special-purpose tools, tiny bespoke runtimes,
preservation work, engine-as-product work, or teams whose core competency is
engine and systems engineering.

There is also some historical distortion in the modern discussion. I
started in the games industry in the 1980s, before off-the-shelf engines and
large publisher-wide engines were the normal way to build games. At that
time, making a game often meant building the runtime framework as part of
making the game. The "engine" might be a loose collection of game code, file
formats, converters, standalone tools, and whatever available asset tools
made sense, such as Deluxe Paint.

Even Unreal Engine started as the technology for a specific game. The clue
is in the name. It later became a general-purpose commercial engine, but it
did not begin as a requirement to solve every possible game development
problem for every possible team.

### A custom engine is not a public engine competitor

There is a common false assumption in some criticism of custom engines: that
writing an engine means trying to compete with Unreal, Unity, Godot, or
other large public engines on features, scale, scope, workflow, ecosystem,
or user support.

That is not what this project is.

A custom engine does not have to satisfy hundreds or thousands of unknown
users. It does not need to support every common genre, every asset workflow,
every rendering technique, every platform, every scripting preference, or
every production style. It does not need to provide the same editor
experience, marketplace, documentation surface, plugin ecosystem, or
compatibility guarantees as a public general-purpose engine.

Large engines are impressive partly because they solve broad problems for
many teams. That breadth is also why they can become awkward when a project
needs to cut across their assumptions.

The useful comparison is not between this codebase and Unreal or Unity as
products. The useful comparison is between this codebase and the specific
technical, creative, and maintenance requirements of this project.

Large engines solve a wider problem. This engine is intended to solve a
narrower one.

### I am not approaching this from the outside

I have worked with Unreal Engine for over a decade, and I have also worked
with many non-public engines used by major publishers, most prominently at
EA and Ubisoft.

That does not make writing an engine easy, but it does mean I have direct
experience of what engines actually contain: not just the visible systems,
but the accumulated tools, constraints, failure modes, maintenance costs,
production compromises, and long-tail edge cases.

This project deliberately breaks the usual rule.

It does so for specific reasons, not because the rule is wrong.

### Why this project is an exception

First, this is partly a demonstration of what I can build. I have spent my
career working on low-level systems, rendering, optimisation, tooling, and
engine infrastructure. This codebase is a way to make some of that work
visible. It is also, unavoidably, a marketing exercise for both myself and
Morphic Void Limited: not in the sense of polished sales material, but in
the sense of showing the engineering judgement, constraints, and trade-offs
behind the work.

Second, this is what I do anyway. My sense of self is strongly tied to being
an engineer. Building and maintaining systems like this is part of how I
keep my skills sharp, test my assumptions, and continue to develop. Even if
this were not attached to a game, much of the infrastructure work would
still be work I wanted to do.

Third, the game I want to build is a poor fit for the publicly available
engines and their surrounding assumptions.

That does not mean the game needs unusually complex rendering or physics.
From my point of view, it does not. Others may disagree. The issue is not
raw complexity; it is shape. The rendering and physics requirements sit in a
place where a large general-purpose engine would likely be fighting me in
the areas where I most need direct control.

### Why this game?

There is an obvious question: if the game is awkward to build in existing
engines, why not build a different game?

The answer is that I have been playing with this game idea for over thirty
years. I want it to see the light. If someone else had made a similar enough
game in the intervening years, this specific project might not exist in this
form, but I would almost certainly still be building something in the same
broad territory. The itch is not just one design document; it is a long-term
interest in a particular kind of game and the technology needed to support it.

The games that came closest, at least in parts of the design space that
matter to me, were *Prey* and *Portal*. They did not remove that itch.

So this repository is not only a game. It is also the engine, the general
runtime infrastructure, the pipeline infrastructure, the tooling direction,
and the game built on top of that stack. The technology is not incidental
overhead; it is part of the purpose of the project.

### What I am not trying to build

This does not mean I intend to build everything from scratch.

Where existing asset pipelines, file formats, tools, libraries, or engine
components can be scavenged sensibly, I will use them. I am not trying to
prove purity, and I am not a masochist.

The point is to own the parts where ownership matters: the runtime model,
the core infrastructure, the rendering and physics decisions that shape the
game, and the long-term maintainability of the codebase.

This is not an argument against existing engines. It is an argument for
knowing what problem you are actually solving.

### The actual trade

This is not a recommendation that other people should do the same.

If your goal is simply to make and ship a game, use an existing engine
unless you have a clear, concrete, and well-understood reason not to. That
does not mean a custom engine must compete with the large public engines.
It does mean you must understand which parts you are choosing to own, which
parts you are choosing not to build, and what those choices will cost.

The likelihood that you already have all the required skills is low.
The likelihood that you know in advance all the skills you will need
is lower still.

In this case, the cost is accepted deliberately. The engine exists because
the control, the constraints, the game idea, the engineering practice, and
the long-term infrastructure are all part of the same project.

Most games need a finished game more than they need a new engine.

This one needs both.
