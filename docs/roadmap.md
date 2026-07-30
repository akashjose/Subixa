# Where the project is, and what is next

> Where the project stands, and the work that follows.
> Split out of `CLAUDE.md`, which is the entry point and links here.

## How much of this document to trust

This file was rewritten on 2026-07-29 after a ten-agent audit checked every claim
in it against the tree and found **nineteen that were wrong** — items listed as
outstanding that had already landed, counted figures that no longer matched, and
one item asking for work that already existed in `MpvTrackList.h` with tests. It
had drifted because it was written from memory rather than from the code.

So: the numbers below were measured on the day of writing, and the ones that go
stale fastest — line counts, version numbers, "there is no X" — are the ones to
re-check rather than repeat. If you are about to quote a figure from here into a
commit message or a plan, run the command first.

## State

Milestones 1–5 are committed, and so is the work that followed on the
`windows-build` branch: a Windows build under MSYS2 UCRT64, the fixes an ordinary
Linux desktop turned up that WSL had hidden (traps 24 and 25), and the mouse work
— folder drops, Esc to minimise, wheel volume, right-click menu.

Since then, 27 commits changed the ground the rest of this document stands on.

**The dependency stack moved to current releases and is now built from source.**
Linux was on Qt 6.9.3, FFmpeg 6.1.1 and mpv 0.37 — a 2023 stack — while the
Windows side, which rolls, was already on FFmpeg 8 and Qt 6.11. Those were two
platforms decoding subtitles with decoders two majors apart, which is trap 5
waiting to happen *across* a build rather than within one. No distribution can
close that: Ubuntu 24.04's apt candidate for `libavcodec-dev` is the installed
6.1.1 and there is no upgrade. `tools/build-deps.sh` builds libplacebo 7.360.1,
FFmpeg 8.1.2 and mpv 0.41.0 from pinned versions into a prefix, and
`-DSUBIXA_DEPS_PREFIX=` points CMake at it.

| | |
|---|---|
| Qt | 6.12.0 |
| FFmpeg | 8.1.2 (`libavcodec` 62.28.102) |
| mpv | 0.41.0, client API 2.5.0 |
| libplacebo | 7.360.1 |
| Compiler | gcc 14.2, C++23, no extensions |

Two consequences worth knowing. mpv 0.41 makes libplacebo's `gpu-next` the
default renderer, so that bump changed the render path under the most delicate
code in the project — the FBO handling trap 10 is about — and it has been
exercised by hand but not by a test. And the Qt floor is now **platform-split**
— 6.12 on Linux, 6.11 on Windows — after a spell at 6.12 everywhere left the
Windows build unable to configure at all. See below.

**The test suite stopped lying.** Three suites — about 2,080 lines of assertions
— called `QSKIP` when fixtures were absent, and `QSKIP` exits 0, so a checkout
that had never run `make-fixtures.sh` reported six green having asserted almost
nothing. `REQUIRED_FILES` now fails those suites instead, `make-fixtures.sh`
generates the base clip rather than printing the command for it, and the seven
`QSKIP`s are `QVERIFY2`.

A first attempt at that fix made it worse and is worth remembering:
`SKIP_REGULAR_EXPRESSION` was added as a "backstop", and because it matches a
test's whole output rather than its exit status, a run that printed the phrase
*and* failed an assertion *and* exited 1 was reported `***Skipped` with ctest
exiting 0. It reopened the exact defect it was meant to close. Measured, then
deleted.

**There is a conformance corpus.** `git ls-files testdata/` used to return four
files; every container was muxed at test time by whatever ffmpeg was installed,
so a Linux run and a Windows run each decoded inputs they had produced
themselves. `testdata/conformance/` is 47 KB committed as bytes, and
`golden.tsv` pins what the extractor makes of it, cue by cue. It is the only
mechanism in the tree by which "both platforms agree" is evidence rather than
coincidence.

**The application id exists**: `com.akashjose.Subixa`, keying the desktop entry,
the icon, the AppStream metainfo and any future Flatpak metadata. `StartupWMClass` stays
`subixa` and the difference is deliberate — measured with `xprop`, X11 takes
`WM_CLASS` from `applicationName` while only Wayland uses the desktop file name.

**The browser listed the wrong track, and no longer does.** Choosing a subtitle
track from the transport menu moved the panel's tab and left `currentTrack`
where it was, so the browser went on listing the previous track's cues and
export wrote them. Two pieces of state for one decision: `currentTrack` is now
derived from `panelUi.tabIndex` rather than assigned. Prev/next were separated
from the queue chip in the same pass, so a folder holding one film shows them
disabled rather than losing them. Both carry regression tests that fail without
the fix.

**The browser reads the ASS styles table.** Styling came only from the override
tags inside each cue, so a track styled entirely through named styles — how most
professionally authored ASS is written — read plain in the panel while libass
rendered it italic and coloured on the picture. `avctx->subtitle_header` was
already in memory and nothing read it. The cache format went 2 → 3 with the
record layout (trap 14), and the actor name is retained — and, as of
2026-07-29, shown: a speaker overline above the cue text, screenplay-style,
only on rows that name one, toggleable on the Browser settings page and pinned
by a qmlpanel test against the committed `styletable.mkv`. Search still
matches the cue text only; whether it should also match the speaker is an open
question for when someone actually wants it.

That change also exposed a hole in the corpus and closed it. `golden.tsv`
recorded only the tag-stripped plain text, in which a cue styled through its
table is byte-identical to an unstyled one — so the fixture could have been
added, the golden regenerated, and nothing would have moved. It now records the
style name, the actor and the styled markup.

**The search proxy was replaced, on a measurement rather than the argument this
file used to make.** One filter change over 200k cues took 326 ms under
`QSortFilterProxyModel`, of which the predicate was 12 ms; scanning the same
track from scratch took 14 ms. Rebuilding is 23× faster than updating
incrementally. The seven characterization invokables were the specification and
were not touched.

**The track reconciliation moved to C++**, `qml/Main.qml` 1515 → 1459. That is
3.7% and worth stating as such: the value is that the most intricate logic in the
product now has a compiler and 20 cases in `tst_mpvtracks`, not the line count.

**The settings file has one owner, and per-file memory is a choice.**
`src/SettingsService`: `meta/schemaVersion`, migration, and pruning of the
per-file entries — a year unwatched or past the newest 500 per group, aged by
a `lastUsed` stamp; entries predating the stamp are stamped rather than
dropped, and a file written by a *newer* schema is left entirely alone.
`GraphicsSetup` takes the store as a parameter; `PlaybackHistory` and
`ShortcutRegistry` borrow it through `instance()` with an owned fallback. The
two QML `Settings` blocks stay, deliberately: migration runs in `main()`
before the QML engine exists, `QQmlSettings` self-syncs fine on Qt 6.12
(measured in a standalone program, not assumed — the write-cadence trap this
file once recorded here was not real), and converting ~44 properties to
Q_PROPERTY boilerplate would buy compile-time names at the cost of recreating
the trap-19 injection surface. On top of the service, resume and the
remembered subtitle track became two independent toggles under Settings →
Playback that gate only the *restore* — history keeps recording, so finishing
a film still clears its position with resume off, and switching a toggle back
on remembers everything. Both gates carry mutation-checked regression tests.

**The release machinery exists, and none of it has run on another machine.**
`.github/workflows/ci.yml` — metadata validation, an Ubuntu 24.04
Debug/Release matrix with the media stack cached on a hash of
`tools/build-deps.sh`, and an MSYS2 UCRT64 job gated on MSYS2 reaching the Qt
6.12 floor (6.11.1 on 2026-07-29, so it skips with a notice until then).
`com.akashjose.Subixa.metainfo.xml` validates and installs. `cmake --install`
writes real rpaths, `tools/install-linux.sh` scripts the update — which is how
the development machine now runs the player day to day — and
`tools/deploy-win.sh` scripts the Windows staging.

**The Windows side is current again, and was not.** For three days the 6.12
floor made `build-win/` impossible to reconfigure, so it went on staging a
bundle from a build tree two days behind the checkout — right DLL count, right
size, correct playback, and none of the work committed in between. Nothing
about the bundle gave it away; what did was a missing `meta/schemaVersion` key
in the registry. The floor is now split, Windows sits at 6.11, and the
justification the 6.12 floor was written on had already expired: the
`QSortFilterProxyModel` API it named left the tree with `a5e489f`.

Rebuilding at HEAD then surfaced a real crash that nothing on Linux had seen.
`SubtitleFilterModel` connected its source's `destroyed` signal to
`setSourceModel(nullptr)` as a safety net, commented "this should never fire" —
and at teardown it fires, running `begin/endResetModel` into a
`QQmlDelegateModel` being destroyed in the same sequence. `qmlpanel` segfaulted;
Qt 6.12 on Linux survives the identical sequence, which is why ten green suites
on one desk had never shown it. The handler now drops its state without
announcing a reset. **It is almost certainly latent on Linux too** — nothing
about emitting a reset from a destructor chain is platform-specific — and it has
no regression test.

`tools/build-win.sh` exists so that sequence is reproducible: configure, build,
fixtures, `ctest`, then `tools/deploy-win.sh`, with `set -e` making a staged
bundle unreachable from a tree that does not pass. Both mistakes this recorded
were staging mistakes, and neither is reachable through it.

Ten suites pass, warning-free under `-Wall -Wextra` on every target.

## Next

**Feature complete is the honest word for where this sits** — nothing below is
a feature. What stands between 0.5.0 and something releasable is release
engineering, in this order:

1. **Push.** The workflow has never executed: nothing is pushed, so "ctest
   passes" is still a sentence about one desk, and the first push is also the
   workflow's first real test — budget a debugging round for it. `master`
   stays at `721ebb6` and merges at release time, not before. Only the
   repository owner takes this step.

2. **Packaging, AppImage first.** The stack argues the order: Subixa needs
   FFmpeg 8, mpv 0.41 and libplacebo 7.3, and no distribution ships them — so
   a `.deb` cannot declare its dependencies from any archive and would bundle
   under `/opt` anyway, which throws away most of what a `.deb` is for. An
   AppImage bundles the `ldd` tree by design, CI already has the stack built
   and cached, and the result is verifiable on this machine by running it.
   Flatpak is the cleanest long-term channel and belongs at release time, when
   `master` merges and the metainfo's screenshot URLs come alive. What
   packaging owes beyond the bundle is relocatability, which the absolute
   install rpaths deliberately do not attempt.

3. **Finish the Windows build.** No longer blocked: the floor split to 6.11
   there, and `tools/build-win.sh` runs configure, build, ten suites and
   staging from a clean checkout (2026-07-31, 223 DLLs, 298 MB, verified
   playing by capture). What remains is that nothing is *packaged* — a staged
   folder is not an installer — and nothing is signed. Azure Trusted Signing at
   about $10/month is the only
   certificate option that works headless in CI — OV certificates have needed a
   hardware token since June 2023. The reason the port was argued for is still
   unmeasured: 4K, HEVC and HDR have not been judged. `hwdec` is no longer among
   them — it resolves to `d3d11va-copy` on Windows and `vaapi-copy` on this Linux
   host.

## Loose ends

Release hardening, none of it features. Worth folding into whatever touches
them next; the sizes are honest guesses.

- **`canonicalFilePath` appears zero times in the tree.** Path identity is
  `absoluteFilePath`, which resolves neither symlinks nor `..`, across
  `PlaybackHistory`, `SubtitleCache`, `Playlist` and `MpvTrackList`. The same
  film reached through a symlink gets two cache entries and two resume positions,
  which a user experiences as "it forgot where I was". A naive swap is worse than
  the bug: `canonicalFilePath` returns empty for a path that does not exist, so
  every missing file would hash to the same key and collapse onto one entry.
- **`SubtitleExtractor` has no ffmpeg interrupt callback**, so
  `avformat_open_input`, `find_stream_info` and `av_read_frame` are all unbounded
  on a stalled network or 9p mount, and `~SubtitleManager` waits on the thread —
  so the application will not exit. The code and a FIFO-based test are
  straightforward; proving it against a genuinely stalled mount is not.
- **`PaintedText` has no test**, and neither does `GraphicsSetup` (174 lines, in
  no test target) or `MpvEngine` (914 lines, no suite of its own).
- **The canary cannot run on a machine that is not WSL.** It is registered as a
  `DISABLED`, `RUN_SERIAL`, `manual` ctest so it has a name, but it shells to
  `powershell.exe` and `tools/wsl-screenshot.ps1`. On X11 its three checks —
  painting, moving, right clip — are a few lines with `import` and two captures a
  second apart. Nothing has been written.
- **The QML harness runs `offscreen`**, so delegate geometry, the FBO cap and
  whether the panel actually *scrolled* are outside it. Both cheap substitutes
  are ruled out in writing: Xvfb returns byte-identical captures a second apart,
  and trap 10 records `toImage()` returning a perfect frame while the screen was
  wrong. This is why traps 17–22 were each found by eye.
- **`hostileCacheCountsAreRefused` pins behaviour, not the allocation.** Removing
  the bound leaves every case passing, because the read loop then fails on the
  next record one enormous `reserve()` later.
- **The FBO cap's threshold is a guess.** `FboCap::SafeArea` is 2.0 MP, chosen
  below the observed boundary (2.86 MP clean, 2.99 MP corrupt) rather than at it.
  Nobody has mapped whether the real variable is area, height or render load.
  The same experiment would settle trap 10's still-open scope question —
  llvmpipe generally, or only under WSLg? — and it is newly possible here:
  `LIBGL_ALWAYS_SOFTWARE=1` on this native host produces llvmpipe with no WSLg
  in the picture, which nothing could do when the note was written.
- **The write cadence knob.** A crash loses up to thirty seconds of position
  (`PositionWriteStep`, from `f800e8c`), named and commented if it wants
  lowering.
- **No `qsTr()` anywhere, and no accessibility.** Zero files. Both get harder the
  longer they wait, and for a *reading* tool the second is more relevant than
  usual. Note `SubtitleManager.cpp` builds `"%1 track%2, %3 line%4%5"` by
  appending an English `s`, which a translator cannot reorder.
- **`AppMenu.fitToContents` has no maximum**, so a pathologically long track name
  produces a very wide menu instead of an overlapping one. Better, not right.
- **Trap 22 has no upstream bug report.** Written off deliberately. The
  reproducer is twelve lines of QML against Qt's own `qml` binary.
