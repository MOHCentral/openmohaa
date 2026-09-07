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

// mqttclient.cpp -- Minimal embedded MQTT 3.1.1 client implementation

#include "mqttclient.h"

#ifdef USE_MQTT

#include <cstring>
#include <algorithm>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
static bool s_wsaInit = false;
static void EnsureWSAInit() {
    if (!s_wsaInit) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        s_wsaInit = true;
    }
}
static void CloseSocket(mqtt_socket_t s) { closesocket(s); }
static int GetSocketError() { return WSAGetLastError(); }
#else
static void EnsureWSAInit() {}
static void CloseSocket(mqtt_socket_t s) { close(s); }
#endif

#define MQTT_MAX_PAYLOAD_SIZE (1024 * 1024)  // 1MB limit

MqttClient::MqttClient()
    : m_socket(MQTT_INVALID_SOCKET)
    , m_connected(false)
    , m_packetId(1)
    , m_keepAlive(60)
{
}

MqttClient::~MqttClient()
{
    if (m_connected) {
        Disconnect();
    }
    if (m_socket != MQTT_INVALID_SOCKET) {
        SocketClose();
    }
}

// ============================================================================
// Socket operations
// ============================================================================

bool MqttClient::SocketConnect(const std::string& host, int port)
{
    EnsureWSAInit();

    struct addrinfo hints, *result = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port);

    int rc = getaddrinfo(host.c_str(), portStr, &hints, &result);
    if (rc != 0 || !result) {
        m_lastError = "DNS resolution failed for " + host;
        return false;
    }

    m_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (m_socket == MQTT_INVALID_SOCKET) {
        freeaddrinfo(result);
        m_lastError = "Failed to create socket";
        return false;
    }

    // Set connect timeout via non-blocking + select
#ifdef _WIN32
    u_long nonBlock = 1;
    ioctlsocket(m_socket, FIONBIO, &nonBlock);
#else
    int flags = fcntl(m_socket, F_GETFL, 0);
    fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
#endif

    rc = connect(m_socket, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);

    if (rc != 0) {
#ifdef _WIN32
        if (GetSocketError() != WSAEWOULDBLOCK) {
#else
        if (errno != EINPROGRESS) {
#endif
            m_lastError = "Connect failed";
            SocketClose();
            return false;
        }

        // Wait for connection with 10 second timeout
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(m_socket, &writefds);
        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;

        rc = select((int)m_socket + 1, nullptr, &writefds, nullptr, &tv);
        if (rc <= 0) {
            m_lastError = "Connect timeout";
            SocketClose();
            return false;
        }

        // Check for connection error
        int optval = 0;
        socklen_t optlen = sizeof(optval);
        getsockopt(m_socket, SOL_SOCKET, SO_ERROR, (char*)&optval, &optlen);
        if (optval != 0) {
            m_lastError = "Connect failed after select";
            SocketClose();
            return false;
        }
    }

    // Set back to blocking mode with timeouts
#ifdef _WIN32
    nonBlock = 0;
    ioctlsocket(m_socket, FIONBIO, &nonBlock);
    DWORD timeout = 5000;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    fcntl(m_socket, F_SETFL, flags); // restore original flags (blocking)
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    return true;
}

void MqttClient::SocketClose()
{
    if (m_socket != MQTT_INVALID_SOCKET) {
        CloseSocket(m_socket);
        m_socket = MQTT_INVALID_SOCKET;
    }
}

bool MqttClient::SocketSend(const uint8_t* data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int rc = send(m_socket, (const char*)(data + sent), (int)(len - sent), 0);
        if (rc <= 0) {
            m_lastError = "Socket send failed";
            return false;
        }
        sent += rc;
    }
    return true;
}

int MqttClient::SocketRecv(uint8_t* buf, size_t maxLen, int timeoutMs)
{
    if (timeoutMs >= 0) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_socket, &readfds);
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        int rc = select((int)m_socket + 1, &readfds, nullptr, nullptr, &tv);
        if (rc <= 0) {
            return 0; // Timeout or error
        }
    }

    int rc = recv(m_socket, (char*)buf, (int)maxLen, 0);
    if (rc <= 0) {
        return -1; // Connection closed or error
    }
    return rc;
}

// ============================================================================
// MQTT packet encoding helpers
// ============================================================================

void MqttClient::EncodeRemainingLength(std::vector<uint8_t>& buf, uint32_t length)
{
    do {
        uint8_t encoded = length & 0x7F;
        length >>= 7;
        if (length > 0) {
            encoded |= 0x80;
        }
        buf.push_back(encoded);
    } while (length > 0);
}

void MqttClient::EncodeString(std::vector<uint8_t>& buf, const std::string& s)
{
    uint16_t len = (uint16_t)std::min(s.size(), (size_t)65535);
    buf.push_back((uint8_t)(len >> 8));
    buf.push_back((uint8_t)(len & 0xFF));
    buf.insert(buf.end(), s.begin(), s.begin() + len);
}

// ============================================================================
// MQTT packet builders
// ============================================================================

std::vector<uint8_t> MqttClient::BuildConnectPacket(const std::string& clientId,
                                                      const std::string& username,
                                                      const std::string& passwd,
                                                      int keepAlive)
{
    // Build variable header + payload first to compute remaining length
    std::vector<uint8_t> varHeader;

    // Protocol name "MQTT"
    EncodeString(varHeader, "MQTT");
    // Protocol level (4 = MQTT 3.1.1)
    varHeader.push_back(0x04);

    // Connect flags
    uint8_t flags = 0x02; // Clean session
    if (!username.empty()) {
        flags |= 0x80; // Username flag
        if (!passwd.empty()) {
            flags |= 0x40; // Password flag
        }
    }
    varHeader.push_back(flags);

    // Keep alive
    varHeader.push_back((uint8_t)(keepAlive >> 8));
    varHeader.push_back((uint8_t)(keepAlive & 0xFF));

    // Payload: client ID
    EncodeString(varHeader, clientId);

    // Username and password
    if (!username.empty()) {
        EncodeString(varHeader, username);
        if (!passwd.empty()) {
            EncodeString(varHeader, passwd);
        }
    }

    // Fixed header
    std::vector<uint8_t> packet;
    packet.push_back((MQTT_CONNECT << 4));
    EncodeRemainingLength(packet, (uint32_t)varHeader.size());
    packet.insert(packet.end(), varHeader.begin(), varHeader.end());

    return packet;
}

std::vector<uint8_t> MqttClient::BuildPublishPacket(const std::string& topic,
                                                      const std::string& payload,
                                                      int qos, bool retain, uint16_t packetId)
{
    std::vector<uint8_t> varHeader;
    EncodeString(varHeader, topic);

    if (qos > 0) {
        varHeader.push_back((uint8_t)(packetId >> 8));
        varHeader.push_back((uint8_t)(packetId & 0xFF));
    }

    varHeader.insert(varHeader.end(), payload.begin(), payload.end());

    uint8_t fixedByte = (MQTT_PUBLISH << 4);
    if (qos == 1) fixedByte |= 0x02;
    if (retain) fixedByte |= 0x01;

    std::vector<uint8_t> packet;
    packet.push_back(fixedByte);
    EncodeRemainingLength(packet, (uint32_t)varHeader.size());
    packet.insert(packet.end(), varHeader.begin(), varHeader.end());

    return packet;
}

std::vector<uint8_t> MqttClient::BuildSubscribePacket(const std::string& topic, int qos, uint16_t packetId)
{
    std::vector<uint8_t> varHeader;
    varHeader.push_back((uint8_t)(packetId >> 8));
    varHeader.push_back((uint8_t)(packetId & 0xFF));
    EncodeString(varHeader, topic);
    varHeader.push_back((uint8_t)qos);

    std::vector<uint8_t> packet;
    packet.push_back((MQTT_SUBSCRIBE << 4) | 0x02); // QoS 1 for SUBSCRIBE itself
    EncodeRemainingLength(packet, (uint32_t)varHeader.size());
    packet.insert(packet.end(), varHeader.begin(), varHeader.end());

    return packet;
}

std::vector<uint8_t> MqttClient::BuildUnsubscribePacket(const std::string& topic, uint16_t packetId)
{
    std::vector<uint8_t> varHeader;
    varHeader.push_back((uint8_t)(packetId >> 8));
    varHeader.push_back((uint8_t)(packetId & 0xFF));
    EncodeString(varHeader, topic);

    std::vector<uint8_t> packet;
    packet.push_back((MQTT_UNSUBSCRIBE << 4) | 0x02);
    EncodeRemainingLength(packet, (uint32_t)varHeader.size());
    packet.insert(packet.end(), varHeader.begin(), varHeader.end());

    return packet;
}

std::vector<uint8_t> MqttClient::BuildPingreqPacket()
{
    return { (uint8_t)(MQTT_PINGREQ << 4), 0x00 };
}

std::vector<uint8_t> MqttClient::BuildDisconnectPacket()
{
    return { (uint8_t)(MQTT_DISCONNECT << 4), 0x00 };
}

std::vector<uint8_t> MqttClient::BuildPubackPacket(uint16_t packetId)
{
    return {
        (uint8_t)(MQTT_PUBACK << 4), 0x02,
        (uint8_t)(packetId >> 8), (uint8_t)(packetId & 0xFF)
    };
}

// ============================================================================
// MQTT packet reading
// ============================================================================

bool MqttClient::ReadPacket(uint8_t& type, std::vector<uint8_t>& payload, int timeoutMs)
{
    // Read fixed header byte
    uint8_t headerByte;
    int rc = SocketRecv(&headerByte, 1, timeoutMs);
    if (rc <= 0) {
        return false;
    }

    type = headerByte;

    // Read remaining length (variable-length encoding)
    uint32_t remainingLength = 0;
    uint32_t multiplier = 1;
    for (int i = 0; i < 4; i++) {
        uint8_t encoded;
        rc = SocketRecv(&encoded, 1, 1000);
        if (rc <= 0) {
            m_lastError = "Failed to read remaining length";
            return false;
        }
        remainingLength += (encoded & 0x7F) * multiplier;
        if ((encoded & 0x80) == 0) break;
        multiplier *= 128;
    }

    if (remainingLength > MQTT_MAX_PAYLOAD_SIZE) {
        m_lastError = "Packet too large";
        return false;
    }

    // Read payload
    payload.resize(remainingLength);
    if (remainingLength > 0) {
        size_t received = 0;
        while (received < remainingLength) {
            rc = SocketRecv(payload.data() + received, remainingLength - received, 5000);
            if (rc <= 0) {
                m_lastError = "Failed to read packet payload";
                return false;
            }
            received += rc;
        }
    }

    return true;
}

bool MqttClient::ParsePublish(const uint8_t* data, size_t len, uint8_t flags, MqttMessage& msg, uint16_t& packetId)
{
    if (len < 2) return false;

    size_t pos = 0;

    // Topic length
    uint16_t topicLen = (data[pos] << 8) | data[pos + 1];
    pos += 2;
    if (pos + topicLen > len) return false;

    msg.topic = std::string((const char*)(data + pos), topicLen);
    pos += topicLen;

    // QoS from flags
    msg.qos = (flags >> 1) & 0x03;
    msg.retained = (flags & 0x01) != 0;

    // Packet ID (only for QoS > 0)
    packetId = 0;
    if (msg.qos > 0) {
        if (pos + 2 > len) return false;
        packetId = (data[pos] << 8) | data[pos + 1];
        pos += 2;
    }

    // Remaining data is payload
    if (pos < len) {
        msg.payload = std::string((const char*)(data + pos), len - pos);
    }

    return true;
}

// ============================================================================
// Public API
// ============================================================================

bool MqttClient::Connect(const std::string& host, int port, const std::string& clientId,
                          const std::string& username, const std::string& passwd,
                          int keepAlive)
{
    if (m_connected) {
        Disconnect();
    }

    m_keepAlive = keepAlive;

    // TCP connect
    if (!SocketConnect(host, port)) {
        return false;
    }

    // Send CONNECT packet
    auto packet = BuildConnectPacket(clientId, username, passwd, keepAlive);
    if (!SocketSend(packet.data(), packet.size())) {
        SocketClose();
        return false;
    }

    // Wait for CONNACK
    uint8_t type;
    std::vector<uint8_t> payload;
    if (!ReadPacket(type, payload, 10000)) {
        m_lastError = "No CONNACK received";
        SocketClose();
        return false;
    }

    if ((type >> 4) != MQTT_CONNACK || payload.size() < 2) {
        m_lastError = "Invalid CONNACK response";
        SocketClose();
        return false;
    }

    uint8_t returnCode = payload[1];
    if (returnCode != MQTT_CONNACK_ACCEPTED) {
        switch (returnCode) {
        case MQTT_CONNACK_REFUSED_PROTOCOL:
            m_lastError = "Connection refused: unacceptable protocol version";
            break;
        case MQTT_CONNACK_REFUSED_IDENTIFIER:
            m_lastError = "Connection refused: identifier rejected";
            break;
        case MQTT_CONNACK_REFUSED_UNAVAILABLE:
            m_lastError = "Connection refused: server unavailable";
            break;
        case MQTT_CONNACK_REFUSED_BAD_CREDENTIALS:
            m_lastError = "Connection refused: bad username or password";
            break;
        case MQTT_CONNACK_REFUSED_NOT_AUTHORIZED:
            m_lastError = "Connection refused: not authorized";
            break;
        default:
            m_lastError = "Connection refused: unknown error";
            break;
        }
        SocketClose();
        return false;
    }

    m_connected = true;
    return true;
}

void MqttClient::Disconnect()
{
    if (m_connected && m_socket != MQTT_INVALID_SOCKET) {
        auto packet = BuildDisconnectPacket();
        SocketSend(packet.data(), packet.size());
        m_connected = false;
    }
    SocketClose();
}

bool MqttClient::IsConnected() const
{
    return m_connected && m_socket != MQTT_INVALID_SOCKET;
}

bool MqttClient::Publish(const std::string& topic, const std::string& payload, int qos, bool retain)
{
    if (!IsConnected()) {
        m_lastError = "Not connected";
        return false;
    }

    if (topic.empty()) {
        m_lastError = "Empty topic";
        return false;
    }

    uint16_t packetId = (qos > 0) ? m_packetId++ : 0;
    auto packet = BuildPublishPacket(topic, payload, qos, retain, packetId);

    if (!SocketSend(packet.data(), packet.size())) {
        m_connected = false;
        return false;
    }

    // For QoS 1, wait for PUBACK
    if (qos == 1) {
        uint8_t type;
        std::vector<uint8_t> respPayload;
        if (!ReadPacket(type, respPayload, 5000) || (type >> 4) != MQTT_PUBACK) {
            m_lastError = "No PUBACK received";
            return false;
        }
    }

    return true;
}

bool MqttClient::Subscribe(const std::string& topic, int qos)
{
    if (!IsConnected()) {
        m_lastError = "Not connected";
        return false;
    }

    uint16_t packetId = m_packetId++;
    auto packet = BuildSubscribePacket(topic, qos, packetId);

    if (!SocketSend(packet.data(), packet.size())) {
        m_connected = false;
        return false;
    }

    // Wait for SUBACK
    uint8_t type;
    std::vector<uint8_t> respPayload;
    if (!ReadPacket(type, respPayload, 5000) || (type >> 4) != MQTT_SUBACK) {
        m_lastError = "No SUBACK received";
        return false;
    }

    return true;
}

bool MqttClient::Unsubscribe(const std::string& topic)
{
    if (!IsConnected()) {
        m_lastError = "Not connected";
        return false;
    }

    uint16_t packetId = m_packetId++;
    auto packet = BuildUnsubscribePacket(topic, packetId);

    if (!SocketSend(packet.data(), packet.size())) {
        m_connected = false;
        return false;
    }

    // Wait for UNSUBACK
    uint8_t type;
    std::vector<uint8_t> respPayload;
    if (!ReadPacket(type, respPayload, 5000) || (type >> 4) != MQTT_UNSUBACK) {
        m_lastError = "No UNSUBACK received";
        return false;
    }

    return true;
}

bool MqttClient::SendPing()
{
    if (!IsConnected()) {
        return false;
    }
    auto packet = BuildPingreqPacket();
    return SocketSend(packet.data(), packet.size());
}

std::vector<MqttMessage> MqttClient::Poll(int timeoutMs)
{
    std::vector<MqttMessage> messages;

    if (!IsConnected()) {
        return messages;
    }

    // Check for incoming data
    uint8_t typeByte;
    std::vector<uint8_t> payload;

    while (ReadPacket(typeByte, payload, timeoutMs)) {
        uint8_t type = typeByte >> 4;

        switch (type) {
        case MQTT_PUBLISH:
            {
                MqttMessage msg;
                uint16_t packetId;
                if (ParsePublish(payload.data(), payload.size(), typeByte & 0x0F, msg, packetId)) {
                    // Send PUBACK for QoS 1
                    if (msg.qos == 1 && packetId > 0) {
                        auto puback = BuildPubackPacket(packetId);
                        SocketSend(puback.data(), puback.size());
                    }
                    messages.push_back(std::move(msg));
                }
            }
            break;

        case MQTT_PINGRESP:
            // Keepalive response, ignore
            break;

        case MQTT_PINGREQ:
            {
                // Respond to ping (unusual but valid)
                uint8_t resp[] = { (uint8_t)(MQTT_PINGRESP << 4), 0x00 };
                SocketSend(resp, 2);
            }
            break;

        default:
            // Ignore other packet types
            break;
        }

        // Only block on first iteration
        timeoutMs = 0;
    }

    return messages;
}

#endif // USE_MQTT
