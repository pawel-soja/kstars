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
#pragma once
#include <QWebSocketServer>
#include <QTcpServer>
#include <QByteArray>

namespace Ekos
{

class Manager;
/**
 * @class ManagerWebSocket
 * @short The class allows calling functions via websocket using the json format.
 * In addition, there is a server with a website that allows you to control the mount or focus.
 */
class ManagerWebSocket
{
public:
    enum {
        HttpServerPort = 1080,
        WebSocketPort = 1081
    };

public:
    explicit ManagerWebSocket(Manager *manager);

protected:
    Manager *m_manager;
    QWebSocketServer m_server;
    QTcpServer m_webServer;
};

}
