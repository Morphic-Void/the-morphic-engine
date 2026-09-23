# Morphic Engine

Morphic Engine is a pre-1.0 C++17 foundation for building bespoke game
technology.

A conventional game engine is a toolkit for making games. Morphic Engine
provides the basic infrastructure from which that toolkit can be made. It is
the cup rather than the coffee: deliberately useful as a structure that a
specific project can fill, rather than a finished game or a ready-made,
general-purpose game engine.

The name describes that role. Morphic Engine is intended to be shaped around a
project's particular requirements. It provides the scaffolding--memory,
ownership, containers, data, transport, modules, assets, lifecycle management,
and diagnostics--without prescribing the game systems built on top of it.

## What it is and is not

Morphic Engine is not a competitor to Unreal Engine, Godot, Unity, or similar
products. Those engines arrive with many game-making systems already selected
and integrated. Morphic Engine does not attempt to provide a universal world
model, ECS, renderer, animation system, physics system, navigation system,
editor, or production toolchain.

Instead, it is intended to support the construction of project-specific
versions of those systems where they are needed. It is not suitable for every
game or platform, and it will never be a complete game engine or a complete
game by itself.

Morphic Engine is still in active development and has not reached version 1.0.
Its current implementation includes Core memory, container, transport, and
document systems; image utilities; a development filesystem image; and
Host-managed asynchronous asset and DLL services. Rendering, audio, and
editor-support infrastructure remain in development. Editor support means the
services, data contracts, runtime boundaries, and integration points from
which a project-specific editor can be built; it does not mean a finished,
general-purpose editor.

## Why it exists

Morphic Engine is being developed in support of *Locality*, whose spatial and
authoring requirements do not fit cleanly within the assumptions of a broad,
general-purpose engine. That game is an important consumer of the foundation,
not its definition. The same foundation can be shaped into other bespoke game
technology where owning the underlying structure is the right trade.

For the longer rationale, including why most games should use an existing
engine, see [Why Morphic Engine](docs/architecture/why_morphic_engine.md).

## Documentation

- [Documentation index](docs/README.md): implemented systems, design direction,
  rationale, and project record.
- [Building and testing](docs/project/building_and_testing.md): ordinary and
  sandboxed Windows builds, test modes, output paths, logs, and lifecycle
  harnesses.
- [Current scope](docs/backlog/current_scope_backlog.md) and
  [completed milestones](docs/project/completed_milestones.md): implementation
  status and project progress.
- [Attribution policy](docs/project/attribution_policy.md): project ownership
  and representation of AI assistance.

## License

Licensed under the MIT License. See [LICENSE](LICENSE) for details.

Copyright (c) 2010-2026 Ritchie Brannan.

Attribution is appreciated where practical.
