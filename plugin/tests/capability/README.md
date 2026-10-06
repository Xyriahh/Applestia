# Rendering capability synchronization

Run `python3 tests/capability/run.py`. This tests the renderer's actual policy
header against the persistent protocol helper's real private libwayland globals.
It does not connect to any compositor or touch reviewer-owned GL tests.

Eligibility requires `layers.enabled`, an installed native layer hook, and
`applestia-drawers` permitted by the actual parsed include/exclude sets. Exclusion
wins; an empty include set admits all namespaces. Config/Lua updates and reloads
synchronize the protocol advertisement. Ordinary off/on and exclude/include
withdraw and create new global generations; all old bound objects remain inert.
For native drawers only, runtime `hg.layer("applestia-drawers", {exclude=false})`
reverses an earlier drawer exclusion. Other namespaces retain legacy semantics.

Shader/unsupported-transform/allocation failures and failed protocol restart are
terminal within the plugin instance. Config toggles never clear that latch.
Init/exit operate only the shapes protocol, never plugin unload or compositor
restart. On transition, tracked layer boxes are damaged and backdrop caches are
marked dirty without deleting already-captured foreground.

The first fatal reason is stored at:
`$XDG_RUNTIME_DIR/applestia-shapes-<Hyprland-instance-signature>.reason`.
Ordinary capability changes store an `eligibility:` reason in the same file;
fatal reasons are not overwritten by config changes. The file is mode 0600 on
creation, rejects symlinks/nonregular/foreign-owned/multiply-linked files, and
is fsynced. Diagnostic I/O failure never blocks safe withdrawal.

The default `SRenderModifData` has `enabled=true` but an **empty** `modifs` list;
it does not trigger the renderer's `enabled && !modifs.empty()` terminal check.
Nonempty active transform hints still cause conservative global downgrade.
An older nested binary without this diagnostic cannot retroactively report why
it withdrew; the `.reason` file appears on the next staged initialization/change.
