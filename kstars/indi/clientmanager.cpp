/*
    SPDX-FileCopyrightText: 2012 Jasem Mutlaq <mutlaqja@ikarustech.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "clientmanager.h"

#include "deviceinfo.h"
#include "drivermanager.h"
#include "guimanager.h"
#include "indilistener.h"
#include "Options.h"
#include "servermanager.h"

#include <indi_debug.h>
#include <QTimer>

ClientManager::ClientManager()
{
    connect(this, &ClientManager::newINDIProperty, this, &ClientManager::processNewProperty, Qt::UniqueConnection);
    connect(this, &ClientManager::removeBLOBManager, this, &ClientManager::processRemoveBLOBManager, Qt::UniqueConnection);
}

bool ClientManager::isDriverManaged(DriverInfo *di)
{
    return std::any_of(m_ManagedDrivers.begin(), m_ManagedDrivers.end(), [di](const auto & oneDriver)
    {
        return di == oneDriver;
    });
}

void ClientManager::newDevice(INDI::BaseDevice *dp)
{
    //setBLOBMode(B_ALSO, dp->getDeviceName());
    // JM 2018.09.27: ClientManager will no longer handle BLOB, just messages.
    // We relay the BLOB handling to BLOB Manager to better manage concurrent connections with large data
    setBLOBMode(B_NEVER, dp->getDeviceName());

    DriverInfo *deviceDriver = nullptr;

    if (QString(dp->getDeviceName()).isEmpty())
    {
        qCWarning(KSTARS_INDI) << "Received invalid device with empty name! Ignoring the device...";
        return;
    }

    qCDebug(KSTARS_INDI) << "Received new device" << dp->getDeviceName();

    // First iteration find unique matches
    for (auto &oneDriverInfo : m_ManagedDrivers)
    {
        if (oneDriverInfo->getUniqueLabel() == QString(dp->getDeviceName()))
        {
            deviceDriver = oneDriverInfo;
            break;
        }
    }

    // Second iteration find partial matches
    if (deviceDriver == nullptr)
    {
        for (auto &oneDriverInfo : m_ManagedDrivers)
        {
            auto dvName = oneDriverInfo->getName().split(' ').first();
            if (dvName.isEmpty())
                dvName = oneDriverInfo->getName();
            if (/*dv->getUniqueLabel() == dp->getDeviceName() ||*/
                QString(dp->getDeviceName()).startsWith(dvName, Qt::CaseInsensitive) ||
                ((oneDriverInfo->getDriverSource() == HOST_SOURCE || oneDriverInfo->getDriverSource() == GENERATED_SOURCE)))
            {
                deviceDriver = oneDriverInfo;
                break;
            }
        }
    }

    if (deviceDriver == nullptr)
        return;

    deviceDriver->setUniqueLabel(dp->getDeviceName());

    DeviceInfo *devInfo = new DeviceInfo(deviceDriver, dp);
    deviceDriver->addDevice(devInfo);
    emit newINDIDevice(devInfo);
}

void ClientManager::newProperty(INDI::Property *pprop)
{
    INDI::Property prop(*pprop);

    // Do not emit the signal if the server is disconnected or disconnecting (deadlock between signals)
    if (!isServerConnected())
    {
        IDLog("Received new property %s for disconnected device %s, discarding\n", prop.getName(), prop.getDeviceName());
        return;
    }

    //IDLog("Received new property %s for device %s\n", prop->getName(), prop->getgetDeviceName());
    emit newINDIProperty(prop);
}

void ClientManager::removeProperty(INDI::Property *prop)
{
    const QString name = prop->getName();
    const QString device = prop->getDeviceName();
    emit removeINDIProperty(device, name);

    // If BLOB property is removed, remove its corresponding property if one exists.
    if (blobManagers.empty() == false && prop->getType() == INDI_BLOB && prop->getPermission() != IP_WO)
        emit removeBLOBManager(device, name);
}

void ClientManager::processRemoveBLOBManager(const QString &device, const QString &property)
{
    auto manager = std::find_if(blobManagers.begin(), blobManagers.end(), [device, property](auto & oneManager)
    {
        const auto bProperty = oneManager->property("property").toString();
        const auto bDevice = oneManager->property("device").toString();
        return (device == bDevice && property == bProperty);
    });

    if (manager != blobManagers.end())
    {
        blobManagers.removeOne(*manager);
        (*manager)->disconnectServer();
        (*manager)->deleteLater();
    }
}

void ClientManager::processNewProperty(INDI::Property prop)
{
    // Only handle RW and RO BLOB properties
    if (prop.getType() == INDI_BLOB && prop.getPermission() != IP_WO)
    {
        BlobManager *bm = new BlobManager(this, getHost(), getPort(), prop.getBaseDevice()->getDeviceName(), prop.getName());
        connect(bm, &BlobManager::newINDIBLOB, this, &ClientManager::newINDIBLOB);
        connect(bm, &BlobManager::connected, this, [prop, this]()
        {
            if (prop && prop.getRegistered())
                emit newBLOBManager(prop->getBaseDevice()->getDeviceName(), prop);
        });
        blobManagers.append(bm);
    }
}

void ClientManager::disconnectAll()
{
    disconnectServer();
    for (auto &oneManager : blobManagers)
        oneManager->disconnectServer();
}

void ClientManager::removeDevice(INDI::BaseDevice *dp)
{
    QString deviceName = dp->getDeviceName();

    QMutableListIterator<BlobManager*> it(blobManagers);
    while (it.hasNext())
    {
        auto &oneManager = it.next();
        if (oneManager->property("device").toString() == deviceName)
        {
            oneManager->disconnect();
            it.remove();
        }
    }

    for (auto &driverInfo : m_ManagedDrivers)
    {
        for (auto &deviceInfo : driverInfo->getDevices())
        {
            if (deviceInfo->getDeviceName() == deviceName)
            {
                qCDebug(KSTARS_INDI) << "Removing device" << deviceName;

                emit removeINDIDevice(deviceName);

                driverInfo->removeDevice(deviceInfo);

                if (driverInfo->isEmpty())
                {
                    m_ManagedDrivers.removeOne(driverInfo);
                    if (driverInfo->getDriverSource() == GENERATED_SOURCE)
                        driverInfo->deleteLater();
                }

                return;
            }
        }
    }
}

void ClientManager::newBLOB(IBLOB *bp)
{
    emit newINDIBLOB(bp);
}

void ClientManager::newSwitch(ISwitchVectorProperty *svp)
{
    emit newINDISwitch(svp);
}

void ClientManager::newNumber(INumberVectorProperty *nvp)
{
    emit newINDINumber(nvp);
}

void ClientManager::newText(ITextVectorProperty *tvp)
{
    emit newINDIText(tvp);
}

void ClientManager::newLight(ILightVectorProperty *lvp)
{
    emit newINDILight(lvp);
}

void ClientManager::newMessage(INDI::BaseDevice *dp, int messageID)
{
    emit newINDIMessage(dp, messageID);
}

#if INDI_VERSION_MAJOR >= 1 && INDI_VERSION_MINOR >= 5
void ClientManager::newUniversalMessage(std::string message)
{
    emit newINDIUniversalMessage(QString::fromStdString(message));
}
#endif

void ClientManager::appendManagedDriver(DriverInfo *dv)
{
    qCDebug(KSTARS_INDI) << "Adding managed driver" << dv->getName();

    m_ManagedDrivers.append(dv);

    dv->setClientManager(this);

    sManager = dv->getServerManager();
}

void ClientManager::removeManagedDriver(DriverInfo *dv)
{
    qCDebug(KSTARS_INDI) << "Removing managed driver" << dv->getName();

    dv->setClientState(false);
    m_ManagedDrivers.removeOne(dv);

    for (auto &di : dv->getDevices())
    {
        // #1 Remove from GUI Manager
        GUIManager::Instance()->removeDevice(di->getDeviceName());

        // #2 Remove from INDI Listener
        INDIListener::Instance()->removeDevice(di->getDeviceName());

        // #3 Remove device from Driver Info
        dv->removeDevice(di);
    }

    if (dv->getDriverSource() == GENERATED_SOURCE)
        dv->deleteLater();
}

void ClientManager::serverConnected()
{
    qCDebug(KSTARS_INDI) << "INDI server connected.";

    for (auto &oneDriverInfo : m_ManagedDrivers)
    {
        oneDriverInfo->setClientState(true);
        if (sManager)
            oneDriverInfo->setHostParameters(sManager->getHost(), sManager->getPort());
    }

    m_PendingConnection = false;
    m_ConnectionRetries = MAX_RETRIES;

    emit started();
}

void ClientManager::serverDisconnected(int exitCode)
{
    qCDebug(KSTARS_INDI) << "INDI server disconnected. Exit code:" << exitCode;

    for (auto &oneDriverInfo : m_ManagedDrivers)
    {
        oneDriverInfo->setClientState(false);
        oneDriverInfo->reset();
    }

    if (m_PendingConnection)
    {
        // Should we retry again?
        if (m_ConnectionRetries-- > 0)
        {
            // Connect again in 1 second.
            QTimer::singleShot(1000, this, [this]()
            {
                qCDebug(KSTARS_INDI) << "Retrying connection again";
                connectServer();
            });
        }
        // Nope cannot connect to server.
        else
        {
            m_PendingConnection = false;
            m_ConnectionRetries = MAX_RETRIES;
            emit failed(i18n("Failed to connect to INDI server %1:%2", getHost(), getPort()));
        }
    }
    // Did server disconnect abnormally?
    else if (exitCode < 0)
        emit terminated(i18n("Connection to INDI host at %1 on port %2 lost. Server disconnected: %3", getHost(), getPort(),
                             exitCode));
}

QList<DriverInfo *> ClientManager::getManagedDrivers() const
{
    return m_ManagedDrivers;
}

void ClientManager::establishConnection()
{
    qCDebug(KSTARS_INDI)
            << "INDI: Connecting to local INDI server on port " << getPort() << " ...";

    m_PendingConnection = true;
    m_ConnectionRetries = 2;

    connectServer();
}

DriverInfo *ClientManager::findDriverInfoByName(const QString &name)
{
    auto pos = std::find_if(m_ManagedDrivers.begin(), m_ManagedDrivers.end(), [name](DriverInfo * oneDriverInfo)
    {
        return oneDriverInfo->getName() == name;
    });

    if (pos != m_ManagedDrivers.end())
        return *pos;
    else
        return nullptr;
}

DriverInfo *ClientManager::findDriverInfoByLabel(const QString &label)
{
    auto pos = std::find_if(m_ManagedDrivers.begin(), m_ManagedDrivers.end(), [label](DriverInfo * oneDriverInfo)
    {
        return oneDriverInfo->getLabel() == label;
    });

    if (pos != m_ManagedDrivers.end())
        return *pos;
    else
        return nullptr;
}

void ClientManager::setBLOBEnabled(bool enabled, const QString &device, const QString &property)
{
    for(auto &bm : blobManagers)
    {
        if (bm->property("device") == device && (property.isEmpty() || bm->property("property") == property))
        {
            bm->setEnabled(enabled);
            return;
        }
    }
}

bool ClientManager::isBLOBEnabled(const QString &device, const QString &property)
{
    for(auto &bm : blobManagers)
    {
        if (bm->property("device") == device && bm->property("property") == property)
            return bm->property("enabled").toBool();
    }

    return false;
}
