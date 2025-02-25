#include "RealTimeSubscription.h"
#include "MarketDataServer.h"
#include "WebSocketClient.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <cstdlib>
#include <ctime>

using json = nlohmann::json;

RealTimeSubscription::RealTimeSubscription()
    : m_server(nullptr)
    , m_wsClient(&WebSocketClient::getInstance())
    , m_running(false)
{
    std::srand(static_cast<unsigned>(std::time(nullptr)));
}

RealTimeSubscription::~RealTimeSubscription()
{
    stop();
}

void RealTimeSubscription::start()
{
    if (!m_wsClient) {
        std::cerr << "[RealTimeSubscription] No WebSocketClient. Cannot start.\n";
        return;
    }
    m_running = true;

    // Use addMessageHandler so we don't overwrite other handlers
    m_wsClient->addMessageHandler([this](const std::string& msg) {
        onMessage(msg);
        });

    std::cout << "[RealTimeSubscription] Started.\n";
}

void RealTimeSubscription::stop()
{
    m_running = false;

    {
        std::lock_guard<std::mutex> lock(m_subscribedChannelsMutex);
        for (const auto& channel : m_subscribedChannels) {
            unsubscribeSymbol(channel);
        }
        m_subscribedChannels.clear();
    }

    std::cout << "[RealTimeSubscription] Stopped.\n";
}

bool RealTimeSubscription::subscribeSymbol(const std::string& channel)
{
    if (!m_wsClient) {
        std::cerr << "[RealTimeSubscription] No WebSocketClient. Can't subscribe.\n";
        return false;
    }

    int requestId = m_wsClient->generateRequestId();
    json request = {
        {"jsonrpc", "2.0"},
        {"method",  "private/subscribe"},
        {"id",      requestId},
        {"params",  {{"channels", {channel}}}}
    };

    json response = m_wsClient->sendBlockingRequest(request, 5);
    if (response.empty()) {
        std::cerr << "[RealTimeSubscription] Subscribe timeout/failure for " << channel << "\n";
        return false;
    }
    if (response.contains("error")) {
        std::cerr << "[RealTimeSubscription] Subscribe error: " << response["error"].dump() << "\n";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_subscribedChannelsMutex);
        m_subscribedChannels.insert(channel);
    }
    std::cout << "[RealTimeSubscription] Subscribed to " << channel << "\n";
    return true;
}

bool RealTimeSubscription::unsubscribeSymbol(const std::string& channel)
{
    if (!m_wsClient) {
        std::cerr << "[RealTimeSubscription] No WebSocketClient. Can't unsubscribe.\n";
        return false;
    }

    int requestId = m_wsClient->generateRequestId();
    json request = {
        {"jsonrpc", "2.0"},
        {"method",  "private/unsubscribe"},
        {"id",      requestId},
        {"params",  {{"channels", {channel}}}}
    };

    json response = m_wsClient->sendBlockingRequest(request, 5);
    if (response.empty()) {
        std::cerr << "[RealTimeSubscription] Unsubscribe timeout/failure for " << channel << "\n";
        return false;
    }
    if (response.contains("error")) {
        std::cerr << "[RealTimeSubscription] Unsubscribe error: " << response["error"].dump() << "\n";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_subscribedChannelsMutex);
        m_subscribedChannels.erase(channel);
    }
    std::cout << "[RealTimeSubscription] Unsubscribed from " << channel << "\n";
    return true;
}

void RealTimeSubscription::onMessage(const std::string& rawMessage)
{
    if (!m_running) {
        return;
    }
    try {
        json incoming = json::parse(rawMessage);
        if (incoming.contains("method") && incoming["method"] == "subscription") {
            if (incoming["params"].contains("channel")) {
                std::string channel = incoming["params"]["channel"].get<std::string>();
                if (incoming["params"].contains("data")) {
                    json data = incoming["params"]["data"];
                    if (m_server) {
                        m_server->sendUpdateToClients(channel, data);
                    }
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[RealTimeSubscription] onMessage parse error: " << e.what()
            << "\nRaw: " << rawMessage << "\n";
    }
}
