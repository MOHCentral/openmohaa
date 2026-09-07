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

// mqttworker.cpp -- Async MQTT worker thread implementation

#include "g_local.h"
#include "mqttworker.h"

#ifdef USE_MQTT

#include "mqttclient.h"
#include <chrono>

MqttWorker g_MqttWorker;

MqttWorker::MqttWorker()
    : m_Running(false)
    , m_MqttConnected(false)
{
}

MqttWorker::~MqttWorker()
{
    Stop();
}

void MqttWorker::Start()
{
    if (m_Running) {
        return;
    }
    m_Running = true;
    m_Thread = std::thread(&MqttWorker::WorkLoop, this);
}

void MqttWorker::Stop()
{
    if (!m_Running) {
        return;
    }
    m_Running = false;
    if (m_Thread.joinable()) {
        m_Thread.join();
    }
}

void MqttWorker::AddTask(const MqttTask& task)
{
    std::lock_guard<std::mutex> lock(m_QueueMutex);
    m_TaskQueue.push(task);
}

bool MqttWorker::GetResult(MqttResult& result)
{
    std::lock_guard<std::mutex> lock(m_ResultMutex);
    if (m_ResultQueue.empty()) {
        return false;
    }
    result = m_ResultQueue.front();
    m_ResultQueue.pop();
    return true;
}

void MqttWorker::WorkLoop()
{
    MqttClient client;
    auto lastPing = std::chrono::steady_clock::now();
    int keepAliveSeconds = 60;

    while (m_Running) {
        MqttTask task;
        bool hasTask = false;

        {
            std::lock_guard<std::mutex> lock(m_QueueMutex);
            if (!m_TaskQueue.empty()) {
                task = m_TaskQueue.front();
                m_TaskQueue.pop();
                hasTask = true;
            }
        }

        if (hasTask) {
            MqttResult result;
            result.sourceScript = task.sourceScript;
            result.callbackLabel = task.callbackLabel;

            switch (task.type) {
            case MQTT_TASK_CONNECT:
                {
                    result.type = MQTT_RESULT_CONNECT;
                    keepAliveSeconds = task.keepAlive > 0 ? task.keepAlive : 60;

                    if (client.Connect(task.host, task.port, task.clientId,
                                       task.username, task.password, keepAliveSeconds)) {
                        result.success = true;
                        m_MqttConnected = true;
                        lastPing = std::chrono::steady_clock::now();
                    } else {
                        result.success = false;
                        result.errorMessage = client.GetLastError();
                        m_MqttConnected = false;
                    }
                }
                break;

            case MQTT_TASK_DISCONNECT:
                {
                    result.type = MQTT_RESULT_DISCONNECT;
                    client.Disconnect();
                    result.success = true;
                    m_MqttConnected = false;
                    m_Subscriptions.clear();
                }
                break;

            case MQTT_TASK_PUBLISH:
                {
                    result.type = MQTT_RESULT_PUBLISH;
                    if (client.Publish(task.topic, task.payload, task.qos, task.retain)) {
                        result.success = true;
                    } else {
                        result.success = false;
                        result.errorMessage = client.GetLastError();
                        if (!client.IsConnected()) {
                            m_MqttConnected = false;
                        }
                    }
                }
                break;

            case MQTT_TASK_SUBSCRIBE:
                {
                    result.type = MQTT_RESULT_SUBSCRIBE;
                    if (client.Subscribe(task.topic, task.qos)) {
                        result.success = true;
                        // Store subscription callback mapping
                        m_Subscriptions[task.topic] = std::make_pair(task.sourceScript, task.callbackLabel);
                    } else {
                        result.success = false;
                        result.errorMessage = client.GetLastError();
                        if (!client.IsConnected()) {
                            m_MqttConnected = false;
                        }
                    }
                }
                break;

            case MQTT_TASK_UNSUBSCRIBE:
                {
                    result.type = MQTT_RESULT_SUBSCRIBE;
                    if (client.Unsubscribe(task.topic)) {
                        result.success = true;
                        m_Subscriptions.erase(task.topic);
                    } else {
                        result.success = false;
                        result.errorMessage = client.GetLastError();
                    }
                }
                break;

            case MQTT_TASK_SET_AUTH:
                // Auth is set at connect time, just store for next connect
                // This is handled by storing in the task params
                continue;
            }

            if (!task.callbackLabel.empty()) {
                std::lock_guard<std::mutex> lock(m_ResultMutex);
                m_ResultQueue.push(result);
            }
        }

        // Poll for incoming messages if connected
        if (client.IsConnected()) {
            auto messages = client.Poll(hasTask ? 0 : 50);
            for (auto& msg : messages) {
                MqttResult result;
                result.type = MQTT_RESULT_MESSAGE;
                result.success = true;
                result.topic = msg.topic;
                result.payload = msg.payload;

                // Find the subscription callback for this topic
                // Simple exact match first, then try prefix matching for wildcards
                auto it = m_Subscriptions.find(msg.topic);
                if (it != m_Subscriptions.end()) {
                    result.sourceScript = it->second.first;
                    result.callbackLabel = it->second.second;
                } else {
                    // Try wildcard matching: find any subscription that could match
                    for (auto& sub : m_Subscriptions) {
                        const std::string& filter = sub.first;
                        // Simple # wildcard: "prefix/#" matches "prefix/anything"
                        if (filter.size() >= 2 && filter[filter.size()-1] == '#') {
                            std::string prefix = filter.substr(0, filter.size() - 1);
                            if (msg.topic.compare(0, prefix.size(), prefix) == 0) {
                                result.sourceScript = sub.second.first;
                                result.callbackLabel = sub.second.second;
                                break;
                            }
                        }
                    }
                }

                if (!result.callbackLabel.empty()) {
                    std::lock_guard<std::mutex> lock(m_ResultMutex);
                    m_ResultQueue.push(result);
                }
            }

            // Keepalive ping
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastPing).count();
            if (elapsed >= (keepAliveSeconds / 2)) {
                client.SendPing();
                lastPing = now;
            }
        } else if (!hasTask) {
            // Not connected and no tasks, sleep to avoid busy loop
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Clean shutdown
    if (client.IsConnected()) {
        client.Disconnect();
    }
    m_MqttConnected = false;
}

#endif // USE_MQTT
