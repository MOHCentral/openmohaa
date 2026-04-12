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

// mqttworker.h -- Async MQTT worker thread for game script commands

#pragma once

#ifdef USE_MQTT

#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <string>
#include <vector>
#include <map>

// Task types for the worker thread
enum MqttTaskType {
    MQTT_TASK_CONNECT,
    MQTT_TASK_DISCONNECT,
    MQTT_TASK_PUBLISH,
    MQTT_TASK_SUBSCRIBE,
    MQTT_TASK_UNSUBSCRIBE,
    MQTT_TASK_SET_AUTH
};

struct MqttTask {
    MqttTaskType type;

    // Connection params
    std::string host;
    int         port;
    std::string clientId;
    std::string username;
    std::string password;
    int         keepAlive;

    // Publish params
    std::string topic;
    std::string payload;
    int         qos;
    bool        retain;

    // Callback params
    std::string sourceScript;
    std::string callbackLabel;
};

// Result types sent back to game thread
enum MqttResultType {
    MQTT_RESULT_CONNECT,
    MQTT_RESULT_DISCONNECT,
    MQTT_RESULT_PUBLISH,
    MQTT_RESULT_SUBSCRIBE,
    MQTT_RESULT_MESSAGE,   // Received message from subscription
    MQTT_RESULT_ERROR
};

struct MqttResult {
    MqttResultType type;
    bool           success;
    std::string    errorMessage;

    // For MQTT_RESULT_MESSAGE
    std::string topic;
    std::string payload;

    // For callbacks
    std::string sourceScript;
    std::string callbackLabel;
};

class MqttWorker {
public:
    MqttWorker();
    ~MqttWorker();

    void Start();
    void Stop();

    void AddTask(const MqttTask& task);
    bool GetResult(MqttResult& result);

    bool IsConnected() const { return m_MqttConnected; }

private:
    void WorkLoop();

    std::thread m_Thread;
    std::mutex  m_QueueMutex;
    std::queue<MqttTask> m_TaskQueue;

    std::mutex  m_ResultMutex;
    std::queue<MqttResult> m_ResultQueue;

    std::atomic<bool> m_Running;
    std::atomic<bool> m_MqttConnected;

    // Subscription callback mapping: topic -> (sourceScript, callbackLabel)
    std::map<std::string, std::pair<std::string, std::string>> m_Subscriptions;
};

extern MqttWorker g_MqttWorker;

#endif // USE_MQTT
