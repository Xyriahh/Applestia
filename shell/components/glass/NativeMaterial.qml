import QtQuick
import Applestia.Glass

LiquidGlass {
    required property var owner // StyledRect or StyledClippingRect material API
    sourceItem: owner
    radius: owner.radius
    topLeftRadius: owner.topLeftRadius
    topRightRadius: owner.topRightRadius
    bottomRightRadius: owner.bottomRightRadius
    bottomLeftRadius: owner.bottomLeftRadius
    tint: owner.nativeTint
    preset: owner.glassPreset
}
