import QtQuick
import QtQuick.Controls
import Misign

ApplicationWindow {
    id: window

    required property DocumentModel document

    width: 1024
    height: 768
    visible: true
    title: qsTr("Misign")
    color: palette.window

    PageView {
        objectName: "pageView"
        anchors.fill: parent
        document: window.document
    }
}
