import QtQuick
import qs.services

// Applestia inner-glass material (see glassfill.frag): a slightly frostier
// layer of glass for cards, pills and buttons that sit on a glass panel.
// `frost` scales the fill; `rimStrength` the specular edge.
ShaderEffect {
    id: root

    property real radius
    property real topLeftRadius: radius
    property real topRightRadius: radius
    property real bottomRightRadius: radius
    property real bottomLeftRadius: radius
    property real frost: 1
    // a step frostier than the panel glass: light mode lifts with white, dark mode
    // deepens with a cool neutral so text stays legible over bright backdrops
    property color tint: Qt.alpha(InnerGlass.innerGlassTint, InnerGlass.innerGlassTint.a * frost)

    readonly property size size: Qt.size(width, height)
    readonly property vector4d radii: Qt.vector4d(topLeftRadius, topRightRadius, bottomRightRadius, bottomLeftRadius)
    readonly property vector4d fill: Qt.vector4d(tint.r, tint.g, tint.b, tint.a)
    // Native refraction needs a transmissive edge. The former painted white
    // rim masked the backdrop precisely where the compositor bends it.
    property real rimStrength: InnerGlass.available ? 0.08 : Colours.light ? 0.55 : 0.42
    property real rimTransmission: InnerGlass.available ? 0.8 : 0
    property real sheen: Colours.light ? 0.06 : 0.035
    readonly property vector2d lightDir: Qt.vector2d(-0.45, -0.89) // from the top, slightly left
    property real rimWidth: 1.4

    fragmentShader: Qt.resolvedUrl("glassfill.frag.qsb")
    blending: true
}
