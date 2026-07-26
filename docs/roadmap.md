# Where the project is, and what is next

> Where the project stands as of the most recent milestone, and the work that follows.
> Split out of `CLAUDE.md`, which is the entry point and links here.

## State

Milestones 1–4 (extraction, browser UI, player usability, caching and settings)
and **milestone 5** are all committed. Milestone 5 was the pre-release pass: it
came out of two reviews, one architectural and one of the interface, and touched
most of the tree.

What it changed, briefly:

- **Correctness.** Playback split out of the scene-graph item into `MpvEngine`,
  so teardown runs in the order libmpv requires. Exact, millisecond-formatted
  seeks — `QString::number(double)` is six significant digits, so clicking a cue
  three hours into a film landed 123 ms off. Bounded cue-cache counts. Shared
  entity decoding, so a cue's styled and plain text can no longer disagree.
  Explicit `textFormat` everywhere, closing an `AutoText` path where a track
  title out of a container could have made the player fetch an `<img src>`.
- **The interface.** A design system with measured contrast, a control library
  replacing the Basic style (which was never selected, and so drew every button
  in the app), vector icons, a two-row transport, a rebuilt subtitle row, and a
  settings window that did not previously exist.
- **Features.** Subtitle delay applied to the browser's timestamps as well as
  the picture, sync-to-this-line, per-cue seek, A-B loop, cue copy, screenshots,
  audio delay, picture adjustments, an OSD, a queue popover, remappable keys.
- **Release plumbing.** GPL-3.0-or-later, install rules, a desktop entry, an
  icon, and the rename from the MVP's *custom media player* to **Subixa** —
  carried through the CMake project, the binary, the desktop entry, the
  `SUBIXA_*` environment switches and, since, the repository directory itself.

Verified against the real 3 GB / 65-track film, not just the fixtures: 93 350
cues in 10.0 s cold and 33 ms cached, click-to-seek landing on the clicked line
with the picture to match, and the track picker that replaces a 65-tab strip.

Six suites pass, the render canary passes, and a from-scratch build is clean
under `-Wall -Wextra`.

## Next

In rough order of value.

1. **CI and packaging.** There is a `LICENSE`, install rules, a `.desktop` entry
   and an icon, but no CI, no AppImage or Flatpak, and no `metainfo.xml`.
   Flatpak is the right primary Linux target: FFmpeg and libmpv versions are the
   biggest portability variable and the runtime pins them. Minimum viable CI is
   an Ubuntu matrix running `ctest`, plus a sanitizer job — `-fsanitize=address`
   would have caught the render-context lifetime bug directly.

2. **One settings service.** Three independent `QSettings` writers remain (the
   QML `Settings` objects, `PlaybackHistory`, `ShortcutRegistry`) with no schema,
   no migration hook, and `PlaybackHistory::remember` calling `sync()` on a
   five-second timer, which rewrites the whole file including whatever the QML
   `Settings` objects are buffering.

3. **Drain `Main.qml`.** Still around 1 300 lines. The two-namespace subtitle
   track reconciliation is the most intricate logic in the product, it is untyped
   JavaScript, and its only coverage is one integration test. It belongs in C++
   beside `MpvTrackList.h`.

4. **The search proxy.** Filtering is still a linear scan over every cue per
   keystroke, on the GUI thread, coalesced by a 150 ms timer. The fix is
   incremental narrowing: when the new pattern extends the old one, only
   currently-accepted rows can still match, which makes every keystroke after the
   first O(matches). `QSortFilterProxyModel` cannot express that, so it means a
   small custom proxy.

5. **A Windows build**, when hwdec, 4K/HEVC or HDR need judging. Note the tell:
   the product thesis is "PotPlayer's subtitle list, done properly", and
   PotPlayer's users are on Windows. It would also sidestep trap 22 entirely,
   since Mesa's D3D12 driver exists only for WSL.

6. **Per-style defaults and native `.ass` parsing.** The styling rendered comes
   from the *override tags* in each cue; an ASS file's `[V4+ Styles]` table never
   reaches the extractor, so a track styled entirely that way reads plain in the
   list and italic on the picture. Native parsing would also give the Name/Actor
   field, which is a strong browser column, and make `.ass` round-trip export
   possible.

## Loose ends

Worth folding into whatever touches them next.

- **Trap 22 has no upstream fix and no bug report.** Reproduced on Mesa 26.1.5,
  the current release. The report was written off deliberately, not forgotten.
  The reproducer is twelve lines of QML against Qt's own `qml` binary if it is
  ever wanted.
- **`PaintedText` has no test.** It is a `Text` replacement with its own metrics,
  wrapping, eliding and `QTextDocument` path, and nothing asserts it agrees with
  the native item. A headless comparison of `contentWidth`/`contentHeight`
  between the two would be cheap, and would catch a layout regression that a
  screenshot on a healthy driver never would.
- **The canary cannot run itself.** It needs a window and the Windows-side
  capture, so it is a tool rather than a `ctest` case, and nothing makes anyone
  run it. An in-process `grabWindow()` could not replace it — trap 10 records
  `toImage()` returning a *perfect* frame while the screen was wrong.
- **`hostileCacheCountsAreRefused` pins behaviour, not the allocation.** Removing
  the bound and re-running leaves every case passing, because the read loop then
  fails on the next record and reports the same miss one enormous `reserve()`
  later. Making that observable needs a memory-limited run or an allocation hook.
- **The FBO cap's threshold is a guess**, if a conservative one. `FboCap::SafeArea`
  is 2.0 MP, chosen below the observed boundary (2.86 MP clean, 2.99 MP corrupt)
  rather than at it, because the failure depends on render load as well as area.
  Nobody has mapped whether the real variable is area, height, or something else.
- **Path identity is `absoluteFilePath`**, which does not resolve symlinks or
  `..`, in `PlaybackHistory`, `SubtitleCache`, `Playlist` and `MpvTrackList`. The
  same film reached through a symlink gets two cache entries and two resume
  positions, which a user experiences as "it forgot where I was". One
  `canonicalFilePath` helper, four call sites.
- **`SubtitleExtractor` has no ffmpeg interrupt callback**, so
  `avformat_open_input` on a stalled network or 9p mount can block indefinitely,
  and `~SubtitleManager` waits on the thread unbounded — so the app will not exit.
- **No `qsTr()` anywhere, and no accessibility.** Both get harder the longer they
  wait, and for a *reading* tool the second is more relevant than usual.
- **Prev/next are invisible with one file in the folder.** Correct behaviour, but
  it reads as broken; they should be visible and disabled instead.
- **The QML harness cannot see anything that needs pixels.** It runs `offscreen`,
  so delegate geometry, the FBO cap and whether the panel actually *scrolled* are
  outside it. Everything visual in milestone 5 was checked by screenshot instead,
  which is why the layout traps (17–21) were each found once by eye rather than
  caught by a test.
