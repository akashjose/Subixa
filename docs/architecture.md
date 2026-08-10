# Architecture

> What the modules are, and the decisions behind the ones that are not obvious.
> Split out of `CLAUDE.md`, which is the entry point and links here.

## The three decisions everything else follows from

**Built from scratch, not forked.** VLC, Haruna and SMPlayer all carry
architecture shaped around their own UI goals, and the feature this project
exists for — a docked, searchable subtitle list that the video composites
underneath — is not something any of them is arranged to grow. libmpv is a
dependency here, not a base.

**The mpv render API, not `--wid`.** The video surface is a
`QQuickFramebufferObject`: mpv renders into an FBO the application owns, so it is
an ordinary scene-graph node and QML composites freely on top of it. With `--wid`
mpv draws into a separate native surface *above* the scene graph, which makes
overlays and docked panels unreliable. Since the entire premise is QML chrome
over video, `--wid` is not an option. The consequences of that choice are what
traps 1, 2 and 10 are about, and it is also why `src/main.cpp` pins the scene
graph to OpenGL.

**Subtitle text is parsed independently of mpv.** This is the least obvious of
the three and the one most likely to be undone by someone tidying up, so: mpv
exposes only the *currently displayed* subtitle line, through the `sub-text`
property. There is no API for "give me every cue in this track", and there could
not usefully be one, since mpv's model is a renderer's. A browsable list needs
every cue up front, so subtitle streams are demuxed and decoded separately with
libavformat and libavcodec, on a worker thread, and cached on disk. That is why
the project links FFmpeg directly as well as libmpv, and why a change to either
library can move what the browser shows.

## Modules

```
src/MpvEngine.{h,cpp}         playback: owns mpv_handle, every property and command
src/MpvVideoItem.{h,cpp}      the video surface: owns only the render context and the FBO
src/PaintedText.{h,cpp}       text via QPainter, for drivers that miscolour glyphs
src/main.cpp                  OpenGL RHI, the Basic style, the engine, and argv parsing
src/GraphicsSetup.{h,cpp}     picks a GL driver before Qt makes a context, probing first
src/SettingsService.{h,cpp}   the settings file's owner: schema version, migration, pruning
src/ShortcutRegistry.{h,cpp}  every keyboard action, its default, and any rebinding
src/SubtitleTypes.h           SubtitleLine / SubtitleTrack plain structs
src/SubtitleExtractor.{h,cpp} libavformat/libavcodec parsing, runs on a worker thread
src/SubtitleText.{h,cpp}      entity decoding, shared by the flattened and styled paths
src/SubtitleCache.{h,cpp}     parsed cues on disk, so a reopen costs nothing
src/SubtitleManager.{h,cpp}   QML-facing owner of the worker and the parsed tracks
src/SubtitleLineModel.{h,cpp} QAbstractListModel over one track's cues
src/SubtitleStyle.{h,cpp}     ASS override tags -> markup the browser can show
src/SubtitleFilterModel.{h,cpp} search proxy, the row mapping auto-follow needs, the delay
src/PlaybackHistory.{h,cpp}   per-file resume positions and remembered tracks, on SettingsService's store
src/Playlist.{h,cpp}          what plays next: the folder as a queue, in natural order
src/FboCap.h                  how large a framebuffer to give mpv on a software rasterizer
src/MpvTrackList.h            maps browser tracks onto mpv's, header-only so it is testable
qml/Main.qml                  the window: services, actions, layout, and the file lifecycle
qml/TransportBar.qml          seek strip + grouped controls, adaptive by named breakpoint
qml/SubtitlePanel.qml         the browser itself, docked or in its own window
qml/SubtitleRow.qml           one cue: the rail, the timestamp column, the text
qml/SettingsWindow.qml        preferences, applied live -- no OK button
qml/Theme.qml                 the design system: colour, type, space, radius, motion
qml/ui/*.qml                  the control library (see below) + Icon/Icons
tools/wsl-*.ps1               screenshot and input injection from the Windows side
```

**Playback and the video surface are separate objects, and that is load-bearing.**
`MpvEngine` is a plain `QObject` created in `main()` *before* the `QQmlApplicationEngine`;
`MpvVideoItem` is the `QQuickFramebufferObject` that draws from it. They used to be one
class, which put the mpv handle inside a scene-graph item and made three things wrong at
once:

- **Teardown ran backwards.** Qt destroys a `Renderer` on the render thread *after* the
  item's destructor, so the handle was freed while the render context that referenced it
  was still alive — the reverse of what libmpv requires. Declaration order in `main()` now
  fixes it by construction: the QML engine is destroyed first, the window blocks on the
  render thread as it tears down, and every `mpv_render_context_free()` has completed
  before `~MpvEngine` runs. `MpvEngine` keeps an atomic count of live contexts and warns
  if that ever stops being true.
- **Nothing about playback could be tested without a window.** Everything on `MpvEngine`
  runs headless under `vo=null`.
- **Every new feature had to be bolted onto a `QQuickItem`** to reach mpv at all.

`MpvVideoItem::itemChange` is where `setPersistentGraphics`/`setPersistentSceneGraph` are
called. They were in `createRenderer()`, which runs on the **render thread**, so they were
reaching into `QQuickWindowPrivate` while the GUI thread was live. The renderer now samples
the window pointer, the mpv handle and the video size in `synchronize()` — the one place
where the GUI thread is blocked — and uses only those copies in `render()`.

mpv state reaches QML through observed properties surfaced as Qt properties — position,
duration, pause, tracks, volume, speed, and now `sub-delay`, `audio-delay`, `ab-loop-a/b`,
`sub-visibility`, `demuxer-cache-time` and `chapter-list` as well.

**Every command and property write is checked.** `MpvEngine::checked()` turns a negative
return into a `commandFailed` signal that reaches the notice banner. It used to be silent,
which is how a malformed sidecar produced a tab that simply did nothing. For the same
reason `mpv_initialize` failing is no longer `qFatal`: init genuinely fails in the field —
no audio device, a sandboxed container — and a core dump is not something a user can act on.
`main()` reports the reason and exits 1.

**Subtitle rendering options are allow-listed** (`setSubtitleOption`). The names all come
from our own settings window, but a general "set any mpv property from a string" invokable
is a far wider surface than this needs, and mpv has properties that load files.

`MpvRenderer` carries three workarounds for Mesa's software rasterizers, all keyed off a
single `GL_RENDERER` check made once at context creation and handed to everything
downstream (including QML, as `MpvEngine.softwareRendering`, which switches off shadows):
8-bit conversion for 10-bit video, a `glFinish()` before Qt samples the FBO, and the
framebuffer cap in `FboCap.h`. Traps 9 and 10 explain why each is needed and what was
ruled out first — do not remove any of them without reading those.

The cap is why `MpvVideoItem` sets `setTextureFollowsItemSize(false)`. With it on, Qt
compares the FBO's size against the item's every frame and destroys any that disagrees,
so a capped framebuffer would be recreated forever; with it off, recreation is asked for
explicitly in `MpvRenderer::synchronize()` when the target size changes.

**The UI is a design system plus an in-repo control library, not Qt's default style.**

`main.cpp` pins `QQuickStyle::setStyle("Basic")`. Before that it was unset, which meant
every `Button`, `Slider`, `TabBar`, `TextField`, `Menu` and `ScrollBar` was drawn by Qt's
reference style — unchanged in look since 2016, with its own hardcoded greys and geometry
that ignore any palette. `Theme.qml` only ever coloured the things the app drew *itself*, so
every actual control was off-palette. That was most of why it looked dated, and no amount of
colour work fixes it. It is pinned rather than left to the platform default so a Windows
build does not silently pick FluentWinUI3 and stop looking like the same program.

`qml/ui/` holds the replacements, built on `QtQuick.Templates` so they carry the behaviour
(hover, press, checked, focus reason) and none of the style's appearance: `IconButton`,
`TextButton`, `Chip`, `SearchField`, `AppMenu`/`AppMenuItem`, `AppScrollBar`, `AppSlider`,
`AppSwitch`, `AppSpinBox`, `AppComboBox`, `Segmented`, `SeekBar`, `Banner`, `EmptyState`,
`SectionCard`, `FormRow`, `ColorSwatch`, `ToolTipBubble`, `Divider`. A full custom Controls
*style* was considered and rejected: the app uses nine control types, and style resolution
plus `qtquickcontrols2.conf` plus fallback styles is a day of work for the same result.

`Theme.qml` is grouped tokens — `Theme.color.textPrimary`, `Theme.space.lg`,
`Theme.radius.md`, `Theme.motion.fast` — rather than the flat list of 24 colours it
replaced. That list had no scale for spacing, type, radius or motion, so every component
invented its own inline and nothing lined up.

**Contrast is checked, not asserted.** Every text token carries its measured WCAG ratio in a
comment, and body text clears 4.5:1 on every surface it is permitted on. The old palette
failed that in eight places — including the parse-progress readout at 3.22:1 and, worst, the
timestamp on the *currently playing* row at 2.64:1, so the highlight made the timestamp
harder to read than an ordinary row. `textTertiary` is deliberately allowed only on
`bgSurface` and `bgRaised`; on a selected row callers step up to `textSecondary`, and the
subtitle row delegate does exactly that.

**Icons are SVG path data in a QML singleton** (`ui/Icons.qml`), drawn by `QtQuick.Shapes`.
No files, no icon font, no decode, and tinting is one colour property rather than a colorize
pass — which matters because this app runs on a software rasterizer often enough that an
extra pass per icon is real. Note that `QtQuick.Shapes` has **no public CMake package** in
Qt 6.9 (only `Qt6QuickShapesPrivate`; unverified against 6.12); a dynamically linked build resolves the QML plugin at
runtime with no link-time dependency, and a future static build will need the private module
plus `qt_import_qml_plugins`.

`Theme.effectsEnabled` follows `MpvEngine.softwareRendering`, so shadows switch off on
llvmpipe. Every elevation level therefore defines a distinct surface colour *and* border as
well as a shadow — the UI has to be complete without a single one of them.

**The subtitle row is the one component with a written brief**: easy to follow, not
distracting. Four decisions carry it, and they are in `SubtitleRow.qml`'s header comment —
a 3px accent rail carries the signal while the fill only carries context; no border and no
corner radius, because the row it replaced triple-encoded its state and the rounded corners
broke the continuous column the eye scans down; text brightens rather than recolours, so the
row gains weight without changing character; and the change crossfades over 140 ms, below
which it strobes on rapid dialogue and above which it lags the audio. The layout is a
leading timestamp column rather than the timestamp stacked above the text — stacking gave
the eye no straight left edge, which is the entire ergonomic point of a timestamped list.

**The subtitle delay reaches the browser, not just mpv.** `SubtitleFilterModel::delayMs`
shifts `rowAt()` one way and `startMsAt()` the other, so auto-follow highlights the right
line and clicking a row still seeks to where it is actually spoken. Without that the two
halves of this player would disagree the moment anyone resynced, which is the one thing a
subtitle browser cannot do.

**The panel is one component in two homes.** `SubtitlePanel.qml` takes the manager, the
filter proxy and a `ui` object as properties rather than reaching for ids, so the same
component runs docked in a `SplitView` or alone in a `Window` (Ctrl+D, or the Detach
button). Only one instance exists at a time — each home is a `Loader` whose
`sourceComponent` is null when the other is active — so the item is destroyed and rebuilt
on every move. That is why search text, tab index and follow live in the caller's `ui`
object: anything stored inside the panel would be lost. It costs almost nothing because
the models are C++-side and passed by reference; a detach re-creates delegates, not cues.

Two QML details that bite here. A property named `lines` on the panel shadows the caller's
`lines` id inside the component's scope, so `lines: lines` silently binds to itself —
hence `linesModel`. And `TabBar` and `TextField` assign their own `currentIndex`/`text` on
interaction, which destroys a binding, so both are pushed back to `ui` by hand instead of
being bound. The tab index is the awkward one, and trap 13 explains why it is restored on
`populated` rather than in `Component.onCompleted`.

**What is remembered, and where.** Two stores, one file
(`~/.config/subixa/subixa.conf`), and since 2026-07-29 the file has one owner:
`SettingsService`, constructed on `main()`'s stack before anything reads it. It
holds the schema version (`meta/schemaVersion`), migrates on upgrade, and
prunes the per-file entries — a year unwatched, or past the newest 500 per
group, aged by a `lastUsed` stamp. `GraphicsSetup` takes its store as a
parameter; `PlaybackHistory` and `ShortcutRegistry` borrow it through
`instance()`, falling back to owning one when no service exists (which is how
their test suites run them). The QML `Settings` blocks stay declarative on
purpose — migration has finished before the QML engine exists, which is what
makes that safe.

- *Per application*, through the QML `Settings` type (`import QtCore`) in the `[ui]` group:
  window geometry and maximised state, panel width, visible, detached, the detached
  window's own geometry, volume and mute, theme and row text size. Restored in
  `Component.onCompleted` and saved on close rather than two-way bound — a binding from a
  window's width to a stored value is broken by the first resize anyway, and saving only
  while `Windowed` is what stops a fullscreen session writing the screen's size back as the
  window size. Volume and mute are written through immediately instead, so a `kill -9`
  cannot lose them.

  The opening geometry is the only size stored as a *window*, because it is restored
  before there is a layout to measure chrome against; the panel's state is restored
  beside it, so the picture comes back the size it was. Nothing else is a window size —
  there is no default window size, only a default *picture* of 1920×1080 with the
  browser docked beside it, and the window is whatever holds that. Every size the
  player states is the picture: the zoom presets, the resize readout, the reset button,
  the saved default behind Ctrl+0. `dockedPanelSpan()` and `transportSpan()` are the
  two halves of the conversion, and the first is what lets a Tab or a Ctrl+D give the
  window the width the browser released and hold the picture still. The panel span is
  computed rather than measured on purpose: it has to be right in the same pass the
  panel width changes in, which a measurement is not. Size and position restore as a
  set: a half-restore left a stale size reopening forever with the switch off.
- *Per file*, in `PlaybackHistory`: the resume position, **which subtitle track was being
  read**, and **which audio track was playing**. In separate groups on purpose — finishing a
  film clears its position by design, and that must not also forget the tracks. Embedded
  tracks are stored by ffmpeg stream index and sidecars by absolute path, the same split
  `MpvEngine` uses to select them, because mpv's own numbering follows from neither.

**Which track a new file opens on.** A file with no entry of its own is answered by a
cross-file preference, held per stream type in three modes. *Follow the file* is the absence
of an opinion; *learn* forms one by watching what gets chosen; *explicit* is an ordered list
stated up front. Only an explicit choice is ever learned — a tab click, the transport menu,
a cycle key — never what the sync handlers do, or mpv's default would overwrite the reader's
track on every open.

Three things about it are load-bearing:

- **A preference is a language *and a flavour*.** A release ships plain English and English
  SDH tagged alike, so language alone put the plain track up in the next episode however
  deliberately SDH had been chosen in this one. `forced`, `hearingImpaired` and
  `visualImpaired` come off the container's disposition bits, falling back to the track
  title, which is where most releases actually say it.
- **Within an entry the language is a requirement and the flavour a preference.** A file
  whose only English track is plain still gets English rather than falling through to
  another language; across entries, order decides.
- **Not matching is not the same as matching nothing.** `preferredTrackChoice` returns
  `matched` beside the index, because the panel must light *some* tab while mpv must be told
  nothing. Pushing the fallback tab at mpv is exactly what *follow the file* asks us not to
  do, and `Main.qml` gates on that flag rather than re-applying unconditionally.

**One spelling of a language.** ffmpeg reports what the container holds (`eng`, `jpn`), mpv
publishes two-letter codes (`en`, `ja`) and sometimes region-tagged ones (`es-419`), and the
settings page offers a third list. `MpvTrackList::canonicalLanguage` folds all of them to
ISO 639-2/B before anything is compared or stored. Without it an audio preference of `eng`
matched no track mpv called `en` — the preference did nothing at all, on every file, which
reads exactly like a feature that was never wired up.

The restores are optional: *Resume where you left off*, *Remember the subtitle track per
file* and *Remember the audio track per file*, under Settings → Playback, independent
because finishing a film clears its position and must not forget the tracks. Off gates only
the restore — recording continues, so nothing is lost to the period a toggle was off, and
finish-clears-the-position keeps working either way. The per-file toggle and the mode are
different questions: the toggle says whether *this film's* entry is honoured, the mode says
what answers when there is none.

**The browser shows the subtitler's own styling** (`SubtitleStyle`). `rawText` was kept per
cue from the start for this: italics, bold and speaker colours carry meaning, and the list
was the one place they were thrown away while the picture kept them. The payload becomes
Qt's StyledText subset — `<i>`, `<b>`, `<u>`, `<font color>`, `<br>` — and everything that
says *where* or *when* to draw (`\pos`, `\fad`, karaoke timing) is dropped, because a list
of lines has no use for it.

Three decisions worth keeping:

- **Built per visible row, never stored.** It is derived from `rawText`, only the twenty
  rows on screen ever ask for it, and a third string per cue would cost megabytes on a
  93 350-cue film for something nobody is looking at.
- **Cue colours are adjusted for the row they land on.** Subtitle colours are chosen to sit
  over a picture, so white dialogue on the light theme would be invisible. `readableOn()`
  keeps the hue and saturation — that is what says *which speaker* — and walks the lightness
  until the WCAG contrast ratio against the row reaches 4.5. A plain brightness comparison
  was tried first and called pure red on a near-black row unreadable, which it is not. The
  manager pushes the theme's colour down to every model, so one binding in `Main.qml`
  restyles the lot.
- **Search still matches the plain text**, never the markup, so what is shown and what is
  matched cannot disagree — and turning styling off in the More menu changes only the
  rendering.

**What plays next is a folder, not a playlist.** Opening any file makes its folder the
queue with that file current; dropping several files makes exactly those the queue, in the
order they were dropped. `openFile()` rebuilds the queue only for a file that is not
already in it, which is what keeps a dropped set from being replaced by its folder at the
first advance. There is no playlist panel and there should not be one — the subtitle
browser is the feature this player exists for, and a second list competing with it would be
the wrong thing to build.

Two details worth knowing:

- **The end of a file is a property, not an event.** `keep-open=yes` means mpv never
  unloads the file and so never emits `MPV_EVENT_END_FILE` for a normal finish; it pauses
  on the last frame and sets `eof-reached`. `MpvEngine` observes that and emits `endOfFile`
  on the rising edge only, because mpv clears it again on the seek an advance performs.
- **`pause` is a player property, not a per-file one.** keep-open pauses at the end of the
  outgoing file, so the incoming one arrives paused unless told otherwise — which is why
  the end-of-file handler calls `setPaused(false)` and a *manual* skip does not.

Ordering is ours rather than `QCollator`'s: numeric mode there depends on ICU and on the
runtime locale, and in this C.UTF-8 session it silently sorted `E10` ahead of `E2` with no
warning. `Playlist`'s comparison walks runs of digits as numbers and everything else
case-folded, so a folder of episodes cannot reorder itself because `LANG` changed.

**Theming is a QML singleton** (`Theme.qml`, `QT_QML_SINGLETON_TYPE` set *before*
`qt_add_qml_module` or it is registered as an ordinary type). Two schemes derived from one
`dark` property, so one assignment restyles both windows — which matters because the panel
is destroyed and rebuilt on every detach and would otherwise have to be told again. The
window pushes the saved value in at startup and writes it back on change. The video's
surround stays black in both schemes: it is letterbox area, not chrome.

**Parsing cost is I/O, not cues.** One `av_read_frame` loop collects every subtitle
stream in a single pass, so 65 tracks cost the same walk as one — but subtitle packets
are interleaved through the container, so the whole file has to be read. Measured:

| file | tracks / cues | parse |
|---|---|---|
| the 3 GB AV1 film on a 9p mount | 65 / 93 350 | **9.4 s** |
| `huge.mp4` + 200k-cue ASS sidecar, local ext4 | 1 / 200 000 | **0.8 s** |
| `subs.mkv`, local | 3 / 11 | 0.06 s |

Twice the cues in a ninth of the time, so optimising the text parsing would buy nothing.
The extractor reports progress by bytes read for the same reason. Because the pass is
single, no track finishes early — publishing tracks one at a time as they complete would
not shorten the wait, which is why the panel shows a percentage instead.

**So the parse is cached instead** (`SubtitleCache`). Nothing about the result changes
between opens, and the cost is all I/O, so the cues are written to
`~/.cache/subixa/subixa/subtitles/<sha1 of path>.cues` and read
back on the next open. Measured on the film:

| | cold | cached |
|---|---|---|
| 65 tracks / 93 350 cues | 15.7 s | **27 ms** headless, ~110 ms in the window |

The entry is 11.7 MB, written through `QSaveFile` — a half-written entry surviving a crash
would read back as a *short* track list, and the reader cannot tell "the file ends here"
from "the track ends here". The directory is pruned oldest-first past 512 MB.

Validation is a stamp per contributing file — path, size, mtime — for the media **and every
sidecar found next to it**, so dropping a `.srt` beside the film invalidates the entry
rather than being ignored. Not a content hash: hashing three gigabytes to decide whether to
re-read three gigabytes saves nothing. A truncated or garbled entry is a miss, not a
partial result, and the format carries a version that is bumped when the layout changes.
`SUBIXA_NO_SUBTITLE_CACHE=1` forces a real parse, which is how the timings above were taken.

The status line says which happened — `65 tracks, 93350 lines · cached` — and the log gives
the elapsed time either way, because a cache hit is otherwise indistinguishable from a
parse that was suspiciously quick.

`SubtitleManager` runs a `SubtitleExtractor` on its own `QThread` and republishes results
as bindable properties. Requests carry a monotonic id; bumping it makes an in-flight parse
abandon its demux loop, so opening a second file does not wait on the first.

Rows reach QML through one `SubtitleLineModel` per track. The model **shares** the track's
line buffer rather than copying it — `QVector` is implicitly shared and nothing mutates it
after `setLines()`, so handing 200k cues to a model is a refcount bump. That is what
replaced milestone 1's `QVariantList` snapshot, which cost ~600 ms of GUI thread per track
switch. `tracks` stays a `QVariantList`: it is per-track metadata only, rebuilt once a load.

Models are owned by the manager, **reused across loads and never deleted** — a QML binding
can still hold one for an instant after the track list changes, and an emptied model is
harmless where a dangling pointer is not. `model()` also pins ownership with
`QQmlEngine::setObjectOwnership(..., CppOwnership)`; without it the engine takes JavaScript
ownership of anything returned from an invokable and can collect it out from under C++.

`SubtitleFilterModel` is the search proxy. Filtering is a plain case-insensitive substring
match on the text role — timestamps are deliberately not searched, since someone typing
"12" means words. It also owns `rowAt(positionMs)`, which is what auto-follow calls: the
source model's binary search, mapped through the filter, returning -1 when the current cue
is filtered out (nothing to highlight, which is the wanted behaviour).

**Why the render API and not `--wid`:** with `--wid`, mpv draws into a separate native
surface *above* the scene graph, so QML cannot reliably paint over it. The whole feature
premise is QML chrome composited on the video, so `--wid` is not an option.
