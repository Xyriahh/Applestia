#include "../../src/GlassShapeLens.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace GlassShapeLens;
    for(float scale : {.25f,.5f,1.f,1.25f,2.f,4.f,8.f}) {
        for(const auto shape : {std::array<float,3>{430,190,16}, {160,48,24}, {120,32,16}, {16,16,8}}) {
            const std::array<float,4> radii{shape[2],shape[2],shape[2],shape[2]};
            const auto lens=geometry(shape[0],shape[1],radii,scale);
            const float oldBezel=std::clamp(.5f*std::min(shape[2],.5f*std::min(shape[0],shape[1])),4.f,16.f)*scale;
            assert(lens.displacementPx > .6f*oldBezel);
            assert(lens.displacementPx <= lens.bezelPx);
            assert(lens.bezelPx <= SHORT_SIDE_BEZEL_CAP*std::min(shape[0],shape[1])*scale+.0001f);
            assert(lens.bezelPx <= MAX_BEZEL_LOGICAL*scale);
            for(float frost : {0.f,.2f,1.f}) {
                const float padding=samplePaddingPx(lens,frost,scale);
                assert(padding >= lens.displacementPx+lens.chromaticPx+frostRadiusPx(frost,scale)+SAMPLE_GUARD_PX);
                assert(padding <= maximumSamplePaddingPx(scale));
            }
        }
    }
    // Regression (owner report 2026-10-05): the recorder card (408x209 r16, band 24)
    // held the Fullscreen split pill 23px below its top edge, so the card's band
    // ran right up to the pill and masked the pill's top-edge warp. A parent band
    // must end CHILD_CLEARANCE before a contained child, on that side only.
    for(float scale : {1.f,1.25f,1.6f,2.f}) {
        const auto card=geometry(408,209,{16,16,16,16},scale);
        assert(std::abs(card.bezelPx-24*scale)<1e-4f);
        const std::array<float,4> gaps{23.f, 16.f, 146.f, 236.f}; // top,right,bottom,left (applied trace)
        const auto sides=sideBezels(card.bezelPx,gaps,scale);
        assert(std::abs(sides[0]-(23-CHILD_CLEARANCE_LOGICAL)*scale)<1e-4f);            // top: clear of the pill
        assert(sides[0] + CHILD_CLEARANCE_LOGICAL*scale <= 23*scale+1e-4f);
        assert(std::abs(sides[2]-card.bezelPx)<1e-4f && std::abs(sides[3]-card.bezelPx)<1e-4f); // far sides keep full strength
        const auto none=sideBezels(card.bezelPx,{1e9f,1e9f,1e9f,1e9f},scale);
        for(float b : none) assert(b==card.bezelPx);                                       // no children: unchanged
        const auto tight=sideBezels(card.bezelPx,{2.f,1e9f,1e9f,1e9f},scale);
        assert(std::abs(tight[0]-MIN_SIDE_BEZEL_LOGICAL*scale)<1e-4f);                     // never collapses
    }
    std::cout<<"PASS child clearance: parent band ends before contained child glass (recorder pill top), other sides full strength\n";
    std::cout<<"PASS production shared lens geometry, stronger radius16/24 bend, short-side excursion cap, scaled snapshot/damage padding\n";
}
