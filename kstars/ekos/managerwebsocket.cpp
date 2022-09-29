/*
    Copyright (C) 2022 by Pawel Soja <kernel32.pl@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "managerwebsocket.h"
#include "manager.h"

#include <QHostAddress>
#include <QUrl>

#include "managerwebsocket_helpers.h"

namespace Ekos
{

template <std::size_t ... Indexes>
static bool s_invoke_impl(QObject *obj, const QMetaMethod &metaMethod, QVariant &returnValue, QVariantList args, std::index_sequence<Indexes...>)
{
    if (metaMethod.isValid() == false || metaMethod.parameterCount() != args.count())
    {
        return false;
    }

    std::array<QGenericArgument, sizeof ... (Indexes)> genericArguments;

    for (int i=0; i<metaMethod.parameterCount(); ++i)
    {
        auto &arg = args[i];
        if (!arg.convert(metaMethod.parameterType(i)))
        {
            return false;
        }
        genericArguments[i] = QGenericArgument(QMetaType::typeName(arg.userType()), arg.constData());
    }

    const auto typeId = QMetaType::type(metaMethod.typeName());

    if (typeId == QMetaType::UnknownType)
    {
        return false;
    }

    if (typeId != QMetaType::Void)
    {
        returnValue = QVariant(typeId, nullptr);
    }

    return metaMethod.invoke(
                obj, Qt::AutoConnection,
                QGenericReturnArgument(metaMethod.typeName(), returnValue.data()),
                genericArguments.at(Indexes)...
    );
}

static bool s_invoke(QObject *obj, const QByteArray &methodName, QVariant &returnValue, const QVariantList &args)
{
    const QMetaObject *metaObject = obj->metaObject();
    for (int i=0; i<metaObject->methodCount(); ++i)
    {
        auto metaMethod = metaObject->method(i);
        if (methodName != metaMethod.methodSignature() && methodName != metaMethod.name())
        {
            continue;
        }

        if (s_invoke_impl(obj, metaMethod, returnValue, args, std::make_index_sequence<Q_METAMETHOD_INVOKE_MAX_ARGS>{}))
        {
            return true;
        }
    }
    return false;
}

static QVariant s_getAllMethods(QObject *obj)
{
    const QMetaObject *metaObject = obj->metaObject();
    QVariantList result;
    for (int i=0; i<metaObject->methodCount(); ++i)
    {
        result.append(metaObject->method(i).methodSignature());
    }
    return result;
}

static void s_invokeModule(QObject *object, const QByteArray &objectName, const QVariantMap &objectArgs, QVariantMap &result)
{
    if (object == nullptr)
        return;

    QVariantMap methodArgs = objectArgs[objectName].toMap();

    if (objectArgs[objectName].toString() == "@availableMethods")
    {
        qvariant_ref<QVariantMap>(result[objectName])["@availableMethods"] = s_getAllMethods(object);
    }

    for (const auto &methodName: methodArgs.keys())
    {
        QVariant resultValue;
        
        const bool isInvoked = s_invoke(
            object,                                             // pointer to object
            methodName.toLatin1(),                              // method name e.g. someMethod or someMethod(int,int)
            resultValue,                                        // QVariant as result
            qvariant_ref<QVariantList>(methodArgs[methodName])  // list of arguments for this method
        );

        if (isInvoked && resultValue.isValid())
        {
            qvariant_ref<QVariantMap>(result[objectName])[methodName] = variantize(resultValue); // append result to "result" map
        }
    }
}

static QString s_qrcPath(const QString &path)
{
    // default to index.html
    if (path == "/")
    {
        return ":/managerwebsocket/index.html";
    }

    // exception, allow to access another directory
    // access to images /qml/mount/*.png
    if (path.startsWith("/qml/mount/") && path.endsWith(".png"))
    {
        return ":/" + path;
    }

    return ":/managerwebsocket/" + path;
}


ManagerWebSocket::ManagerWebSocket(Manager *manager)
    : m_manager(manager)
    , m_server("Ekos WebSocket Server", QWebSocketServer::NonSecureMode)
{
    m_server.listen(QHostAddress::Any, WebSocketPort);

    QObject::connect(&m_server, &QWebSocketServer::newConnection, [this](){
        QWebSocket *client = m_server.nextPendingConnection();

        QObject::connect(client, &QWebSocket::disconnected, [client](){
            client->deleteLater();
        });
        
        QObject::connect(client, &QWebSocket::textMessageReceived, [this, client](const QString &message){
            /*
            * Example:
            * {
            *     ekos: {
            *         mount: {
            *              motionCommand: [0, 1, -1]
            *         },
            *         focus: {
            *              adjustFocusOffset: [100, false]
            *         }
            *     }
            * }
            */
            auto args = QJsonDocument::fromJson(message.toLatin1()).toVariant().toMap()["ekos"].toMap();
            if (args.count() == 0)
                return;

            QVariantMap result;

            s_invokeModule(m_manager->mountModule(), "mount", args, result);
            s_invokeModule(m_manager->focusModule(), "focus", args, result);

            if (!result.isEmpty())
            {
                QVariantMap resultMap;
                resultMap["ekos"] = result;
                client->sendTextMessage(QJsonDocument::fromVariant(resultMap).toJson());
            }
            /* ... */
        
        });
    });

    // Simply Http Server for qt5
    m_webServer.listen(QHostAddress::Any, HttpServerPort);
    QObject::connect(&m_webServer, &QTcpServer::newConnection, [this](){
        QTcpSocket *client = m_webServer.nextPendingConnection();

        QObject::connect(client, &QTcpSocket::disconnected, [client](){
            client->deleteLater();
        });

        QObject::connect(client, &QTcpSocket::readyRead, [this, client]()
        {
            for(;;)
            {
                const QByteArray request = client->peek(8*1024);

                int requestSize = request.indexOf("\r\n\r\n");
                if (requestSize == -1)
                {
                    return;
                }

                requestSize += 4; // \r\n\r\n

                const QString path = QUrl(client->readLine().split(' ').at(1)).path();

                QByteArray content = "Not Found";


                // auto generate content
                if (path == "/target.js")
                {
                    content = QString("var serverAddress = '%1://[%2]:%3/';")
                        .arg(m_server.secureMode() == QWebSocketServer::NonSecureMode ? "ws" : "wss")
                        .arg(client->localAddress().toString())
                        .arg(m_server.serverPort()).toLatin1();
                }
                else
                {
                    // load from qrc
                    QFile file(s_qrcPath(path));
                    if(file.open(QIODevice::ReadOnly))
                    {
                        content = file.readAll();
                    }
                }

                client->write(QString(
                    "HTTP/1.1 OK\r\n"
                    "Content-Length: %1\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                ).arg(content.size()).toLatin1() + content);

                client->skip(requestSize);
            }
        });
    });
}

}
