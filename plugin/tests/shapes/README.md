# Native shapes offline tests and MVP downgrade

## Split-pill missing-edge investigation

The accepted owner-lens strength/bands/radii/frost and all runtime shader/renderer
code are **unchanged** by this investigation. The symptom is not declared fixed.
The owner feedback image was inspected, and read-only queries explicitly targeted
the instance recorded in `results/nested-instance`: WAYLAND-1 is scale 1,
transform 0, with an active full-window `applestia-drawers` layer. Client review
confirmed symmetric top/bottom radii, unclipped header ancestry, nonoverlapping
half boxes, card depth 0 / halves depth 1, and no visible native menu/state-layer
material overdraw. These observations do not expose the applied server hint list.

New production-shader software tests use a poisoned 640x384 reusable scratch FBO,
copy only each padded initialized ROI, and verify all four sides against explicit
rounded SDF, finite-gradient normal, circular-profile and gradient-address oracles.
They cover three fractional scales, integer/fractional positions, round pills,
`[20,4,4,20]` / `[4,20,20,4]` split halves, four independent checker frequencies
(3/7/13/23px) and actual parent-card -> left -> right snapshot/draw ordering.
Measured scale-1 integer-probe inward pulls on the coordinate-gradient backdrop:

| Geometry | TOP | BOTTOM | LEFT | RIGHT |
|---|---:|---:|---:|---:|
| Round 120x40 pill | 7.51 | 8.76 | 8.01 | 8.01 |
| Left asymmetric 120x40 half | 7.51 | 7.51 | 6.01 | 6.01 |
| Right asymmetric 40x40 half | 7.50 | 7.50 | 6.01 | 6.01 |

Units are framebuffer px, quantized by UNORM8 coordinate gradients (~1.25px
vertical / ~2px horizontal per LSB). Every tested edge has nonzero finite normal,
nonzero supported circular profile and a nonzero inward sample displacement.
Different top/bottom round-pill values above are gradient quantization, not a
changed physical rule: scalar/profile oracles are checked independently. The
right sibling leaves every pixel of the separated left half byte-exact despite
overlapping sample padding. A top clip suppresses exactly the clipped pixels,
without changing the original SDF or inventing a new rounded cut edge.

An optical-stack counterexample is also reproduced: a nonzero ~7px child top
sample bend produces only ~1.25px visible coordinate-gradient change relative
to its already-lensed parent, at parent-bezel inset 2px. A circular parent lens
remaps/folds its content; a subsequent inward sample can revisit nearly the same
source feature. A periodic checker can additionally conceal displacement within
a uniform tile or at matching phase. This establishes a real content/stack
interaction, **not proof that it caused the real Fullscreen screenshot**.

No renderer direction/sign/scissor/ROI failure has been established. Before an
appearance fix, obtain one bounded trace of the actual APPLIED list for the
stable recorder view: layer box and monitor scale, generation, original insertion
index, depth, local x/y/w/h, all four radii, opacity/tint/preset, original clip
rectangle, and later overlapping shapes. In particular identify recorder parent
and both halves to distinguish sampled-parent cancellation from bad metadata.
The running candidate has no read-only shapes-list command; do not invent one
or guess private compositor symbols. The orchestrator/client coder can arrange
an approved one-frame trace or a separately built diagnostic candidate. No
mapped binary replacement, live eval or nested lifecycle action was performed.

## Owner-visible shared lens revision (alternate candidate)

The owner rejected the previous preview's control lens appearance despite correct
native ordering and labels. No live installation was approved. The old suggested
original design model gave a radius-16 card a maximum 4.8 logical-pixel bend, far below the
outer panel's roughly 22px excursion. Increasing painted specular/tint would not
fix that geometric mismatch.

`src/GlassShapeLens.hpp` now defines ONE preset-independent physical material:

```
shortSide = min(width, height)
effectiveRadius = clamp(min(radii), 0, shortSide/2)
bezel = min(clamp(1.5*effectiveRadius, 12, 24), .35*shortSide)
maxDisplacement = .9*bezel
chromatic = .25 logical px; lip = 1.5 logical px
```

All these logical distances scale together with the monitor. The circular height
profile/filtered endpoints and rounded-rect normal model are retained. The .35
short-side cap leaves the centre flat, including normal small pills; the maximum
excursion stays below its supported bezel, never an unbounded distant pull. This
is an explicit owner-feedback-driven deviation from the original design's suggested `.6*b`,
not a per-control or per-preset tweak. Rim paint strength and tint placement are
unchanged. Frost now ramps from zero at the physical edge to the preset's amount
at the interior via `smoothstep(0, bezel, inwardDistance)`, so edge refraction samples
the sharp CURRENT parent target rather than an extra frosted control texture.
It does not bypass the parent to fetch a different desktop or warp any labels.

Snapshot padding uses the same production geometry's maximum displacement plus
chromatic excursion, actual frost kernel radius and a two-framebuffer-pixel guard.
Old/new shape damage uses the same geometry with the maximum frost footprint;
native render-pass sample bounds use its maximum supported geometry, including
large/fractional monitor scales. No old `.6*b`/20px padding rule remains in those
paths. Fused low-frost handling, prefix order, clipping and commit/downgrade
semantics are preserved. A wider bezel and larger snapshots may increase GPU
cost: previous candidate performance results do not validate this revision.

Offscreen measured checkerboard/diagonal-wall regression at scale 1, 1.5px inside
the top boundary (UNORM8 gradient measurement, roughly +/-1px precision):

| Shared geometry | Old measured pull | Revised measured pull |
|---|---:|---:|
| 430x190 card, radius 16 | 2.50px | 16.26px |
| 160x48 pill, radius 24 | 5.00px | 10.01px |
| 120x32 pill, radius 16 | 2.50px | 6.25px |

These are sampled BACKDROP displacements, not painted-rim brightness. The test
also checks exact unwarped centres, unchanged pixels outside the shape/rounded
corner, per-pixel excursion bounds, and hundreds/thousands of visibly different
checker pixels. Production-header CPU tests cover formula/bounds at scales .25
through 8. Optimized vs unoptimized shared-model shader tests remain byte-exact;
fused vs separate frost remains within one UNORM8 LSB with exact coverage/alpha.

Nested-only `HYPRGLASS_SHAPES_DEBUG=4` renders the sharp current target with flat
optics; `=5` renders the SAME sharp target through the fixed physical lens. Both
disable frost/specular/shadow identically but keep shape tint/foreground order.
Set the renderer environment before the orchestrator starts its alternate
candidate; neither mode changes presets or the client. Existing `1` SDF, `2`
normals and `3` bezel debug views remain supported. No agent build, replacement
of mapped `hyprglass.so`, compositor restart or runtime file change is required
by the offline tests. The code is for the orchestrator's alternate-file build
`hyprglass-owner-lens.so` and owner visual review, not live deployment.

Run from any directory:

```sh
python3 tests/shapes/run.py
```

Requires Python 3, g++, glslangValidator, pkg-config, EGL/GLES development
libraries and Mesa's EGL vendor file. Uses only software llvmpipe on a private
surfaceless EGL context, not a running or nested compositor. Temporary extracted
shaders and the test binary live under `/tmp/opencode` and are removed afterward.

CPU checks cover circular-profile endpoints/monotonicity, stable depth ordering,
scale-dependent sample padding and outward fractional damage bounds. The C++
EGL harness executes the actual embedded shaders and checks current-parent
sampling, asymmetric radii, normals, flat clips, whole-effect opacity, bounded
frost with poisoned unused scratch capacity, premultiplied foreground and the
native glass-only vs legacy one-pass source-over algebra. This is not a complete
Hyprland coordinate/damage integration test or a performance benchmark.

## Measured-workload optimization regression

The unoptimized nested fixture's extra shapes stage measured 1557.54 us/frame
at 1600x1000 (7 shapes, including 1080x660 parent and 430x190 card). This exceeded
the 1 ms incremental budget; no acceptance/performance claim follows from these
offline tests. The orchestrator must remeasure the staged build and real shell.

The optimized lens collapses three RGB samples to one sharp/soft sample outside
the filtered bezel's exact support (`distance >= bezel + 0.375`). A deep interior
path also skips normals/circular profiles/specular after 32 lip e-folds: omitted
reflection is below 1.3e-14, below FP16 representability. Full circular geometry,
chromatic sampling, lip and shadow remain unchanged at visible edges. Debug
views always retain their full semantics. Clear material doesn't allocate frost
scratch or fetch soft texture; fully frosted material doesn't fetch sharp.

For frost radius <=1 framebuffer pixel, the [1,2,1] 3x3 binomial kernel is computed
with four bilinear taps instead of nine. This is the same ideal filter at texel
centers; actual GL_LINEAR fraction quantization can differ by one UNORM8 LSB in
the frost-only result. Larger radii keep the original nine taps, pixel-exact.
No control geometry, sharp edge texture or frost render target is downscaled.
Each shape still snapshots the current target before drawing, so parents/tint
and committed hint timing remain unchanged; no unsafe same-depth batching.

The durable pre-optimization shader oracles are tested against all pixels in 96
large-shape cases (four scales, three frost amounts, two opacities, all debug
views). The optimized lens is byte-exact in these tests with identical soft
input. Separate textured frost tests bound GL interpolation differences to one
LSB and require exact output for the unchanged radius>1 path. Existing clipped
pixel, foreground, parent-sampling and real CStateGuard tests remain in place.

At scale 1 with frost .2, the dominant interior's texture fetch count drops from
15 (nine frost + six lens) to six (four frost + two lens), plus substantially less
lens ALU. This predicts a gain, not a measured GPU-time guarantee. At scales or
frost strengths that push radius above one, only the lens optimization applies.

### Low-frost pass fusion (real-shell follow-up)

The first optimization still measured 1452.42 us/frame incremental shapes on the
staged NVIDIA real shell (5336 shapes / 197 frames, ~27/frame; ~.75 sampled Mpix).
It did **not** meet the ~1 ms budget. The follow-up removes the separate frost
draw for `0 < frost <= .25` and radius <=1 framebuffer pixel, evaluating equivalent
soft samples directly in the analytical lens instead. No `m_frost` allocation,
FBO draw, frost shader selection, temporary viewport, or blend toggle is required
for these controls. Sharp current-target snapshots are still taken per shape in
stable depth/insertion order; there is no cross-frame cache or control downscale.

For fractional refracted positions, four taps simply at `position +/- radius/2`
would NOT reproduce the preblurred material. The fused shader combines the
centered binomial kernel with the bilinear sampling phase into four 1-D weights,
groups adjacent weights into two hardware-linear reads per axis, and evaluates
the equivalent 2-D kernel in four fetches. The dominant interior costs five reads
(sharp + four frost) instead of six across two passes; the narrow chromatic edge
can cost fifteen instead of ten. The expected win is primarily eliminating ~27
small-control driver/FBO draws per animating frame, not uniformly lower edge ALU.
Wider kernels/stronger frost retain the separate pass. Material strength, fixed
circular geometry, tints, opacity, clipping and foreground order are unchanged.

Software regressions compare fused output against the actual separate-pass
material on a high-frequency colored texture, including fractional translations,
three scales, low-frost amounts, small asymmetric pills, cards, flat cuts and
opacity. All channels differ by at most one UNORM8 LSB; alpha is exact. An unused
soft texture is deliberately magenta poison to catch stale-FBO sampling. Existing
96-case byte-exact nonfused lens tests and CStateGuard tests still run. Staged GPU
timing through the orchestrator remains necessary: no <1 ms claim is made.

The separate state-cache EGL regression compiles the **actual CStateGuard**
extracted from GlassShapesRenderer.cpp against models of Hyprland's cached
useShader/scissor/viewport/cap APIs. It repeats shape and foreground stages with
a different incoming program, enabled/disabled nontrivial scissor boxes, incoming
raw box changes and exception-path raw GL changes. It checks cache hits/rebinds,
actual GL state and clipped draw pixels after restoration. Program selection is
intentionally not restored: Hyprland exposes no cache-aware restore by program
ID, so the last selected shader and its private tracker must remain consistent;
the next renderer operation selects its shader through useShader(). Scissor
restoration updates the tracked box through the API and also asserts the actual
saved box, including when a stale cache would otherwise skip that GL change.

## Conservative global downgrade

For MVP, a native-bound layer on a rotated/flipped output or with active render
modifiers withdraws **the global shapes capability for all monitors**, rather
than render controls incorrectly or leave native-capable QML without its bodies.
Shared/native shader compilation failures, region/capture/panel-sample/blur
allocation failures and exceptions in native preparation/drawing also withdraw
immediately. No automatic retry/re-advertisement occurs in that plugin lifetime.
The warning is logged once; `hg.features().shapes` becomes false and the advisory
readiness marker is removed. Recovery requires a later plugin initialization;
this implementation never unloads/reloads the plugin or restarts any compositor.

Withdrawal calls `GlassShapes::exit()` from rendering, **not from a protocol or
commit callback**. That existing shutdown clears helper callbacks before stop,
sends manager `finished`, makes extant objects inert, removes the registry global
and unlinks the protocol listeners. The persistent helper retains old interfaces
and tolerates raced binds/requests. Wayland events are queued, not synchronously
dispatched by exit. Layer-owned listeners and shader/FBO objects stay alive:
there is no plugin teardown. Each native frame owns a copied shape vector, so
protocol listener teardown cannot invalidate an in-flight shape iteration.

Before redirect, failure rebinds the original framebuffer and permits normal
queued surface rendering. After redirect, a scratch failure keeps the compiled
foreground shader available and composites captured labels/icons/fills once.
Partially completed lenses may remain in that transitional frame. Other queued
native stages skip lenses after withdrawal but still draw captured foreground.
Clients restore legacy bodies after receiving `finished`/global removal and
committing their fallback frame; one transitional body-less frame is possible,
but the capability cannot remain advertised with persistently missing controls.
Old native boxes are damaged on withdrawal to remove trails. Layer sample
prefixes are marked dirty without FPS throttling, but their validity and any
captured foreground are retained until the current render pass completes.

Native bindings (including empty lists) always suppress Hyprland background blur
while capturing their foreground, irrespective of `layersManageBlur`. Legacy
surfaces still obey that option. Native panel coverage still requires an explicit
panel blur region; no alpha-only panel is inferred for standalone controls without
one. Broad panel regions can still include isolated text in the old silhouette.

Nested-only debug environment: `HYPRGLASS_SHAPES_DEBUG=1` SDF, `2` normals, `3`
bezel. Set before starting the orchestrator's nested compositor. No debug preset
changes the lens geometry.
