/*
 * E-Lib
 * Copyright (C) 2019 EnAccess
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "cicada/commdevices/simcommdevice.h"
#include "cicada/commdevices/ipcommdevice.h"
#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <cstring>

#ifdef CICADA_DEBUG
#include "printf.h"
#endif

#define MIN_SPACE_AVAILABLE 22

using namespace Cicada;

const char* SimCommDevice::_okStr = "OK";
const char* SimCommDevice::_lineEndStr = "\r\n";
const char* SimCommDevice::_quoteEndStr = "\"\r\n";

SimCommDevice::SimCommDevice(IBufferedSerial& serial) :
    _serial(serial),
    _apn(NULL),
    _lbFill(0),
    _sendState(0),
    _replyState(0),
    _bytesToWrite(0),
    _bytesToReceive(0),
    _bytesToRead(0),
    _rssi(99)
{}

void SimCommDevice::setApn(const char* apn)
{
    _apn = apn;
}

bool SimCommDevice::connect()
{
    if (_apn == NULL)
        return false;

    return IPCommDevice::connect();
}

bool SimCommDevice::serialLock()
{
    if (_waitForReply || _replyState != 0)
        return false;

    _stateBooleans |= SERIAL_LOCKED;
    return true;
}

void SimCommDevice::serialUnlock()
{
    _stateBooleans &= ~SERIAL_LOCKED;
}

Size SimCommDevice::serialWrite(char* data)
{
    if (_stateBooleans & SERIAL_LOCKED) {
        return _serial.write((const uint8_t*)data);
    }

    return 0;
}

Size SimCommDevice::serialRead(char* data, Size maxSize)
{
    if (_stateBooleans & SERIAL_LOCKED) {
        return _serial.read((uint8_t*)data, maxSize);
    }

    return 0;
}

bool SimCommDevice::fillLineBuffer()
{
    // Buffer reply from modem in line buffer
    // Returns true when enough data to be parsed is available.
    if (_stateBooleans & LINE_READ) {
        while (_serial.bytesAvailable()) {
            char c = _serial.read();
            _lineBuffer[_lbFill++] = c;
            if (c == '\n' || c == '>' || _lbFill == LINE_MAX_LENGTH) {
                _lineBuffer[_lbFill] = '\0';
                _lbFill = 0;
                return true;
            }
        }
    }
    return false;
}

void SimCommDevice::logStates(int8_t sendState, int8_t replyState)
{
#ifdef CICADA_DEBUG
    if (_connectState < connected) {
        if (_waitForReply)
            printf("_sendState=%d, _replyState=%d, "
                   "_waitForReply=\"%s\", data: %s",
                sendState, replyState, _waitForReply, _lineBuffer);
        else
            printf("_sendState=%d, _replyState=%d, "
                   "_waitForReply=NULL, data: %s",
                sendState, replyState, _lineBuffer);
    }
#endif
}

bool SimCommDevice::parseDnsReply()
{
    if (strncmp(_lineBuffer, "+CDNSGIP: 1", 11) == 0) {
        char* tmpStr;
        uint8_t i = 0, q = 0;

        // Validate DNS reply string
        while (_lineBuffer[i]) {
            if (_lineBuffer[i++] == '\"')
                q++;
        }
        if (q < 4 || q > 10) {
            // Error in input string
            _connectState = dnsError;
            return false;
        }
        i = 0, q = 0;

        // Parse IP address
        while (q < 3)
            if (_lineBuffer[i++] == '\"')
                q++;
        tmpStr = _lineBuffer + i;
        while (_lineBuffer[i]) {
            if (_lineBuffer[i] == '\"')
                _lineBuffer[i] = '\0';
            i++;
        }
        strcpy(_ip, tmpStr);
        return true;
    } else if (strncmp(_lineBuffer, "+CDNSGIP: 0", 11) == 0) {
        _stateBooleans |= RESET_PENDING;
    }

    return false;
}

bool SimCommDevice::parseCiprxget4()
{
    if (strncmp(_lineBuffer, "+CIPRXGET: 4,0,", 15) == 0) {
        int bytesToReceive;
        sscanf(_lineBuffer + 15, "%d", &bytesToReceive);
        _bytesToReceive += bytesToReceive;
        return true;
    }
    return false;
}

bool SimCommDevice::parseCiprxget2()
{
    if (strncmp(_lineBuffer, "+CIPRXGET: 2,0,", 15) == 0) {
        int bytesToReceive;
        sscanf(_lineBuffer + 15, "%d", &bytesToReceive);
        _bytesToReceive -= bytesToReceive;
        _bytesToRead += bytesToReceive;
        _stateBooleans &= ~LINE_READ;
        return true;
    }
    return false;
}

bool SimCommDevice::parseCsq()
{
    if (strncmp(_lineBuffer, "+CSQ: ", 6) == 0) {
        unsigned int rssi;
        if (sscanf(_lineBuffer + 6, "%u", &rssi) == 1) {
            _rssi = rssi;
        }
        return true;
    }
    return false;
}

void SimCommDevice::flushReadBuffer()
{
    while (_bytesToRead && _serial.bytesAvailable()) {
        _serial.read();
        _bytesToRead--;
    }
    _bytesToReceive = 0;

    if (_bytesToRead == 0) {
        _stateBooleans |= LINE_READ;
    }
}

bool SimCommDevice::handleDisconnect(int8_t nextState)
{
    if (_stateBooleans & DISCONNECT_PENDING) {
        _stateBooleans &= ~DISCONNECT_PENDING;
        _sendState = nextState;

        return true;
    }

    return false;
}

bool SimCommDevice::handleConnect(int8_t nextState)
{
    if (_stateBooleans & CONNECT_PENDING) {
        _stateBooleans &= ~CONNECT_PENDING;
        _sendState = nextState;

        return true;
    }

    return false;
}

bool SimCommDevice::sendDnsQuery()
{
    if (_serial.spaceAvailable() < strlen(_host) + 20)
        return false;

    _serial.write((const uint8_t*)"AT+CDNSGIP=\"");
    _serial.write((const uint8_t*)_host);
    _serial.write((const uint8_t*)_quoteEndStr);

    return true;
}

void SimCommDevice::sendCipstart(const char* variant)
{
    char portStr[6];
    sprintf(portStr, "%d", _port);

    _serial.write((const uint8_t*)"AT+CIP");
    _serial.write((const uint8_t*)variant);
    _serial.write((const uint8_t*)"=0,\"TCP\",\"");
    _serial.write((const uint8_t*)_ip);
    _serial.write((const uint8_t*)"\",");
    _serial.write((const uint8_t*)portStr);
    _serial.write((const uint8_t*)_lineEndStr);
}

bool SimCommDevice::prepareSending()
{
    if (_serial.spaceAvailable() < 22)
        return false;

    _bytesToWrite = _writeBuffer.bytesAvailable();
    if (_bytesToWrite > _serial.spaceAvailable() - MIN_SPACE_AVAILABLE) {
        _bytesToWrite = _serial.spaceAvailable() - MIN_SPACE_AVAILABLE;
    }

    char sizeStr[6];
    sprintf(sizeStr, "%u", (unsigned int)_bytesToWrite);

    _serial.write((const uint8_t*)"AT+CIPSEND=0,");
    _serial.write((const uint8_t*)sizeStr);
    _serial.write((const uint8_t*)_lineEndStr);

    _waitForReply = ">";

    return true;
}

void SimCommDevice::sendData()
{
    while (_bytesToWrite--) {
        _serial.write(_writeBuffer.pull());
    }
}

bool SimCommDevice::sendCiprxget2()
{
    if (_serial.spaceAvailable() > 8 && _readBuffer.spaceAvailable() > 0) {
        Size bytesToReceive = _serial.spaceAvailable() - 8;
        if (bytesToReceive > _bytesToReceive)
            bytesToReceive = _bytesToReceive;
        if (bytesToReceive > _readBuffer.spaceAvailable())
            bytesToReceive = _readBuffer.spaceAvailable();

        const char str[] = "AT+CIPRXGET=2,0,";
        char sizeStr[6];
        sprintf(sizeStr, "%u", (unsigned int)bytesToReceive);
        _serial.write((const uint8_t*)str, sizeof(str) - 1);
        _serial.write((const uint8_t*)sizeStr);
        _serial.write((const uint8_t*)_lineEndStr);
        return true;
    } else {
        return false;
    }
}

void SimCommDevice::checkConnectionState(const char* closeVariant)
{
    if (strncmp(_lineBuffer, "+CIPRXGET: 1,0", 14) == 0) {
        _stateBooleans |= DATA_PENDING;
    } else if (strncmp(_lineBuffer, closeVariant, strlen(closeVariant)) == 0) {
        _waitForReply = NULL;
        _stateBooleans &= ~IP_CONNECTED;
    }
}

bool SimCommDevice::receive()
{
    if (_serial.bytesAvailable() >= _bytesToRead) {
        while (_bytesToRead) {
            _readBuffer.push(_serial.read());
            _bytesToRead--;
        }
        _stateBooleans |= LINE_READ;

        return true;
    } else {
        return false;
    }
}

void SimCommDevice::sendCommand(const char* cmd)
{
    _serial.write((const uint8_t*)cmd);
    _serial.write((const uint8_t*)_lineEndStr);
}

void SimCommDevice::requestRSSI()
{
    _rssi = UINT8_MAX;
}

uint8_t SimCommDevice::getRSSI()
{
    return _rssi;
}
