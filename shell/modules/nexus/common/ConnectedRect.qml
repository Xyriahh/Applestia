import QtQuick
import Caelestia.Config
import qs.components
import qs.services

StyledRect {
    id: root

    property bool first
    property bool last
    readonly property bool settingsRow: true
    // Switch backgrounds live inside the control rather than the page layout.
    readonly property Item rowItem: parent && parent.bg === root ? parent : root
    readonly property var groupRows: {
        const layout = rowItem.parent;
        if (!layout)
            return [rowItem];
        // Reading geometry here makes the group follow layout changes, loaders,
        // expanded lists and animated row heights without a polling timer.
        return Array.from(layout.children).filter(item => item.visible && item.height > 0)
            .sort((a, b) => a.y - b.y);
    }

    function backgroundFor(item) {
        const candidate = item?.item ?? item;
        return candidate?.bg?.settingsRow ? candidate.bg : candidate?.settingsRow ? candidate : null;
    }

    readonly property int rowIndex: groupRows.indexOf(rowItem)
    readonly property var previousRow: rowIndex > 0 ? groupRows[rowIndex - 1] : null
    readonly property bool startsGroup: first || !backgroundFor(previousRow)
        || backgroundFor(previousRow).last
        || Math.abs(rowItem.y - (previousRow.y + previousRow.height)) > 1
    readonly property real groupHeight: {
        if (!startsGroup)
            return height;
        let end = rowItem.y + rowItem.height;
        if (!last) {
            for (let i = rowIndex + 1; i < groupRows.length; ++i) {
                const item = groupRows[i];
                const bg = backgroundFor(item);
                if (!bg || bg.first || Math.abs(item.y - end) > 1)
                    break;
                end = item.y + item.height;
                if (bg.last)
                    break;
            }
        }
        return Math.max(height, end - rowItem.y);
    }
    readonly property var nextRow: rowIndex >= 0 ? groupRows[rowIndex + 1] : null
    readonly property bool endsGroup: last || !backgroundFor(nextRow)
        || backgroundFor(nextRow).first
        || Math.abs(nextRow.y - (rowItem.y + rowItem.height)) > 1

    // One continuous material per section, not a stack of separately rimmed pills.
    color: "transparent"
    glass: false
    refractive: false
    topLeftRadius: startsGroup ? AppleTokens.moduleRadius : 0
    topRightRadius: topLeftRadius
    bottomLeftRadius: endsGroup ? AppleTokens.moduleRadius : 0
    bottomRightRadius: bottomLeftRadius

    StyledRect {
        z: -1
        width: root.width
        height: root.groupHeight
        visible: root.startsGroup
        radius: AppleTokens.moduleRadius
        glass: true
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        height: 1
        visible: !root.endsGroup
        color: Qt.alpha(AppleTokens.separator, AppleTokens.light ? 0.65 : 0.45)
    }
}
