# Applestia glass shapes v1

The public wire contract is `protocols/applestia-glass-shapes-v1.xml`; the C++
renderer contract is `src/GlassShapes.hpp`. These are optional rendering hints,
not a substitute for normal surface content or visibility.

## State and annotations

- `begin` and `clear` both replace the **pending** list with an empty list.
  `add_shape` appends, up to 128 shapes; bad geometry or excess shapes are ignored,
  not protocol errors. Sizes/coordinates/depth/names are sanitized and corner
  radii use one proportional CSS scale.
- Hyprland's surface `precommit` snapshots pending hints. `stateCommit` associates
  that copy with the actual weak queued `SSurfaceState` identity. A required
  `SSurfaceState::updateFrom` hook selects that state's hints when it updates
  `m_current`, before the applied `commit` event publishes them. A synchronized
  subsurface stages the actual foreground-state update and publishes on its
  ancestor's commit event. This is identity-based, **not a FIFO**: acquire fences,
  FIFO and timers can queue A/B before A applies; rejection/drop expires weak
  associations without advancing unrelated metadata. Null-buffer commits bypass
  queue signals, so the hook recognizes `m_pending` directly and clears queued
  associations just as Hyprland clears queued foreground states. No-op states
  which skip `updateFrom` cannot publish newer hints. Requests after a snapshot
  cannot leak into it. The helper never issues `wl_surface.commit` itself.
- An active association with an empty current list returns an engaged, empty
  vector. No active association returns `nullopt`. Applied changes and association
  removal/recreation receive newer revisions; identical commits do not bump an
  active object's revision. With no object, generation conservatively uses the
  helper's latest revision; an unavailable protocol returns zero.
- Optional `set_clip(x,y,w,h)` and `set_opacity(opacity)` annotate the most recent
  **accepted add request**. Every rejected add invalidates that annotation target,
  even if older accepted shapes remain. `begin`/`clear` also invalidate it. Until
  a new add is accepted, both annotations are no-ops, so an ignored shape cannot
  accidentally modify its predecessor. Multiple annotations are last-wins.
- New shapes start unclipped at opacity one. Clip is a flat surface-local
  rectangle: it crops the original lens without inventing rounded lens edges.
  Nonpositive clip extents produce an empty clip. Opacity clamps to `[0,1]` and
  fades refraction/specular as well as tint. Neither request changes lens geometry.

## Activation and unloading

The private helper ABI is version 2; wire XML and the public shape layout are
unchanged. The helper is embedded, loaded with `RTLD_NODELETE`, and adopted through its
exported API on later loads. Its interfaces and resource handlers never point
into unloadable plugin code. Stop withdraws the registry global, sends manager
`finished`, detaches surface listeners, clears all plugin callbacks and makes
existing objects inert. A raced old-manager request still creates a valid inert
`new_id`. A later global requires newly bound objects.

`GlassShapes::active()` and `hg.features().shapes` become true only after the API
ABI, required state-identity hook, global creation and listener recovery succeed. Readiness is written last to
`$XDG_RUNTIME_DIR/applestia-shapes-<instance-signature>.ready` (`1\n`); it is advisory,
so clients must also verify protocol activation. Failed initialization and exit
clear active status/callbacks, withdraw the global and remove readiness. A plugin
unload guard also cleans up if `PLUGIN_EXIT` was skipped. Marker I/O failure does
not disable an otherwise active registry protocol; no successful marker is left
after a failed write.

## Offline tests

From the worktree root:

```sh
make test-shapes-helper
make src/shapes-helper/applestia-shapes-helper.so src/ShapesHelperBlob.o \
     src/GlassShapes.o src/PluginConfig.o src/main.o

cc -std=c11 -g -O1 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  $(pkg-config --cflags wayland-server) -Isrc/shapes-helper -Ibuild/shapes-helper \
  src/shapes-helper/test.c build/shapes-helper/applestia-glass-shapes-v1-protocol.c \
  -o /tmp/opencode/applestia-shapes-test-asan \
  $(pkg-config --libs wayland-server) -lm
ASAN_OPTIONS=detect_leaks=1 /tmp/opencode/applestia-shapes-test-asan
```

The harness uses a private libwayland display/socket pair, direct dispatch and
injected allocation/global-creation failures; it never connects to a compositor.
It covers pending/cached/applied timing, empty lists, sanitation/CSS radii,
annotation scopes after invalid/over-cap adds and list resets, revisions,
destruction orders, inert requests and protocol restart. These are protocol
semantics tests, not rendered-output or actual Hyprland commit-interception tests;
those still require the separate nested-compositor integration test.

Additional regression executables are included in `make test-shapes-helper`:
`shapes-commits-test` exercises the identical weak-state snapshot selector used
by the plugin for queued A/B, out-of-order updates/drops, rejection, synchronized
parent publication, null-buffer queue clearing and address reuse;
`shapes-loader-test` reproduces glibc's reused `/proc/self/fd/N` loader-path cache
collision after ItemHints loads, then verifies the distinct `/proc/<pid>/fd/N`
shapes path and retained/adoptable API. Both run entirely offline.

Initialization failure diagnostics persist independently of Hyprland logging in
`$XDG_RUNTIME_DIR/applestia-shapes-<instance-signature>.error`. `lastFailure()`
exposes the same bounded reason; `recordFailure(reason)` permits renderer-owned
downgrade diagnostics without changing its capability policy. Ordinary exit
preserves that diagnostic and successful re-init clears it. Exact loader errors,
ABI mismatch versions, missing/failed state identity hooks, global creation
failure and initialization exception stages are recorded. An absent symbol/hook
keeps the protocol unavailable rather than falling back to unreliable FIFO
matching. A previously loaded ABI-v1 NODELETE helper cannot be upgraded in-place;
use a new isolated compositor process for ABI-v2 integration.
