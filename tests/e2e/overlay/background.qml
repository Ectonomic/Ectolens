// A desktop background for the off-screen KWin: an image on the layer-shell background
// layer, so KWin and Ectolens treat it as the desktop, not as a window.
import QtQuick
import org.kde.layershell 1.0 as LayerShell

Window {
    visible: true
    color: "black"
    LayerShell.Window.layer: LayerShell.Window.LayerBackground
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone
    Image {
        anchors.fill: parent
        source: Qt.resolvedUrl(Qt.application.arguments[Qt.application.arguments.length - 1])
        fillMode: Image.PreserveAspectCrop
    }
}
