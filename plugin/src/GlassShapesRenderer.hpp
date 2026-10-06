#pragma once

#include "GlassShapes.hpp"
#include "PluginConfig.hpp"
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Box.hpp>

// Per-layer, reusable region scratch. Never samples the framebuffer currently
// being drawn into. No monitor-sized allocations or JFA for control shapes.
class CGlassShapesRenderer {
  public:
    void draw(const std::vector<GlassShapes::SGlassShape>& shapes, PHLMONITOR monitor,
              const CBox& layerBox, SP<Render::IFramebuffer> target, float alpha, const SResolveContext& context);
    // tileBuffer: SilhouetteField::tileBuffer() for this layer, or 0 for the whole quad.
    static void foreground(SP<Render::IFramebuffer> surface, SP<Render::IFramebuffer> target,
                           const CBox& rawBox, const CBox& transformedBox, GLuint tileBuffer = 0);
    [[nodiscard]] static float bezelLogical(const GlassShapes::SGlassShape& shape);
    // Render-thread only. Withdraws the protocol, never unloads the plugin.
    // A finished event/global removal restores the staged client's material.
    static void downgrade(const char* reason) noexcept;
    // Call once after PLUGIN_INIT helper initialization, then on ordinary
    // configuration/eligibility changes. Fatal failures remain latched.
    static void initializeCapability() noexcept;
    static void syncCapability() noexcept;

  private:
    SP<Render::IFramebuffer> m_snapshot, m_frost;
};
