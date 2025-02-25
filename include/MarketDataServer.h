#ifndef MARKETDATASERVER_H
#define MARKETDATASERVER_H

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <nlohmann/json.hpp>
#include <set>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>

#include "RealTimeSubscription.h"

struct ConnectionHash {
    std::size_t operator()(const websocketpp::connection_hdl& hdl) const {
        auto ptr = hdl.lock().get();
        return std::hash<std::uintptr_t>()(reinterpret_cast<std::uintptr_t>(ptr));
    }
};

struct ConnectionEqual {
    bool operator()(const websocketpp::connection_hdl& a, const websocketpp::connection_hdl& b) const {
        return a.lock().get() == b.lock().get();
    }
};

class MarketDataServer {
public:
    typedef websocketpp::server<websocketpp::config::asio> server;

    // *** CHANGED *** (Thread mgmt) default io_threads = hardware_concurrency
    MarketDataServer(uint16_t port, RealTimeSubscription* realTimeSub,
        size_t io_threads = std::thread::hardware_concurrency());
    ~MarketDataServer();

    void run();
    void stop();

    // Called by RealTimeSubscription to forward real-time data
    void sendUpdateToClients(const std::string& channel, const nlohmann::json& data);

    // If OrderManagement wants to forward a message, it can call onMessage():
    void onMessage(const std::string& rawMessage) {}

private:
    server m_server;
    std::vector<std::thread> m_asioThreads;
    size_t m_ioThreads;
    uint16_t m_port;

    RealTimeSubscription* m_realTimeSubscription;

    // Active connections
    std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> m_connections;
    std::mutex m_connectionsMutex;

    // Valid keys
    std::unordered_set<std::string> m_validApiKeys;
    std::mutex m_apiKeysMutex;

    // Channel subscription
    std::unordered_map<std::string, std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>>> m_channelSubscribers;
    std::unordered_map<websocketpp::connection_hdl, std::set<std::string>, ConnectionHash, ConnectionEqual> m_clientSubscriptions;
    std::mutex m_subscriptionsMutex;

    // WebSocket handlers
    void on_open(websocketpp::connection_hdl hdl);
    void on_close(websocketpp::connection_hdl hdl);
    void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg);
    void on_fail(websocketpp::connection_hdl hdl);

    bool authenticateClient(const std::string& apiKey);
    void addSubscription(websocketpp::connection_hdl hdl, const std::string& channel);
    void removeSubscription(websocketpp::connection_hdl hdl, const std::string& channel);
};

#endif // MARKETDATASERVER_H
