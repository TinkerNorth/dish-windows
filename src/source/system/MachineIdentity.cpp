// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/system/MachineIdentity.h"

#include "core/diagnostics/HostIdentity.h"

#include <QSettings>
#include <QSysInfo>

namespace dish::source {

MachineIdentity readMachineIdentity() {
    const QSettings bios(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS"),
                         QSettings::NativeFormat);
    const QString manufacturer = bios.value(QStringLiteral("SystemManufacturer")).toString();
    const QString product = bios.value(QStringLiteral("SystemProductName")).toString();
    const QString kernel = QSysInfo::kernelVersion();

    MachineIdentity out;
    out.model = QString::fromStdString(
        diagnostics::machineModelOf(manufacturer.toStdString(), product.toStdString()));
    out.os = QStringLiteral("%1 (%2)").arg(QSysInfo::prettyProductName(), kernel);
    out.arch = QSysInfo::currentCpuArchitecture();
    out.windowsMajor = diagnostics::windowsMajorFromKernel(kernel.toStdString());
    return out;
}

} // namespace dish::source
