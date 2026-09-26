import QtQuick
import StylusProbe

Window {
    id: window

    width: 1200
    height: 800
    visible: true
    title: "PHY-97 stylus probe (" + Recorder.mode + ")"
    color: "white"

    InkView {
        id: ink

        anchors.fill: parent
        recorder: Recorder
    }

    // What a QML UI would get: one PointHandler per device class. They only
    // take passive grabs, so they do not stop the window-level recording.
    PointHandler {
        acceptedDevices: PointerDevice.Stylus | PointerDevice.Airbrush
        onPointChanged: Recorder.recordHandler("stylus", point.position.x, point.position.y, point.pressure, active)
    }
    PointHandler {
        acceptedDevices: PointerDevice.Mouse
        onPointChanged: Recorder.recordHandler("mouse", point.position.x, point.position.y, point.pressure, active)
    }
    PointHandler {
        acceptedDevices: PointerDevice.TouchPad | PointerDevice.TouchScreen
        onPointChanged: Recorder.recordHandler("touch", point.position.x, point.position.y, point.pressure, active)
    }
    // Mouse and touchpad draw in toggle mode, with no button held: what a QML
    // UI would get is hover.
    HoverHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onPointChanged: Recorder.recordHandler("hover", point.position.x, point.position.y, point.pressure, hovered)
    }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_C) {
                Recorder.clear();
            } else if (event.key === Qt.Key_S) {
                status.text = "Saved: " + Recorder.save();
            } else if (event.key === Qt.Key_P) {
                ink.showPoints = !ink.showPoints;
            } else if (event.key === Qt.Key_T) {
                Recorder.mark("touchpad");
                status.text = "Marker: touchpad";
            } else if (event.key === Qt.Key_M) {
                Recorder.mark("mouse");
                status.text = "Marker: mouse";
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        width: 520
        height: summary.implicitHeight + status.implicitHeight + 24
        color: "#e8ffffff"
        border.color: "#999"

        Column {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Text {
                text: "Stylus: draw while touching. Mouse and touchpad: click to start drawing, click again to stop. T before using the touchpad, M before using the mouse. C: clear, S: save, P: toggle points"
                font.bold: true
                wrapMode: Text.Wrap
                width: parent.width
            }
            Text {
                id: summary

                font.family: "monospace"
                font.pixelSize: 11
                wrapMode: Text.Wrap
                width: parent.width
            }
            Text {
                id: status

                font.pixelSize: 11
                wrapMode: Text.Wrap
                width: parent.width
            }
        }
    }

    Timer {
        interval: 500
        repeat: true
        running: true
        onTriggered: summary.text = Recorder.summary()
    }
}
