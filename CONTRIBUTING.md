# Contributing to Subixa

Subixa is pre-release and has had one author so far. Bug reports, patches and
questions are all welcome; none of what follows is meant to make that harder
than it needs to be.

## Before you change anything

**Read [`docs/traps.md`](docs/traps.md).** It is 25 entries, each one something
that already cost somebody a debugging cycle, and several of them look like
arbitrary sledgehammers until you read why they exist. If you are touching the
render path, the subtitle extractor or anything in `qml/ui/`, that file will save
you more time than it takes.

Two warnings about it. Every entry carries a *Scope:* line as of 2026-07-29,
because several used to read as universal that are not — traps 20 and 22 are
WSLg only, trap 1 was found under WSLg but its rule binds everywhere, 9 and 10
apply to software rasterizers, 24 and 25 came off an ordinary Ubuntu desktop.
And one entry is openly unresolved (trap 10). Check an entry's scope before
treating it as a constraint on yours.

[`CLAUDE.md`](CLAUDE.md) is the orientation file: architecture, conventions and
the trap index in one page.

## Building and testing

[`docs/building.md`](docs/building.md) for Linux, [`docs/windows.md`](docs/windows.md)
for Windows. Neither is a one-liner, because no current distribution ships the
required FFmpeg.

```bash
./testdata/make-fixtures.sh          # a fresh clone has no media fixtures
cd build && ctest --output-on-failure
```

Ten suites must pass. Three of them **fail** rather than skip when fixtures are
absent — that is deliberate, because a skip exits 0 and the suite used to report
green having asserted almost nothing.

The tree is warning-free under `-Wall -Wextra` on every target, including the
test targets, and should stay that way.

### The conformance corpus

`testdata/conformance/` is committed as bytes and `golden.tsv` pins the
extractor's output over it. If your change alters that output, the suite fails
with the exact cue that moved.

**Read the diff before regenerating.** The golden is not updated as a side effect
of a run, on purpose:

```bash
SUBIXA_REGOLD=1 ./tools/regold.sh --reason "why the new output is correct"
```

The reason is recorded in the file. A golden that rewrites itself when it fails
does not test anything.

## Conventions

- **C++23**, no compiler extensions. 4-space indent, Qt naming — `m_` members,
  camelCase methods.
- **Keep mpv types out of `MpvEngine.h`.** It forward-declares `mpv_handle` and
  takes `void*` in `handleMpvEvent` so mpv headers stay in the `.cpp`.
- **QML talks to mpv only through `Q_INVOKABLE`s and properties on `MpvEngine`.**
  Do not reach into libmpv from QML.
- **All text goes through `AppText`**, never a bare `Text`, and every use sets
  `textFormat` explicitly. `Text.AutoText` promotes anything that looks like
  markup to full rich text, which supports `<img src>` and will fetch it — and
  plenty of strings here come straight out of a container's metadata.
- **Sizes, colours and durations come from `Theme`.** A literal `12` or a hex
  colour in a component is the thing the token system exists to prevent. Two
  exceptions are deliberate and labelled where they sit: the subtitle *rendering*
  colours in `Main.qml` and `ColorSwatch`'s presets describe how subtitles are
  drawn over the film, and must not follow the UI scheme.
- **Subtitle parsing must not block the GUI thread.**
- **No commit hashes in comments or documentation.** Name the change instead —
  "when the search proxy became hand-written" survives a rebase, a rewrite or a
  squash; `a5e489f` does not. Every hash cited in this tree went dangling at
  once when the branch was rewritten to a single author identity, and the
  sentences around them were still perfectly clear without the hash.

## Commit messages

Match what is already there — run `git log` and read a few. Subjects are a plain
sentence saying what changed, and bodies explain *why*, particularly when the
reasoning would not survive being rediscovered. Several commits here exist mainly
to record a measurement or a false start, which is the point.

Don't hard-wrap mid-sentence; break at sentence boundaries.

## What is most useful right now

0.5.0 is feature complete, so the useful work is release engineering rather than
features. [`docs/roadmap.md`](docs/roadmap.md) is ordered by value and honest
about what is blocked. The top of it is pushing, so that the CI workflow —
written, but never executed, because nothing has been pushed — runs for the
first time; packaging comes next, AppImage first, and does not exist in any
format yet; after those, the release hardening in the roadmap's loose ends,
none of which is a feature either.

Things that need a human rather than a patch: anything requiring a screenshot to
judge, anything on Windows, and the 4K/HEVC/HDR measurements that the Windows
port was argued for and which nobody has taken.

## License

GPL-3.0-or-later. By contributing you agree your work is licensed the same way.
