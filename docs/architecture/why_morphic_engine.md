Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

File:   why_morphic_engine.md
Author: Ritchie Brannan

# Why Morphic Engine

## The usual advice is mostly right

The usual advice is simple: do not write your own game engine.

I have often given that advice to other people, and I still think it is usually
the right advice. If the aim is to make and ship a game, an existing engine is
normally the better trade. Building engine technology turns one difficult
project into several: runtime architecture, memory management, asset loading,
tools, rendering, physics, audio, platform integration, debugging, profiling,
packaging, documentation, testing, and long-term maintenance.

Mature engines hide a vast amount of accumulated knowledge. Most developers
considering their own engine underestimate not only the work, but also the
range of disciplines involved. That is not a criticism; it is the shape of the
problem.

The advice is good because it applies to most cases, not because it applies to
every case. There are narrower reasons to own technology: unusual technical
requirements, research, platform constraints, education, special-purpose
tools, small bespoke runtimes, preservation work, or teams whose core
competency is systems engineering.

There is also some historical distortion in the modern discussion. I started
in the games industry in the 1980s, before off-the-shelf engines and large
publisher-wide engines were normal. Making a game often meant building its
runtime framework as part of the game: a loose collection of game code, file
formats, converters, standalone tools, and whatever asset tools made sense.

Even Unreal Engine began as technology for a specific game. It later became a
broad commercial engine, but it did not begin as an attempt to solve every
possible game-development problem for every possible team.

## A foundation, not a public-engine competitor

Morphic Engine does not seek to compete with Unreal, Unity, Godot, or other
public game engines on scope, features, workflows, ecosystem, or support.

Those products are toolkits for making games. They provide a large integrated
set of decisions: a world and object model, renderer, animation, physics,
audio, editor, asset pipeline, build tooling, and production workflows.
Morphic Engine is the foundation from which a project can construct the parts
of such a toolkit that it actually needs.

It is closer in role to the layer that gets an application ready to grow its
own technology than to a finished engine product. A graphics bootstrap
framework removes the boilerplate of creating a graphics device and frame loop;
Morphic Engine applies the same idea more broadly across low-level systems
infrastructure. It establishes useful structures and boundaries, then leaves
the project to supply the specific game technology.

That is why the engine is *morphic*. It is designed to be shaped around a
project's requirements. By itself it deliberately does little. It is the cup,
not the coffee: a vessel for a particular runtime rather than the game content
or a universal set of ready-made game systems.

This does not promise that it can fit every game, every platform, or every
production model. Its value is in being adaptable where a small, deliberate
technology base is a better fit than adopting the assumptions of a broad one.

## Experience and engineering practice

I have worked with Unreal Engine for more than a decade and with many
non-public engines used by major publishers, most prominently at EA and
Ubisoft. That does not make writing an engine easy, but it does mean I have
direct experience of what engines contain beyond their visible systems: the
constraints, failure modes, maintenance costs, production compromises, and
long-tail edge cases.

## Why this project is an exception

Morphic Engine is partly a demonstration of what I can build. I have spent my
career working on low-level systems, rendering, optimisation, tooling, and
engine infrastructure. This codebase makes some of that work visible and shows
the engineering judgement, constraints, and trade-offs behind it. It is also a
marketing exercise for both myself and Morphic Void Limited, not as polished
sales material, but as evidence of that engineering work.

It is also engineering practice in its own right. Building and maintaining
systems like this is part of how I keep my skills sharp, test assumptions, and
continue to develop. Much of the foundational work would be worth doing even
without one specific game attached to it.

The project is not based on proving purity or rebuilding everything from
scratch. Existing file formats, libraries, asset pipelines, and tools should
be used where they provide sensible leverage. The parts worth owning are the
runtime model, core infrastructure, and the decisions that shape a project's
technology and long-term maintainability.

## Locality

The immediate reason to develop Morphic Engine is *Locality*, a game idea I
have been exploring for more than thirty years. Its spatial and authoring
requirements do not align cleanly with the assumptions of broad,
general-purpose engines.

If a sufficiently similar game had appeared in the intervening years, this
project might not exist in this form. *Prey* and *Portal* came closest in some
of the areas that matter, but did not remove the interest in this particular
design space.

The issue is not simply raw rendering or physics complexity. It is the shape
of the problem. The game needs its world model, spatial rules, authoring, and
runtime behaviour to agree at a structural level. Retrofitting those concerns
into an existing engine would mean removing, replacing, or working around
assumptions while preserving the rest of the engine's coherence.

For this project, building a narrower foundation is judged to be more coherent
than performing that semantic surgery. That judgement is specific to Locality;
it is not a claim that custom technology is generally the right choice.

The historical context and the explored spatial model are recorded in
[Origins](origins.md).

## Work in progress

Morphic Engine is pre-1.0 and remains under active development. It will become
more capable than it is now, but it will not become a complete, ready-to-use
general-purpose game engine or a complete game in isolation.

Rendering, audio, and editor-support infrastructure are still in development.
Editor support means the services, data contracts, runtime boundaries, and
integration points from which a project-specific editor can be built. It does
not mean that Morphic Engine will ship as a general-purpose editor.

The goal is not the largest possible engine. It is a foundation that can be
understood, maintained, demonstrated, and shaped into the technology required
by the projects that use it.

## The actual trade

This is not a recommendation that other people should make the same choice. If
the goal is simply to make and ship a game, use an existing engine unless there
is a clear, concrete, and well-understood reason not to.

Owning technology means understanding which parts are being built, which are
being left out, and what those choices cost. The likelihood that a developer
already has every skill required is low; the likelihood that they can identify
every skill in advance is lower still.

For this project, the cost is accepted deliberately. The control, constraints,
game idea, engineering practice, and long-term infrastructure are part of the
same project. Most games need a finished game more than they need new engine
technology. *Locality* needs both a game and the foundation from which its
specific technology can be built.
