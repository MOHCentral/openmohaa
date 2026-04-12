/*
===========================================================================
Copyright (C) 2025 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// mqttclient.h -- Minimal embedded MQTT 3.1.1 client for telemetry/stats

#pragma once

#ifdef USE_MQTT

#include <string>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET mqtt_socket_t;
#define MQTT_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int mqtt_socket_t;
#define MQTT_INVALID_SOCKET (-1)
#endif

// MQTT 3.1.1 packet types
enum MqttPacketType {
    MQTT_CONNECT     = 1,
    MQTT_CONNACK     = 2,
    MQTT_PUBLISH     = 3,
    MQTT_PUBACK      = 4,
    MQTT_SUBSCRIBE   = 8,
    MQTT_SUBACK      = 9,
    MQTT_UNSUBSCRIBE = 10,
    MQTT_UNSUBACK    = 11,
    MQTT_PINGREQ     = 12,
    MQTT_PINGRESP    = 13,
    MQTT_DISCONNECT  = 14
};

// MQTT QoS levels
enum MqttQos {
    MQTT_QOS_0 = 0,  // At most once
    MQTT_QOS_1 = 1,  // At least once
    MQTT_QOS_2 = 2   // Exactly once (not implemented)
};

// MQTT CONNACK return codes
enum MqttConnackCode {
    MQTT_CONNACK_ACCEPTED              = 0,
    MQTT_CONNACK_REFUSED_PROTOCOL      = 1,
    MQTT_CONNACK_REFUSED_IDENTIFIER    = 2,
    MQTT_CONNACK_REFUSED_UNAVAILABLE   = 3,
    MQTT_CONNACK_REFUSED_BAD_CREDENTIALS = 4,
    MQTT_CONNACK_REFUSED_NOT_AUTHORIZED  = 5
};

// Received message from a subscription
struct MqttMessage {
    std::string topic;
    std::string payload;
    int         qos;
    bool        retained;
};

class MqttClient {
public:
    MqttClient();
    ~MqttClient();

    // Connection
    bool Connect(const std::string& host, int port, const std::string& clientId,
                 const std::string& username = "", const std::string& passwd = "",
                 int keepAlive = 60);
    void Disconnect();
    bool IsConnected() const;

    // Publish
    bool Publish(const std::string& topic, const std::string& payload, int qos = 0, bool retain = false);

    // Subscribe / Unsubscribe
    bool Subscribe(const std::string& topic, int qos = 0);
    bool Unsubscribe(const std::string& topic);

    // Keepalive
    bool SendPing();

    // Must be called periodically to process incoming data and keepalive
    // Returns received messages
    std::vector<MqttMessage> Poll(int timeoutMs = 0);

    // Error info
    const std::string& GetLastError() const { return m_lastError; }

private:
    // Socket operations
    bool SocketConnect(const std::string& host, int port);
    void SocketClose();
    bool SocketSend(const uint8_t* data, size_t len);
    int  SocketRecv(uint8_t* buf, size_t maxLen, int timeoutMs);

    // MQTT packet building
    std::vector<uint8_t> BuildConnectPacket(const std::string& clientId,
                                             const std::string& username,
                                             const std::string& passwd,
                                             int keepAlive);
    std::vector<uint8_t> BuildPublishPacket(const std::string& topic,
                                             const std::string& payload,
                                             int qos, bool retain, uint16_t packetId);
    std::vector<uint8_t> BuildSubscribePacket(const std::string& topic, int qos, uint16_t packetId);
    std::vector<uint8_t> BuildUnsubscribePacket(const std::string& topic, uint16_t packetId);
    std::vector<uint8_t> BuildPingreqPacket();
    std::vector<uint8_t> BuildDisconnectPacket();
    std::vector<uint8_t> BuildPubackPacket(uint16_t packetId);

    // Encoding helpers
    static void EncodeRemainingLength(std::vector<uint8_t>& buf, uint32_t length);
    static void EncodeString(std::vector<uint8_t>& buf, const std::string& s);

    // Packet parsing
    bool ReadPacket(uint8_t& type, std::vector<uint8_t>& payload, int timeoutMs);
    bool ParsePublish(const uint8_t* data, size_t len, uint8_t flags, MqttMessage& msg, uint16_t& packetId);

    mqtt_socket_t   m_socket;
    bool            m_connected;
    uint16_t        m_packetId;
    int             m_keepAlive;
    std::string     m_lastError;

    // Partial receive buffer
    std::vector<uint8_t> m_recvBuf;
};

#endif // USE_MQTT
