// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "." as Input

Input.NexusRuntime {
    Component.onCompleted: activate(Qt.resolvedUrl("NexusBridge.qml"))
}
