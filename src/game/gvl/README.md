# gvl — absorbed into openliero

These files are a copy of [gliptic/gvl](https://github.com/gliptic), originally
authored by Erik Lindroos and Martin Erik Werner (BSD-2-Clause). They were
previously vendored under `src/gvl/` as a separate library target; that target
has been removed and the files now live in the main `game` target so the
project no longer depends on an external `gvl` library.

The expectation is that the remaining contents here (mostly `io2/`,
`serialization/`, `resman/`, plus a few support helpers) will be incrementally
simplified or replaced with C++23 stdlib equivalents, but doing so requires
rewriting the binary archive / TOML adapter call sites in `replay.cpp`,
`settings.cpp`, `worm.cpp`, etc. — see `docs/gvl-removal-plan.md`.

## License

BSD-2-Clause. Copyright (c) 2010 Erik Lindroos <gliptic@gmail.com>,
copyright (c) 2012 Martin Erik Werner <martinerikwerner@gmail.com>.
