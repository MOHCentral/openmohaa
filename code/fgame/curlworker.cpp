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

#include "g_local.h"
#include "curlworker.h"

#ifdef USE_HTTP

CurlWorker g_CurlWorker;

#define MAX_CURL_RESPONSE_SIZE (1024 * 1024) // 1MB limit

CurlWorker::CurlWorker() : m_Running(false) {
}

CurlWorker::~CurlWorker() {
    Stop();
}

void CurlWorker::Start() {
    if (m_Running) {
        return;
    }
    m_Running = true;
    m_Thread = std::thread(&CurlWorker::WorkLoop, this);
}

void CurlWorker::Stop() {
    if (!m_Running) {
        return;
    }
    m_Running = false;
    if (m_Thread.joinable()) {
        m_Thread.join();
    }
}

void CurlWorker::AddTask(const CurlTask& task) {
    std::lock_guard<std::mutex> lock(m_QueueMutex);
    m_RequestQueue.push(task);
}

bool CurlWorker::GetResult(CurlResult& result) {
    std::lock_guard<std::mutex> lock(m_ResultMutex);
    if (m_ResultQueue.empty()) {
        return false;
    }
    result = m_ResultQueue.front();
    m_ResultQueue.pop();
    return true;
}

size_t CurlWorker::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* mem = (std::string*)userp;

    if (mem->size() + realsize > MAX_CURL_RESPONSE_SIZE) {
        return 0; // Abort download
    }

    mem->append((char*)contents, realsize);
    return realsize;
}

int CurlWorker::ProgressCallback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
    CurlWorker* worker = (CurlWorker*)clientp;
    if (!worker->m_Running) {
        return 1; // Abort transfer
    }
    return 0;
}

void CurlWorker::WorkLoop() {
    while (m_Running) {
        CurlTask task;
        bool hasTask = false;

        {
            std::lock_guard<std::mutex> lock(m_QueueMutex);
            if (!m_RequestQueue.empty()) {
                task = m_RequestQueue.front();
                m_RequestQueue.pop();
                hasTask = true;
            }
        }

        if (hasTask) {
            CURL* curl;
            CURLcode res;
            CurlResult result;
            result.callbackLabel = task.callbackLabel;
            result.sourceScript = task.sourceScript;
            result.success = false;
            result.httpCode = 0;

            curl = curl_easy_init();
            if (curl) {
                curl_easy_setopt(curl, CURLOPT_URL, task.url.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&result.data);

                // Set progress callback for faster shutdown
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, ProgressCallback);
                curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, this);

                // Set timeouts to prevent hanging threads forever
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
                curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

                // Timeout override
                if (task.timeout > 0) {
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, task.timeout);
                }

                // Headers
                struct curl_slist *chunk = NULL;
                for(const auto& header : task.headers) {
                    chunk = curl_slist_append(chunk, header.c_str());
                }
                if (chunk) {
                    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
                }

                // Follow redirects
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

                // Custom method
                if (!task.customMethod.empty()) {
                     curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, task.customMethod.c_str());
                }

                if (task.isPost || !task.postData.empty()) {
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, task.postData.c_str());
                }

                res = curl_easy_perform(curl);

                if (chunk) {
                    curl_slist_free_all(chunk);
                }

                if (res == CURLE_OK) {
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.httpCode);
                    result.success = (result.httpCode >= 200 && result.httpCode < 300);
                } else {
                    result.success = false;
                    result.data = curl_easy_strerror(res);
                }

                curl_easy_cleanup(curl);
            }

            {
                std::lock_guard<std::mutex> lock(m_ResultMutex);
                m_ResultQueue.push(result);
            }
        } else {
            // Sleep to avoid busy loop
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

#endif // USE_HTTP
