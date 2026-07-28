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

## Since milestone 5

Work on the `windows-build` branch, not a milestone of its own. Three strands.

- **A Windows build exists.** MSYS2 UCRT64, gcc rather than MSVC, all six suites
  passing, and a deployable payload of roughly 170 DLLs and 300 MB. `README.md`
  has the toolchain, the configure line and the deployment sequence; what the
  port turned up is in `docs/graphics.md`. Item 5 below is what remains.
- **It runs on an ordinary Linux desktop.** Installed on Ubuntu rather than run
  under WSL, three things broke that WSL had hidden: `mpv_create` returns null
  under any `LC_NUMERIC` but `C`, gdk-pixbuf sniffs only a file's first kilobyte
  for `<svg` so the icon was blank wherever GNOME drew one, and nothing tied a
  running window to `subixa.desktop` without `setDesktopFileName`. Traps 24 and
  25.
- **The window answers the mouse.** A drop takes folders now, expanded by
  `Playlist::expand` — depth-bounded, files before subfolders, symlinked
  directories skipped, and filtered in `setFiles` rather than in the walk so one
  unplayable file still reports mpv's error. Esc minimises outside fullscreen and
  stops a film but not an album, which needed mpv's albumart flag carried through
  `MpvEngine` so a tagged mp3 stops reading as a film. The wheel over the picture
  is volume, accumulating deltas and spending them a notch at a time so a
  trackpad's stream of small deltas is not forty steps. A right-click opens the
  same overflow menu object the transport button opens. That last one exposed a
  defect the shared components had all along: a `Menu` never consults its rows
  for its width, so every row longer than the background's fixed 220 overflowed,
  and `AppMenuItem` measured itself through `AppText`'s Loader and reported zero.

## Next

In rough order of value.

1. **CI and packaging.** There is a `LICENSE`, install rules, a `.desktop` entry
   and an icon, but no CI, no AppImage or Flatpak, and no `metainfo.xml`.
   Flatpak is the right primary Linux target: FFmpeg and libmpv versions are the
   biggest portability variable and the runtime pins them. Minimum viable CI is
   an Ubuntu matrix running `ctest`, plus a sanitizer job — `-fsanitize=address`
   would have caught the render-context lifetime bug directly. Packaging now has
   two targets rather than one: the Windows deployment sequence in `README.md` is
   the other half, and it is a sequence of commands rather than a script.

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

5. **Finish the Windows build.** It exists — MSYS2 UCRT64, gcc rather than MSVC,
   all six suites passing, and a deployable payload; `README.md` has the steps
   and `docs/graphics.md` what the port turned up. The thesis argued for it: the
   product is "PotPlayer's subtitle list, done properly", and PotPlayer's users
   are on Windows. What remains before it is a target rather than a build:

   - **A non-ASCII path broke subtitle extraction** — `QFile::encodeName` is the
     local codepage there, and `avformat_open_input` refuses what it produces.
     Found by measurement rather than suspected, and fixed: the call site now
     passes `toUtf8()`, with a regression test that was checked against a
     reverted build. See `docs/graphics.md`.
   - **Nothing is packaged.** Deployment is a documented sequence of commands,
     not a script; there is no installer and nothing is signed. The sequence at
     least works off the PATH now — trying it that way found `windeployqt6`
     exiting 0 having staged nothing, because it looks for `qmlimportscanner`
     beside itself and MSYS2 ships it in `share/qt6/bin`, and then a bundle that
     would not start, because MSYS2 puts the qml tree under `share/qt6` and
     relocation preserves that offset while `windeployqt` stages to `qml/`. A
     `qt.conf` reconciles them. Verified with `PATH` cut to `system32`, which is
     MSYS2 off the PATH on this machine and still weaker than a clean one. Folds
     into item 1.
   - **The reason for building it is still unmeasured**: hwdec, 4K/HEVC and HDR
     were what WSL could not judge, and none of them have been judged yet.
   - It does sidestep trap 22, as predicted — Mesa's D3D12 driver exists only
     for WSL — though `AppText` still chooses at runtime, so no QML changes.

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
- **Menus have no maximum width.** `AppMenu.fitToContents` sizes a menu to its
  widest row, which fixed rows overflowing their background but leaves a
  pathologically long track name producing a very wide menu instead of an
  overlapping one. Better, not right.
- **Prev/next are invisible with one file in the folder.** Correct behaviour, but
  it reads as broken; they should be visible and disabled instead. Still true:
  `TransportBar.showQueue` gates `visible` on `playlist.count > 1`, and the
  buttons already carry the `enabled` binding the fix wants.
- **The QML harness cannot see anything that needs pixels.** It runs `offscreen`,
  so delegate geometry, the FBO cap and whether the panel actually *scrolled* are
  outside it. Everything visual in milestone 5 was checked by screenshot instead,
  which is why the layout traps (17–21) were each found once by eye rather than
  caught by a test.
