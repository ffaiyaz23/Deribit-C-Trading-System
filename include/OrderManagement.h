#ifndef ORDERMANAGEMENT_H
#define ORDERMANAGEMENT_H

#include "WebSocketClient.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <memory>
#include <string>

class RealTimeSubscription;

class OrderManagement {
public:
    OrderManagement();
    ~OrderManagement();

    void placeOrder(const std::string& instrumentName,
        int amount,
        const std::string& type,
        const std::string& label,
        double price);

    void cancelOrder();
    void modifyOrder();
    void viewCurrentPositions(const std::string& currency, const std::string& kind);
    void getOrderBook(const std::string& instrumentName, int depth = 5);

    void setRealTimeSubscription(RealTimeSubscription* server);

private:
    std::string getUserSelectedOrderId();

    RealTimeSubscription* m_server;
    WebSocketClient* m_wsClient;
    std::atomic<int> m_requestIdCounter;

    // For pending requests
    struct PendingRequest {
        std::mutex mtx;
        std::condition_variable cv;
        bool done = false;
        nlohmann::json response;
    };

    std::mutex m_responseMutex;
    std::map<int, std::shared_ptr<PendingRequest>> m_pendingRequests;

    // Protect operations
    std::mutex m_operationMutex;

    void handleWebSocketMessage(const std::string& message);
    int generateRequestId();

    // *** CHANGED *** CPU: we can cache a JSON "base" object for repeated building.
    nlohmann::json m_cachedBaseBuyJson; // partial JSON for "private/buy", so we only set certain fields each time.
};

#endif // ORDERMANAGEMENT_H
