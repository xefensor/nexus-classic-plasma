// SPDX-License-Identifier: Apache-2.0
#include "nexusinput.h"
#include <QQmlExtensionPlugin>
#include <qqml.h>

class NexusInputPlugin : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
public:
    void registerTypes(const char *uri) override
    {
        qmlRegisterType<NexusState>(uri, 1, 0, "NexusState");
        qmlRegisterType<NexusRuntime>(uri, 1, 0, "NexusRuntime");
    }
};
#include "plugin.moc"
