pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Misign

// The pages of the document, one under the other. A page is only rendered
// when the ListView creates its delegate, as it scrolls into view.
ListView {
    id: view

    required property DocumentModel document
    // Logical pixels per point: 100 % zoom, one point being 1/72 inch.
    readonly property real pointScale: 96 / 72
    // Size of the placeholder shown for a page without a visible area, in points.
    readonly property size placeholderSize: Qt.size(595, 120)

    model: document
    spacing: 16
    topMargin: spacing
    bottomMargin: spacing
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    contentWidth: Math.max(width, contentItem.childrenRect.width)
    flickableDirection: Flickable.AutoFlickIfNeeded

    ScrollBar.vertical: ScrollBar {}
    ScrollBar.horizontal: ScrollBar {}

    delegate: Item {
        id: page

        required property int index
        required property real pageWidth
        required property real pageHeight
        required property bool hasVisibleArea

        objectName: "page" + index
        width: Math.max(view.width, sheet.width)
        height: sheet.height

        Rectangle {
            id: sheet

            objectName: "sheet"
            anchors.horizontalCenter: parent.horizontalCenter
            width: (page.hasVisibleArea ? page.pageWidth : view.placeholderSize.width) * view.pointScale
            height: (page.hasVisibleArea ? page.pageHeight : view.placeholderSize.height) * view.pointScale
            color: page.hasVisibleArea ? "white" : "transparent"
            border.color: page.hasVisibleArea ? "transparent" : palette.mid
            border.width: page.hasVisibleArea ? 0 : 1

            Image {
                objectName: "image"
                anchors.fill: parent
                visible: page.hasVisibleArea
                // Qt Quick multiplies sourceSize by the device pixel ratio
                // before asking the provider, so the page is rendered at the
                // size in device pixels and stays sharp on high-DPI screens.
                sourceSize: Qt.size(width, height)
                source: page.hasVisibleArea ? "image://pages/" + view.document.generation + "/" + page.index : ""
                // The renderer is used from the GUI thread only.
                asynchronous: false
                cache: false
            }

            Label {
                objectName: "placeholder"
                anchors.centerIn: parent
                visible: !page.hasVisibleArea
                text: qsTr("Page %1 has no visible area").arg(page.index + 1)
            }
        }
    }
}
