#ifndef REALTIMESUBSCRIPTION_H
#define REALTIMESUBSCRIPTION_H

#include <string>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>

class MarketDataServer;
class WebSocketClient;

class RealTimeSubscription {
public:
    RealTimeSubscription();
    ~RealTimeSubscription();

    void setMarketDataServer(MarketDataServer* server) { m_server = server; }
    void setWebSocketClient(WebSocketClient* wsClient) { m_wsClient = wsClient; }

    void start();
    void stop();

    bool subscribeSymbol(const std::string& channel);
    bool unsubscribeSymbol(const std::string& channel);

    void onMessage(const std::string& rawMessage);

private:
    MarketDataServer* m_server;
    WebSocketClient* m_wsClient;

    std::unordered_set<std::string> m_subscribedChannels;
    std::mutex m_subscribedChannelsMutex;

    std::atomic<bool> m_running;
};

#endif // REALTIMESUBSCRIPTION_H
