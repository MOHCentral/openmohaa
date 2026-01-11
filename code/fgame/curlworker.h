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

#pragma once

#ifdef USE_HTTP

#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <string>
#include <curl/curl.h>

struct CurlTask {
    std::string url;
    std::string postData;
    std::string callbackLabel;
    bool isPost;
};

struct CurlResult {
    std::string data;
    std::string callbackLabel;
    bool success;
    long httpCode;
};

class CurlWorker {
public:
    CurlWorker();
    ~CurlWorker();

    void Start();
    void Stop();

    void AddTask(const CurlTask& task);
    bool GetResult(CurlResult& result);

private:
    void WorkLoop();
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static int ProgressCallback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow);

    std::thread m_Thread;
    std::mutex m_QueueMutex;
    std::queue<CurlTask> m_RequestQueue;

    std::mutex m_ResultMutex;
    std::queue<CurlResult> m_ResultQueue;

    std::atomic<bool> m_Running;
};

extern CurlWorker g_CurlWorker;

#endif // USE_HTTP
