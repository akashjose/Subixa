# Traps

> Every one of these has already cost time. Read the relevant one before changing that area.
> Split out of `CLAUDE.md`, which is the entry point and links here.

## The list

1. **`vo=libmpv` must be set before `mpv_initialize`.** Without it mpv picks a native VO
   (`wlshm` under WSLg), creates **its own Wayland surface**, and never touches the FBO.
   Video appears *outside* the app window and steals input. Looks like a compositing bug;
   it is not.
2. **The render context does not exist until the item first renders.** It is created in
   `createFramebufferObject()`, which runs *after* QML's `Component.onCompleted`. Calling
   `loadFile()` earlier gives `No render context set` → `Video: no video`, with no picture
   and no obvious error. Early loads are queued and flushed in `onRenderContextReady()`.
   **Any new mpv command that must run before first paint needs the same treatment.**
3. **`target_include_directories(... PRIVATE src)` is mandatory.** qmltyperegistrar emits
   `#if __has_include(<MpvEngine.h>)`; without `src/` on the include path that guard is
   silently false and the build fails with `QQuickItem was not declared` — nowhere near
   the real cause.
4. **`QOpenGLFramebufferObject` is in QtOpenGL, not QtGui** (Qt 6 moved it;
   `QOpenGLContext` stayed in QtGui).

## Subtitle extraction

5. **Every ffmpeg text subtitle decoder emits ASS, and the field layout is not the one in
   an `.ass` file.** SRT, ASS and `mov_text` all come back as
   `ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text` — the text starts
   after the **8th** comma, and the first field is a read order counter, *not* a
   timestamp. Timings come from the packet, not the payload. Splitting as if it were a
   file's `Dialogue:` line silently eats the first words of every cue.
6. **Rebase against the container start time per track, not wholesale.** mpv shifts
   playback to start at zero (`--rebase-start-time`, on by default), so an MPEG-TS that
   starts an hour in needs the same shift or every seek lands 3600 s out. But muxers do
   produce files whose video starts at 1 h while the subtitle stream still starts at 0
   (`testdata/shifted.mp4`); subtracting there flattens every cue onto `00:00:00`. Only
   shift a track whose own first cue is at or past the offset.
7. **ffmpeg does not decode character entities.** Its SRT/WebVTT decoders convert `<i>`
   into ASS override tags but leave `&amp;`, `&#39;` and friends literal — they would show
   up raw in the browser *and* break search. Decoding is on our side.
8. **Bitmap vs text is a codec property, not a name list.** `avcodec_descriptor_get()`
   exposes `AV_CODEC_PROP_TEXT_SUB` / `AV_CODEC_PROP_BITMAP_SUB`; use those rather than
   matching codec names, and the classification stays right as codecs are added.
   (ffmpeg cannot transcode text to bitmap, so there is no way to *generate* a PGS/VOBSUB
   fixture locally — that path is verified against the codec table, not a file.)

## Rendering — both are software-rasterizer bugs, both fixed conditionally

9. **Mesa's software rasterizers render 10-bit video wrong, and say nothing about it.**
   A `yuv420p10` file comes out either fully black (synthetic 10-bit H.264) or heavily
   vertically striped (a real AV1 film), while a byte-for-byte 8-bit twin of the same clip
   renders perfectly in the same session. mpv logs no error at `-v`: it reports
   `Texture for plane 0/1/2` and `Using FBO format rgba16f` identically for both depths,
   so the log will not tell you. `MpvRenderer::usingSoftwareRasterizer()` checks
   `GL_RENDERER` for llvmpipe/softpipe/swrast/"Software Rasterizer" and, when it matches,
   applies `vf=format=yuv420p` — a no-op for 8-bit content, a CPU conversion for 10-bit.
   It is deliberately conditional so a real GPU keeps the native path.

   The workaround must be applied **before** the queued file starts playing, which is why
   it is queued ahead of `onRenderContextReady()` — see trap 2.

   Do not misread this as slow decode. AV1 1920x800 decodes at **25× realtime** here
   (20 cores, measured with `ffmpeg -f null -`); software *decode* is not the bottleneck,
   software *rendering* is.

10. **mpv's render must be *finished*, not just issued, before Qt samples the FBO.**
    Symptom: above roughly **2048 px of video-pane width** the picture goes black, or
    streaked, or shows a fine mesh of unwritten pixels. Below it, everything looks fine.
    A conformant driver tracks the render-to-texture dependency itself; llvmpipe does not,
    so the scene graph composites a partially rasterised surface.

    How that was established, because every cheaper explanation was wrong:

    - Not resize handling — a window that is 2400x1300 *from launch* fails identically.
    - Not a driver limit — `GL_MAX_TEXTURE_SIZE`, `GL_MAX_RENDERBUFFER_SIZE` and
      `GL_MAX_VIEWPORT_DIMS` all report **16384**. A 2060-wide FBO is legal.
    - No GL error is raised, at any point, draining the queue every frame.
    - Not two contexts: the renderer and the scene graph share one
      (`OpenGLContextResource` compares equal), so this is not the shared-context
      flush rule.
    - **Not mpv.** Dumping the FBO with `toImage()` at the failing size yields a
      *perfect* frame. The readback is itself the missing synchronisation, which is
      why the dump looks right while the screen does not.
    - Not sampling geometry — filling the FBO with four `glScissor`+`glClear`
      quadrants renders sharp, correctly placed quadrants at the failing size. (A
      uniform fill proves nothing here: it looks identical under any scaling error.)

    `glFinish()` after `mpv_render_context_render()` fixes it. `glFlush()` is **not**
    enough — it submits the work without waiting, which visibly improves the frame but
    leaves a fine grid of unwritten pixels. Gated on `usingSoftwareRasterizer()` so a
    real GPU is not stalled every frame for a bug it does not have. `SUBIXA_NO_SYNC=1`
    disables the call, which is how to A/B it.

    **`glFinish()` is necessary but NOT sufficient — the fix is incomplete.** Adding
    fullscreen exposed this immediately, and it is not a fullscreen bug: it tracks FBO
    size, in a window as much as out of one. Measured on `testclip.mp4`, software
    rasterizer, sync on unless stated:

    | video pane | mode | result |
    |---|---|---|
    | 2216x1290 | windowed | clean |
    | 2220x1345 | fullscreen | fine mesh of unwritten pixels |
    | 2560x1345 | windowed | **fully black** |
    | 2560x1345 | fullscreen | fine mesh |
    | 2560x1345 | fullscreen, `SUBIXA_NO_SYNC=1` | fully black |

    So `glFinish()` still buys a great deal — without it a large pane is black rather
    than meshed — but somewhere above roughly 2.9 megapixels of FBO it stops being
    enough, and the symptom becomes exactly what `glFlush()` alone used to produce.
    Note how close the clean and broken cases are (2216x1290 versus 2220x1345): this is
    a threshold in total FBO area, not a width cliff, so do not trust a single
    resolution to tell you the path is healthy.

    **Fixed by capping the FBO** (`FboCap.h`), which is what makes fullscreen usable on
    the software path: a 2560x1388 pane now renders into 1280x720 and Qt scales it, so
    the corrupt regime stops being reachable rather than being pushed slightly further
    out. Verified at the size that produced the mesh. The price is a softer picture when
    the window exceeds the video, which is the trade every player makes.

    Two terms, both needed. The *native* term stops a 720p file being rendered into a
    2560 px surface for no gain. The *area* term is what saves 4K, where the native size
    is above the pane and the native term would never engage at all. Both are gated on
    `usingSoftwareRasterizer()`: libass draws subtitles into this same FBO, so capping
    renders subtitle text at video resolution and upscales it — the last thing to blur in
    a player built around subtitles, and pointless on a GPU that has no need of it.
    `SUBIXA_NO_FBO_CAP=1` disables the cap, the way `SUBIXA_NO_SYNC=1` disables the glFinish.

    It also cuts CPU, though **not by as much as this file used to claim**. Measured on a
    3-minute 720p clip, fullscreen, steady state: **1435% CPU uncapped, 826% capped** —
    about 1.7x, not the ~6x guessed at before the cap existed. The remainder is Qt
    compositing a 2560x1440 scene through llvmpipe plus software decode, neither of which
    the cap touches.

    **Is this llvmpipe generally, or WSLg?** Unresolved, and worth knowing before spending
    much on the cap. It is *not* the Wayland surface specifically: switching to XWayland
    (`QT_QPA_PLATFORM=xcb`) keeps the corruption and only changes its severity — a fine
    mesh where the Wayland path paints solid black. But both still run through WSLg, so
    that does not exonerate it. The Xvfb attempt to remove WSLg entirely produced no
    evidence either way for the reason recorded in the Tests section: the window never
    repaints there, so every size looks broken. Settling it needs llvmpipe on a Linux
    desktop that is not WSLg — a VM with a compositor, or real hardware with
    `LIBGL_ALWAYS_SOFTWARE=1`.

    Until that lands, **fullscreen and very large windows show a corrupt picture on the
    software rasterizer** — so run with `GALLIUM_DRIVER=d3d12`, where the same 2560 px pane
    is clean. "A real GPU is unaffected" is no longer an assumption: it was measured on the
    D3D12 path, at the exact size that paints black under llvmpipe.

    That also lowers the urgency of the cap. It is still worth doing — it is the only fix
    for anyone stuck on software rendering, and it cuts CPU — but it is a fallback-path fix
    rather than a blocker, and it must stay gated on `usingSoftwareRasterizer()` for a
    reason beyond safety: libass draws subtitles into this same FBO, so capping it renders
    subtitle text at video resolution and upscales it. In a player whose whole point is
    subtitles, that is the last thing to blur on a GPU that has no need of the cap.

## Browser UI

11. **A delegate cannot take `required property string text`.** `ItemDelegate` already has
    a `text` property, and the role of the same name collides with it. Take
    `required property var model` and read `model.text` instead.
12. **Only the browser decodes entities, so mpv's own overlay disagrees with the panel.**
    libass renders `&amp;` literally over the video while the same cue reads `&` in the
    list. Both are behaving as designed (trap 7) — it is not a parsing regression.

13. **A `TabBar` writes its own resets back into whatever you sync it with.** The panel's
    tab index lives in the caller's `ui` object because the panel is destroyed and rebuilt
    on every detach — so the outgoing copy's last word is what the incoming one restores
    from. Two things make that dangerous, and both bit:

    - The tabs come from a `Repeater` over the track list, so the bar is **empty** when
      `Component.onCompleted` runs. Restoring there does nothing, and then a `Container`
      adopts index 0 the moment it receives its first item — which the `onCurrentIndexChanged`
      handler dutifully stored, losing the reader's tab on a 66-tab film.
    - A bar being torn down drops its tabs first, resetting `currentIndex` on the way out.

    Fixed by gating **both directions** on the bar being finished:
    `count > 0 && count === manager.tracks.length`. A bar still filling up, or emptying,
    has no opinion about which track the reader chose. `tst_qmlpanel` covers it, and found
    it in the first place — it is invisible in a screenshot unless you happen to notice
    which tab is lit.

    A second-order version of the same thing: `TabBar` scrolls its strip to `currentIndex`
    itself, but only once the strip has been laid out, so a restore lands with the strip at
    the start and tab 65 off the end. The panel positions it explicitly after a 50 ms
    timer — `Qt.callLater` is too early, `contentWidth` is not final yet.

14. **`{\p1}` is not text, it is a shape** — an extraction trap, numbered late because the
    numbers are identities rather than an order. ASS switches to drawing mode with `\p1`
    and back with `\p0`, and what sits between them is a path: `m 0 0 l 100 0 b 20 30`.
    Flattened like any other payload it becomes a row of the browser reading exactly that,
    and it is searchable, so a track full of signs fills the list with coordinates. Both
    the plain and the styled paths drop drawing mode now.

    This is also the case the cue cache's version bump exists for: the fix changed the
    *extractor's output*, so every entry written before it holds coordinates as dialogue and
    would have gone on serving them. `SubtitleCache::kFormatVersion` went to 2 in the same
    commit — the remedy is easy and easy to forget.

## QML — every one of these cost a build-and-look cycle

15. **`font.families` does not exist on `Text` in Qt 6.9.** Only `font.family`. Assigning a
    fallback list — the natural way to say "Inter, else Ubuntu Sans, else DejaVu" — fails at
    load with `Cannot assign to non-existent property "families"`, pointing at the property
    rather than at the missing feature. `Theme.pickFont()` walks a preference list against
    `Qt.fontFamilies()` once instead, and every call site uses `font.family: Theme.type.sans`.

16. **`AbstractButton.icon` is FINAL, so a control cannot declare its own `icon`.** Anything
    built on `Button`, `MenuItem` or `ItemDelegate` fails with `Cannot override FINAL
    property`. The components here take `iconName`. The rename is worth doing carefully: a
    blanket `s/icon/iconName/` also rewrites `iconSize` into `iconNameSize`, which then
    fails one layer further down.

17. **A `default property alias` swallows the component's own children.** Declaring
    `default property alias content: inner.data` means *anything written as an ordinary
    child of that file's root* is routed into the alias — including the component's own
    internal layout, which then contains the item it is being assigned into. The error
    surfaces a long way from the cause (it came out as a bogus complaint about `font`).
    `SectionCard` and `FormRow` assign their internal structure through `children: [ ... ]`
    for exactly this reason.

18. **`Layout.fillWidth` on a *nested layout* does not stretch it.** A
    `ColumnLayout { Layout.fillWidth: true }` inside a `RowLayout` stays at its implicit
    width, so everything after it tracks the length of its own content instead of forming a
    column. Measured, not assumed: the same structure with an explicit
    `Item { Layout.fillWidth: true }` spacer aligns to the pixel. The hotkey table in
    Settings is the case that exposed it — the key caps and buttons drifted per row.

19. **`prefs: prefs` binds to itself, and says nothing.** This is trap 13's shadowing rule
    generalised: a child declaring `required property var prefs` and given `prefs: prefs`
    resolves the right-hand side to its *own* property. With a `var` there is no error at
    all — it simply holds `undefined`, every control reading through it falls back to its
    declared default, and the settings window comes up showing zeroes for values that are
    not zero. `Main.qml` therefore exposes each service as a `readonly property` on the root
    (`root.prefsStore`, `root.linesModel`, `root.queue`, …) and every binding that hands one
    to a child is qualified. That makes the trap unrepresentable rather than something to
    remember — which matters, because it had already been paid for once with `linesModel`.

20. **A Qt 6.9 `Menu` defaults to a *native* popup, and there is none under WSLg.**
    `popupType` arrived in 6.8, and a `Menu` defaults to `Popup.Native`: Qt asks the
    platform for a real menu and, where the platform has one, ignores `background`,
    `contentItem` and every delegate you wrote. WSLg has no native menu implementation, so
    `popup()` returned successfully and drew **nothing** — no warning, no error, no menu.
    Every button in the app that opens a menu was silently dead, which is most of them:
    the track picker, the overflow, audio, subtitles, speed, the queue, the panel's More
    menu, and right-click on a row. `AppMenu` sets `popupType: T.Popup.Item`. Note this is
    invisible to `tst_qmlpanel` (it asserts model state, not that a popup appeared) *and*
    to a screenshot, because a native popup would be a separate window anyway.

21. **`ScrollBar` and `ScrollIndicator` are different types.** `T.ScrollIndicator.vertical:
    AppScrollBar {}` fails with a type mismatch that names both, which is clear enough, but
    the attached property to use inside a `ListView` in a popup is `T.ScrollBar.vertical`.

Known-harmless: Qt's fallback `FileDialog` will not prefill the name field for a
`SaveFile`, whatever `selectedFile`/`currentFile` are set to and whenever they are set —
the file being named does not exist yet, so nothing in the listing matches it. The export
dialog therefore opens in the right folder with the right filter and appends `.srt` on its
own, but the name is typed. There is no xdg-desktop-portal under WSLg, so the native
dialog that would honour it is not in play here.

Known-harmless: CMake warns `QTP0004` about qmldir files for `qml/`. Cosmetic.
mpv logs `Suspected software renderer`, EGL/DRM/Vulkan probe failures,
`Cannot load libcuda.so.1` and `Failed to open VDPAU backend` — all expected under WSL.
The VDPAU line is new only in the sense that the player now asks for hardware decoding at
all; it is mpv ruling out a backend, not a failure.

## Graphics driver

22. **Mesa's D3D12 driver renders every glyph in the wrong colour, and only
    glyphs.** On this machine Qt Quick's text materials come out wrong while
    everything else in the same frame is exact: `#aab2c2` renders as pure green,
    `#e8eaf0` as yellow, and the browser's 11px `#7e93b5` timestamp loses so much
    luminance it reads as black and disappears entirely.

    It is not ours, and it is not the capture path. Established by elimination,
    all of it measured rather than eyeballed:

    - A `Rectangle` filled `#aab2c2` measures exactly `(170,178,194)` beside a
      `Text` coloured `#aab2c2` that measures `(0,255,1)`. No transport codec
      distinguishes a glyph from a quad.
    - RGB images, **single-channel greyscale images**, and `QtQuick.Shapes`
      geometry are all exact — so it is not 8-bit texture sampling either.
    - It survives both text render types, `layer.enabled`, an opacity node,
      subpixel antialiasing disabled at the fontconfig level, and an opaque
      surface.
    - It is **independent of the backdrop**: identical over an opaque white
      rectangle and over the dark window background.
    - It reproduces in Qt's own `qml` binary with no application code, and on
      **Mesa 26.1.5** as well as 25.2.8.
    - The same build on llvmpipe is correct.

    The mapping is not a channel permutation — `#ff0000` renders black,
    `#000000` renders correctly black, `#ffffff` renders black — which is
    consistent with the fill colour being multiplied by a garbage per-draw
    value: black is "correct" only because zero times anything is zero.

    **Worked around, not fixed.** `PaintedText` draws with QPainter into an
    ordinary texture, which is measured correct to the byte on the same driver,
    and `AppText` selects between it and the native item at runtime from what
    `GL_RENDERER` reported. Native stays the default: it shares one glyph atlas
    where the painted path allocates a texture per item. **Every piece of text in
    the app must go through `AppText`.**

    Chosen by driver name rather than probed by readback deliberately: trap 10
    already records this environment returning a *perfect* frame from `toImage()`
    while the screen was wrong, so a readback cannot be trusted to describe what
    is displayed. Settings -> Interface overrides either way.

23. **XML forbids a double hyphen inside a comment.** `icons/subixa.svg` cannot
    use the em dash the rest of the tree writes as two hyphens. Breaking it does
    not fail loudly: ImageMagick rendered the entire tile black and reported
    nothing until asked directly, which sent the first diagnosis after a
    gradient that was working fine.
