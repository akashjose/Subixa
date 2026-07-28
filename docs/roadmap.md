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

Since then, 13 commits changed the ground the rest of this document stands on.

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
exercised by hand but not by a test. And **the Windows build cannot configure
until MSYS2 reaches Qt 6.12**, because the floor was raised deliberately.

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
themselves. `testdata/conformance/` is 35 KB committed as bytes, and
`golden.tsv` pins what the extractor makes of it, cue by cue. It is the only
mechanism in the tree by which "both platforms agree" is evidence rather than
coincidence.

**The application id exists**: `com.akashjose.Subixa`, keying the desktop entry,
the icon and any future AppStream or Flatpak metadata. `StartupWMClass` stays
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

`SubtitleFilterModel`'s seven untested invokables now have characterization
tests, pinning what the class does today because item 5 below plans to replace
it and those are the invariants a base-class swap breaks silently.

Seven suites pass, warning-free under `-Wall -Wextra` on every target:
40 cases in `tst_subtitles`, 23 in `tst_playbackhistory`, 14 in `tst_qmlpanel`.

## Next

In rough order of value. The work recorded above sits mostly *underneath* these
rather than through them — of the six the previous version of this file listed,
one is partly done (the settings item, item 3) and the rest are untouched.

1. **CI, and then packaging.** Still no CI anywhere — no `.github`, no
   `metainfo.xml`, no AppImage, Flatpak or `.deb`. This ranks above the
   engineering items because everything below is verified by "ctest passes", and
   until a machine runs ctest that sentence depends on someone remembering to.
   Budget for the part nobody accounts for: a runner has to build the media stack
   from source via `tools/build-deps.sh` and fetch Qt via `aqtinstall`, so the
   prefix must be cached or every push costs twenty minutes.

2. **Per-style defaults and native `.ass` parsing.** The styling the browser
   renders comes only from the *override tags* in each cue; an ASS file's
   `[V4+ Styles]` table never reaches the extractor, so a track styled entirely
   that way reads plain in the list and italic on the picture. The two halves of a
   subtitle-reader's screen visibly disagree, and no fixture catches it. The
   header is already in memory — `avctx->subtitle_header` after
   `avcodec_open2` — and the same pass gets the Name/Actor field, which is a
   strong browser column. Bump `SubtitleCache::kFormatVersion` with it (trap 14).

3. **One settings service.** Five independent writers land in one file with no
   schema and no version key: `PlaybackHistory` and `ShortcutRegistry` each hold
   a `QSettings`, `GraphicsSetup.cpp:132` constructs one on the stack, and
   `qml/Main.qml` has two `Settings` blocks (`:68`, `:134`).

   `PlaybackHistory`'s half of this landed in `f800e8c`: it holds the position
   in memory and writes only once it has moved `PositionWriteStep`, instead of
   handing every tick to `QSettings`. **The trap this file used to record here
   was not real** — it claimed that timer was the only mid-session flush the QML
   `Settings` groups got, so removing it would drop their durability to
   exit-only. On Qt 6.12 `QQmlSettings` has its own write timer and `QSettings`
   syncs itself from the event loop, so a group reaches disk about a second
   after a change with nobody calling `sync()`. Measured against this Qt in a
   standalone program, not assumed. What remains is the consolidation: one
   service, a schema version key, and pruning.

4. **Drain `Main.qml`.** 1,502 lines, not the 1,300 the previous version of this
   file claimed — and it was already 1,502 in the commit that wrote that line.
   The two-namespace track reconciliation is the most intricate logic in the
   product and is untyped JavaScript. Note that half of it already exists in C++:
   `subtitleIdForStream`, `subtitleIdForFile`, `sameFile` and `isSelected` are in
   `src/MpvTrackList.h` with coverage in `tests/tst_mpvtracks.cpp`. What remains
   is the browser-namespace half and the arbitration between them.

5. **The search proxy.** Filtering is a linear scan over every cue, on the GUI
   thread. Measure before building: the payoff the previous version claimed —
   "every keystroke after the first is O(matches)" — targets a case that
   `qml/SubtitlePanel.qml`'s 150 ms debounce already caps, so keystrokes never
   reach the proxy. The expensive scan is the first one, which incremental
   narrowing cannot help. The real cost may be that `invalidateRowsFilter`
   re-tests every rejected row and emits `countChanged` once per contiguous run.

6. **Finish the Windows build.** Blocked on MSYS2 reaching Qt 6.12, then: nothing
   is packaged, deployment is a documented command sequence rather than a script,
   and nothing is signed. Azure Trusted Signing at about $10/month is the only
   certificate option that works headless in CI — OV certificates have needed a
   hardware token since June 2023. The reason the port was argued for is still
   unmeasured: 4K, HEVC and HDR have not been judged. `hwdec` is no longer among
   them — it resolves to `d3d11va-copy` on Windows and `vaapi-copy` on this Linux
   host.

## Loose ends

Worth folding into whatever touches them next.

- **Resume should be optional, and it is not.** Reopening a film always returns
  you to where you stopped, and some people do not want that — a rewatch, a
  shared machine, or simply a preference. The policy already exists and is
  conservative (a clip under two minutes, the first thirty seconds and the last
  minute are never remembered, and finishing clears the position), so this is a
  switch in Settings -> Playback over machinery that is already there rather than
  new behaviour. Worth deciding whether "off" means *do not restore* or *do not
  record*, because the second also disables remembering which subtitle track was
  being read, and on a 65-track film that is the more valuable half.
  Note the write cadence changed in `f800e8c`: the position is now held in memory
  and written once it has moved `PositionWriteStep` (30 s), so a crash loses up
  to thirty seconds of it rather than five. Accepted deliberately; the constant is
  named and commented if it wants lowering.

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
- **`PaintedText` has no test**, and neither does `GraphicsSetup` (175 lines, in
  no test target) or `MpvEngine` (894 lines, no suite of its own).
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
- **`docs/traps.md` records no platform for any of its 25 entries**, and several
  read as universal that are not — 1, 20 and 22 are WSLg-only, 9 and 10 are
  software-rasterizer-only, 24 and 25 came off an ordinary Ubuntu desktop. One is
  openly unresolved (around line 143). Adding a scope line to each is cheap and
  would have saved time already.
- **No `qsTr()` anywhere, and no accessibility.** Zero files. Both get harder the
  longer they wait, and for a *reading* tool the second is more relevant than
  usual. Note `SubtitleManager.cpp` builds `"%1 track%2, %3 line%4%5"` by
  appending an English `s`, which a translator cannot reorder.
- **`AppMenu.fitToContents` has no maximum**, so a pathologically long track name
  produces a very wide menu instead of an overlapping one. Better, not right.
- **Trap 22 has no upstream bug report.** Written off deliberately. The
  reproducer is twelve lines of QML against Qt's own `qml` binary.
- **Nothing is pushed.** The branch of record is `feature/finish_all_milestones`;
  `master` sits at `721ebb6` with none of the Windows work, the UTF-8 path fix or
  anything above.
