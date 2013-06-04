/*********************************************************************/
/* Copyright (c) 2011 - 2012, The University of Texas at Austin.     */
/* All rights reserved.                                              */
/*                                                                   */
/* Redistribution and use in source and binary forms, with or        */
/* without modification, are permitted provided that the following   */
/* conditions are met:                                               */
/*                                                                   */
/*   1. Redistributions of source code must retain the above         */
/*      copyright notice, this list of conditions and the following  */
/*      disclaimer.                                                  */
/*                                                                   */
/*   2. Redistributions in binary form must reproduce the above      */
/*      copyright notice, this list of conditions and the following  */
/*      disclaimer in the documentation and/or other materials       */
/*      provided with the distribution.                              */
/*                                                                   */
/*    THIS  SOFTWARE IS PROVIDED  BY THE  UNIVERSITY OF  TEXAS AT    */
/*    AUSTIN  ``AS IS''  AND ANY  EXPRESS OR  IMPLIED WARRANTIES,    */
/*    INCLUDING, BUT  NOT LIMITED  TO, THE IMPLIED  WARRANTIES OF    */
/*    MERCHANTABILITY  AND FITNESS FOR  A PARTICULAR  PURPOSE ARE    */
/*    DISCLAIMED.  IN  NO EVENT SHALL THE UNIVERSITY  OF TEXAS AT    */
/*    AUSTIN OR CONTRIBUTORS BE  LIABLE FOR ANY DIRECT, INDIRECT,    */
/*    INCIDENTAL,  SPECIAL, EXEMPLARY,  OR  CONSEQUENTIAL DAMAGES    */
/*    (INCLUDING, BUT  NOT LIMITED TO,  PROCUREMENT OF SUBSTITUTE    */
/*    GOODS  OR  SERVICES; LOSS  OF  USE,  DATA,  OR PROFITS;  OR    */
/*    BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON ANY THEORY OF    */
/*    LIABILITY, WHETHER  IN CONTRACT, STRICT  LIABILITY, OR TORT    */
/*    (INCLUDING NEGLIGENCE OR OTHERWISE)  ARISING IN ANY WAY OUT    */
/*    OF  THE  USE OF  THIS  SOFTWARE,  EVEN  IF ADVISED  OF  THE    */
/*    POSSIBILITY OF SUCH DAMAGE.                                    */
/*                                                                   */
/* The views and conclusions contained in the software and           */
/* documentation are those of the authors and should not be          */
/* interpreted as representing official policies, either expressed   */
/* or implied, of The University of Texas at Austin.                 */
/*********************************************************************/

#include "DcSocket.h"
#include "../NetworkProtocol.h"
#include "../log.h"
#include <QtNetwork/QTcpSocket>

DcSocket::DcSocket(const char * hostname)
{
    // defaults
    socket_ = NULL;
    firstMessageQueued_ = false;

    if(connect(hostname) != true)
    {
        put_flog(LOG_ERROR, "could not connect to host %s", hostname);
    }
}

DcSocket::~DcSocket()
{
    disconnect();
}

bool DcSocket::isConnected()
{
    if(socket_ == NULL)
    {
        put_flog(LOG_ERROR, "socket is NULL");

        return false;
    }
    else if(socket_->state() != QAbstractSocket::ConnectedState)
    {
        put_flog(LOG_ERROR, "socket is not connected");

        return false;
    }

    return true;
}

bool DcSocket::queueMessage(QByteArray message)
{
    // only queue the message if we're connected
    if(isConnected() != true)
    {
        return false;
    }

    // don't queue new message until the ack for the last message has been received
    if(firstMessageQueued_ == true)
    {
        socketWaitForAck();
    }

    sendMessagesQueue_.push(message);

    // todo: the actual sending / receiving of messages over the socket should happen in another thread (including the ack above)
    if(socketSendQueuedMessages() != true)
    {
        return false;
    }

    firstMessageQueued_ = true;

    return true;
}

bool DcSocket::connect(const char * hostname)
{
    // reset
    firstMessageQueued_ = false;

    // delete existing socket if we have one
    if(socket_ != NULL)
    {
        delete socket_;
    }

    socket_ = new QTcpSocket();

    // open connection
    socket_->connectToHost(hostname, 1701);

    if(socket_->waitForConnected() != true)
    {
        put_flog(LOG_ERROR, "could not connect to host %s", hostname);

        delete socket_;
        socket_ = NULL;

        return false;
    }

    // handshake
    while(socket_->waitForReadyRead() && socket_->bytesAvailable() < (int)sizeof(int32_t))
    {
    #ifndef _WIN32
        usleep(10);
    #endif
    }

    int32_t protocolVersion = -1;
    socket_->read((char *)&protocolVersion, sizeof(int32_t));

    if(protocolVersion != NETWORK_PROTOCOL_VERSION)
    {
        socket_->disconnectFromHost();

        put_flog(LOG_ERROR, "unsupported protocol version %i != %i", protocolVersion, NETWORK_PROTOCOL_VERSION);

        delete socket_;
        socket_ = NULL;

        return false;
    }

    put_flog(LOG_INFO, "connected to to host %s", hostname);

    return true;
}

void DcSocket::disconnect()
{
    if(socket_ != NULL)
    {
        delete socket_;
        socket_ = NULL;
    }
}

bool DcSocket::socketSendQueuedMessages()
{
    if(socket_->state() != QAbstractSocket::ConnectedState)
    {
        return false;
    }

    while(sendMessagesQueue_.size() > 0)
    {
        QByteArray message = sendMessagesQueue_.front();
        sendMessagesQueue_.pop();

        char * data = message.data();
        int size = message.size();

        int sent = socket_->write(data, size);

        while(sent < size && socket_->state() == QAbstractSocket::ConnectedState)
        {
            sent += socket_->write(data + sent, size - sent);
        }

        if(sent != size)
        {
            return false;
        }
    }

    return true;
}

void DcSocket::socketWaitForAck()
{
    while(socket_->waitForReadyRead() && socket_->bytesAvailable() < 3)
    {
#ifndef _WIN32
        usleep(10);
#endif
    }

    socket_->read(3);
}
