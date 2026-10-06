# Read-only applied layer-shape diagnostic

Run offline regression: `python3 tests/shape-diagnostics/run.py`.

After the orchestrator builds **a separate** `hyprglass-pill-trace.so` and
authorizes its new nested instance, collect one snapshot with its explicitly
stored signature:

```sh
signature=<your nested Hyprland instance signature>
test -n "$signature" && hyprctl -i "$signature" -j hyprglass shapes
# Human-readable form:
test -n "$signature" && hyprctl -i "$signature" hyprglass shapes
```

This command uses the existing `hyprglass` dispatcher and enumerates live layer
roots from monitor layer lists. It reads **currently APPLIED**
`GlassShapes::forSurface(root)` and its generation on the command thread. It does
not report pending/cached client requests or claim to show the last rendered
frame. There are no GL queries, GUI polls, file/marker reads/writes, counter
resets, protocol requests, damage, capability changes or render hot-path writes.
No control labels, surface pixels or client-file contents are included.

The top-level `protocolActive` is the same API activation used by
`hg.features().shapes`. Per-layer `nativeActive` means active protocol + bound
association + mapped/configured renderer eligibility (installed layer hook,
enabled layers, actual include/exclude namespace sets, normal output transform,
not hints-only). It is explicitly **not a last-draw verdict** and does not claim
the compositor drew a particular shape this instant. Transient render modifiers
are not inferred from stale render-data state.

`binding: unbound` has `shapes: null`; `bound-empty` has `shapes: []`. Original
protocol insertion indices and depths are retained, not reordered by this
diagnostic. Runtime rendering sorts stably by ascending depth, preserving index
within a depth. Boxes and radii are surface-local logical coordinates; clip is
original x/y/width/height, NOT min/max or rounded clip geometry. `layerBoxGlobal`
and `monitorPosition` provide the logical mapping context; monitor scale and
transform are separate. `layer` and `surface` are opaque process-local object
addresses, not persistent IDs or GUI item names.

Output is capped at 16 layers and 128 shapes/layer, prioritizes bound layers,
and reports omitted counts. Metadata strings are capped at 256 bytes plus a
visible `...` suffix; JSON safely escapes controls and preserves valid UTF-8.
Nonfinite numeric values defensively appear as JSON null (real hints are already
sanitized by the protocol helper). Shared-model effective bezel/displacement
are supplementary logical distances; all original hint values remain present.

The offline test compiles the actual pure formatter from `Diagnostics.cpp` and
checks parser validity, Unicode/control escaping, nullable binding/clip states,
original order/fractional metadata, uint32 tint/uint64 generation preservation,
shared geometry, layer/shape/string caps, inactive capability, text output and
the existing dispatcher/read-only applied collection. Real surface enumeration
still requires the orchestrator's staged capture. Neither the command nor these
tests changes lens strength, bands, radii, frost or rendering behavior.
