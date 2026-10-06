#include "../../src/DiagnosticsShapes.hpp"
#include <cassert>
#include <iostream>
#include <limits>

int main(int argc, char** argv) {
    using namespace Diagnostics;
    std::vector<SAppliedLayerShapes> layers;
    SAppliedLayerShapes legacy{.layer="0x10", .nameSpace="legacy", .monitor="WAYLAND-1", .surface="0x20", .generation=7, .applied=std::nullopt};
    layers.push_back(legacy);
    auto empty=legacy;
    empty.nameSpace="empty";empty.generation=8;empty.mapped=true;empty.renderingEligible=true;
    empty.applied.emplace();layers.push_back(empty);
    auto native=empty;
    native.nameSpace="applestia-drawers\"\n\xce\xbb";
    native.layerBoxGlobal={100,200,1600,1000};native.monitorPosition={100,200};native.monitorScale=1.25;
    native.generation=9007199254740993ULL;
    GlassShapes::SGlassShape shape{.x=1407.125f,.y=552.375f,.width=120.25f,.height=40.5f,
        .radii={20,4,4,20},.depth=3,.tint=0xfedcba98,.preset="clear\"\\\n\t",.opacity=.5f,
        .clip=std::array<float,4>{4,5,1500,900}};
    native.applied->push_back(shape);
    shape.depth=1;shape.clip.reset();shape.x=-3.5f;native.applied->push_back(shape);
    layers.push_back(native);
    auto excluded=native;excluded.nameSpace="excluded";excluded.renderingEligible=false;
    layers.push_back(excluded);
    const std::string mode=argc>1 ? argv[1] : "json";
    if(mode=="bounded") {
        layers.assign(40,legacy);native.applied->resize(140,shape);
        native.nameSpace=std::string(4096,'x');native.applied->front().preset=std::string(4096,'p');
        layers.push_back(native); // Bound entry must displace legacy entries at the cap.
    } else if(mode=="nonfinite") {
        native.applied->front().x=std::numeric_limits<float>::quiet_NaN();
        native.applied->front().opacity=std::numeric_limits<float>::infinity();
        layers={native};
    }
    const auto copy=layers;
    std::cout<<formatAppliedShapes(layers, mode!="inactive", layers.size(), mode!="text");
    assert(layers.size()==copy.size());
    assert(layers.front().nameSpace==copy.front().nameSpace); // Formatter never mutates metadata.
}
