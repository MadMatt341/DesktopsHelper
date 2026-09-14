# Contributor context map

Desktops Helper has one UI: native buttons inside Explorer's primary taskbar. The C helper owns desktop operations, keyboard input, and a hidden message/tray host. There is no floating overlay or placement mode.

Read only what the task needs:

| Task | Start here | Go deeper when needed |
|---|---|---|
| Understand the product | [README.md](README.md) | [Architecture](docs/architecture.md) |
| Build, run, update binaries | [Development](docs/development.md) | [Adapter provenance](vendor/README.md) for adapter rebuilds |
| Change behavior or diagnose a bug | [Architecture](docs/architecture.md) | Relevant source files; [native internals](src/taskbar-prototype/README.md) for Explorer UI |
| Choose or run checks | [Testing](docs/testing.md) | [Evidence index](benchmarks/README.md) only for historical comparisons |

Keep this file a routing guide, not a duplicate manual. Keep README at product level, cross-component design in docs/, and private ABI/protocol details beside the component. Update the document that owns a changed fact. Do not preload benchmark JSON, archived investigations, or all source files.

Preserve the boundaries: desktop COM work stays off the input and Explorer UI threads; event-driven idle behavior and bounded recovery remain. Treat window/message identities as shared interfaces even when their names are historical. A loaded Explorer DLL outlives the helper: follow the development guide before replacing or claiming to test a changed component.

Run checks appropriate to the change. Keep new local reports under ignored `benchmarks/local-*.json`; retained evidence belongs to its recorded hashes. Do not replace historical results with a fresh run or claim old benchmarks qualify new code.
