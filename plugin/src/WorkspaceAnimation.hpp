#pragma once

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/output/Monitor.hpp>

// Shared by CGlassLayerSurface::sampleAndRedirect and
// CGlassDecoration::wantsBackgroundResample: a workspace slide or fade moves
// the whole scene behind a still glass surface without changing the surface's
// own geometry, so neither path's own move/resize check would ever see it.
namespace WorkspaceAnimation {

// Scans every workspace of the monitor, not its active/special pointers: the
// special pointer is already cleared while the old workspace animates away.
[[nodiscard]] inline bool anyWorkspaceAnimating(PHLMONITOR monitor) {
    for (const auto& ws : State::workspaceState()->workspaces()) {
        if (ws->m_monitor != monitor)
            continue;
        if (ws->m_renderOffset->isBeingAnimated() || ws->m_alpha->isBeingAnimated())
            return true;
    }

    return false;
}

// A layer fading or sliding in or out changes what glass above it shows without
// any commit (the animation is Hyprland's), so glass resamples while one runs.
[[nodiscard]] inline bool anyLayerAnimating(PHLMONITOR monitor) {
    if (!monitor)
        return false;
    for (const auto& level : monitor->m_layerSurfaceLayers)
        for (const auto& ref : level) {
            const auto layer = ref.lock();
            if (layer && (layer->alpha()[Desktop::View::LS_ALPHA_FADE]->isBeingAnimated() || layer->positionAnimation()->isBeingAnimated() ||
                          layer->sizeAnimation()->isBeingAnimated()))
                return true;
        }
    return false;
}

} // namespace WorkspaceAnimation
