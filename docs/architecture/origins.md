Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited  
License: MIT (see LICENSE file in repository root)  

File:   origins.md  
Author: Ritchie Brannan  
Date:   2 May 26  

# Origins

## Overview

The Morphic Engine did not begin from a blank conceptual state.

It is the active reconstruction of ideas, constraints, and lessons that were
first explored in an earlier proof-of-concept project. That project was a
separate legacy codebase. It is not the current engine, not a code foundation
for this repository, and not a representation of the engineering standards used
by the active project.

Its value was exploratory. It helped identify the shape of the game, the
engine, the editor, and the supporting tooling that would be needed for a more
deliberate implementation.

The Morphic Engine is the result of carrying those lessons forward while
rebuilding the systems under stricter rules for ownership, allocation, module
boundaries, validation, documentation, and long-term maintainability.

## The proof-of-concept project

The earlier proof-of-concept project was an old working prototype.

Its newest parts were around a decade old when The Morphic Engine began to be
rebuilt, and some of its oldest parts were older still. Much of the work dated
from an earlier development period and then sat dormant while professional
consulting work took priority.

The proof of concept should be understood as historical design evidence. It
preserved a working exploration of the game and its supporting technology, but
it was not intended to be carried forward as production code.

The project helped answer several important questions:

- whether the central spatial ideas were practical enough to pursue
- whether the game required a custom authoring model
- whether a conventional scene model would be a poor semantic fit
- which engine systems needed to be owned directly
- which systems could be simplified, deferred, or externalised
- where runtime, editor, and data-conditioning responsibilities should separate

The result was not a final engine design. It was evidence about the shape of
the problem.

## What the proof of concept demonstrated

The proof of concept explored the game's non-standard spatial model.

The model involved ideas such as:

- overlapping non-Euclidean spaces
- local frames of reference
- unusual gravity, scale, and handedness behaviour
- navigation and gameplay rules derived from those spaces
- world-authoring requirements that do not map cleanly to a conventional
  scene graph

These were not just rendering tricks. They affected how the world needed to be
represented, authored, validated, transformed, and reasoned about.

That distinction is important.

A conventional engine can often be extended with custom components, scripts, or
editor tools. For this project, the proof of concept showed that the unusual
parts were not merely behaviours attached to otherwise ordinary objects. They
were part of the semantic structure of the world itself.

That pushed the active project toward a purpose-built engine and editor rather
than a conventional engine with a large layer of project-specific workarounds.

## What influenced The Morphic Engine

The proof of concept influenced The Morphic Engine at the level of system
shape, not implementation reuse.

Several broad directions were carried forward:

- a custom world-authoring model
- a clear separation between runtime, tooling, and data preparation
- explicit ownership of the canonical world model
- a module-oriented architecture
- rendering abstraction rather than direct dependence on one graphics API
- rendering inspection and validation support
- automation for repetitive or error-prone conditioning work
- controlled container and ownership patterns
- narrow systems designed around the game rather than broad engine generality

The active implementation is intentionally more disciplined than the prototype.

The Morphic Engine rebuilds the useful ideas under stricter constraints around:

- ownership
- allocation
- module boundaries
- container behaviour
- rendering abstraction
- validation
- automation
- documentation
- long-term maintainability

The proof of concept helped identify the direction. The Morphic Engine defines
the current implementation.

## What was not carried forward

The Morphic Engine is not a direct continuation of the proof-of-concept
codebase.

The old code was exploratory and provisional. Some of it reflected earlier
tooling, earlier constraints, and earlier engineering preferences. Some systems
were useful because they proved that an idea could work, not because their
implementation should be preserved.

The active project does not attempt to preserve:

- the old directory structure
- the old build orchestration
- the old API shapes
- the old implementation style
- ad hoc prototype mechanisms
- historical assumptions that no longer fit the project

The reconstruction is selective.

Ideas were carried forward where they remained useful. Mechanisms were replaced
where they were too narrow, too implicit, too fragile, or too dependent on the
old codebase's accidental shape.

## Why a custom engine and editor?

The usual advice against writing a custom game engine is mostly correct.

Most games should not start by trying to recreate Unreal, Unity, Godot, or any
other broad commercial engine. Existing engines are powerful, mature, and
supported by much larger teams and ecosystems.

The Morphic Engine is not trying to compete with those engines.

It is being built because this game has specific spatial and authoring
requirements that do not align cleanly with conventional engine and editor
assumptions.

The editor needs to author the game's world model directly. It cannot treat the
world as a conventional scene graph with a collection of custom behaviours
attached to ordinary objects. The spatial rules are central enough that they
need to belong to the editor and runtime model rather than live as a workaround
inside another engine's assumptions.

The engine is therefore purpose-built rather than market-engine-shaped.

It will contain systems that resemble those found in larger engines, such as an
object model, rendering abstraction, job system, tools, and editor support. Those
systems are bounded by the needs of this project. They are not intended to match
the breadth, feature set, or ecosystem of a general-purpose engine.

## Why not start from an existing engine?

Starting from an existing engine can be the right choice when the desired result
is still mostly shaped like that engine.

That was not the conclusion reached for this project.

A general-purpose engine brings a large amount of useful functionality, but it
also brings its own assumptions about:

- object lifetime
- ownership
- scene structure
- editor semantics
- asset import
- scripting
- reflection
- threading
- allocation
- build structure
- rendering architecture
- platform abstraction

If those assumptions are mostly aligned with the project, they are a
major advantage.

If the desired shape is structurally different, the work becomes semantic
surgery: understanding, removing, replacing, or working around inherited
assumptions while keeping the whole system coherent.

For this project, cutting down or working around a broad existing engine was
judged to be more expensive and less coherent than building a narrower
purpose-built substrate.

That judgement is specific to this project. It is not a general claim that
custom engines are usually the right answer.

## External tools and data conditioning

The project is not based on avoiding external tools.

External tools may be used where they provide useful leverage, especially for
offline conditioning, validation, conversion, inspection, or precomputation.

For example, some world data may be exported to tools such as Blender, Unreal,
or other external systems for tasks such as inspection, pre-lighting, conversion,
or data preparation, then re-imported into the project's own data model.

Those tools are conditioning dependencies, not semantic authorities.

The canonical world model belongs to the project. External tools may influence
interchange formats, cached data, validation passes, or import/export pipeline
details, but they must not define the editor's semantic centre or the runtime
world model.

In short:

- external tools may condition data
- external tools may constrain pipeline contracts
- external tools may help validate or inspect intermediate results
- external tools must not become the canonical source of truth for the world

## Design constraints

The Morphic Engine is shaped by strong resource and maintenance constraints.

Those constraints are not treated as universal proof that every engine should be
written this way. They are viability conditions for this specific project.

The active codebase is designed around constraints such as:

- limited development resources
- controlled allocation
- no exceptions in production code
- constrained STL use without exceptions, uncontrolled allocation or material vendor/platform dependence
- careful compartmentalisation of third-party code
- strict ownership boundaries
- deliberate module boundaries
- automation where manual processes would not scale
- narrow systems that serve the game rather than generality for its own sake

These constraints improve the design by aligning it with the project's real
viability conditions. They make many bad choices impossible, but they do not
make the remaining choices uniquely or universally optimal.

The goal is not to build the largest possible engine.

The goal is to build an engine that can be understood, maintained, demonstrated,
and used for this specific game.

## Scope

The proof-of-concept project established useful evidence about the shape of the
game and its supporting technology.

The Morphic Engine is the active project that carries those lessons forward.

It is a reconstruction, not a continuation. It is narrower than a
general-purpose public engine, but more deliberate than the prototype that came
before it. Its purpose is to support a specific game, its spatial model, and the
tooling needed to build that game coherently.
