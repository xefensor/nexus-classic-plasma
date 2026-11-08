// SPDX-License-Identifier: Apache-2.0
import QtQuick
import "." as Input

Item {
    id: bridge
    objectName: "nexusClassicBridge"
    property var engine: null
    property bool mouseEnabled: true
    property bool shared: false
    property bool attached: true
    property var routing: [0, 1, 2, 3]
    readonly property int extraCount: state.extraCount
    readonly property url textureUrl: state.textureUrl
    function detach() { attached = false }
    Component.onDestruction: detach()
    onEngineChanged: {
        if (!engine) return
        let channels = []
        for (let i = 0; i < 4; i++) channels.push(engine.imageChannels[i])
        channels[3] = 3
        routing = channels
    }
    Input.NexusState {
        id: state
        target: bridge.engine
        mouseEnabled: bridge.mouseEnabled
        shared: bridge.shared
        desktopPixels: bridge.engine ? bridge.engine.desktopResolution : Qt.vector2d(0, 0)
        screenOffset: bridge.engine ? bridge.engine.screenOffset : Qt.vector2d(0, 0)
        pixelRatio: bridge.engine && bridge.engine.devicePixelRatio > 0 ? bridge.engine.devicePixelRatio : 1
    }
    Binding {
        target: bridge.engine
        property: "iChannel3"
        value: state.textureUrl
        when: bridge.attached && bridge.engine !== null && state.textureUrl.toString().length > 0
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: bridge.engine
        property: "iChannel3Enabled"
        value: true
        when: bridge.attached && bridge.engine !== null
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: bridge.engine
        property: "imageChannels"
        value: bridge.routing
        when: bridge.attached && bridge.engine !== null
        restoreMode: Binding.RestoreBindingOrValue
    }
}
