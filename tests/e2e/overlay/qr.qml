// A QR code on screen for the Open QR Code check: it says https://ectonomic.com.
import QtQuick
import QtQuick.Window
import org.kde.prison as Prison
Window {
    width: 170; height: 170; visible: true; color: "white"; title: "QR test"
    Prison.Barcode { anchors.fill: parent; anchors.margins: 10; barcodeType: Prison.Barcode.QRCode; content: "https://ectonomic.com" }
}
