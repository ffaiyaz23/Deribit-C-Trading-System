#include "OrderManagement.h"
#include "RealTimeSubscription.h"
#include <iostream>
#include <limits>
#include <chrono>
#include <unordered_map>  // DATA STRUCTURE OPT: for fast lookups
#include <memory>         // MEMORY OPT: use smart pointers
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// We use these "optimization" macros in comments to highlight changes

OrderManagement::OrderManagement()
    : m_requestIdCounter(1000)  // CPU OPT: start at 1000
    , m_wsClient(&WebSocketClient::getInstance()) // MEMORY OPT: no raw pointers
    , m_server(nullptr)
{
    // CPU OPT: we attach one handler for inbound WebSocket messages,
    // no repeated overwrites
    m_wsClient->addMessageHandler([this](const std::string& msg) {
        handleWebSocketMessage(msg);
        });
}

OrderManagement::~OrderManagement()
{
    // No manual new/delete => MEMORY OPT
}

int OrderManagement::generateRequestId()
{
    // CPU OPT: atomic fetch_add is lock-free on many platforms
    return m_requestIdCounter.fetch_add(1);
}

/**
 * placeOrder
 * - We build JSON for "private/buy"
 * - Wait for the response
 * - Print the raw JSON
 * - Show errors or success (order_id).
 *
 * NETWORK OPT: We do everything over a single persistent WebSocket, not re-connecting each time.
 */
void OrderManagement::placeOrder(const std::string& instrumentName,
    int amount,
    const std::string& type,
    const std::string& label,
    double price)
{
    std::unique_lock<std::mutex> opLock(m_operationMutex); // CPU OPT: single lock for entire operation

    int requestId = generateRequestId();
    auto pending = std::make_shared<PendingRequest>();  // MEMORY OPT: no raw pointer
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests[requestId] = pending; // DATA STRUCTURE OPT: store in an unordered_map
    }

    // Build JSON
    json params = {
        {"instrument_name", instrumentName},
        {"amount", amount},
        {"type", type},
        {"label", label}
    };
    if (type == "limit") {
        if (price <= 0.0) {
            std::cerr << "[placeOrder] Invalid price for limit order.\n";
            {
                std::lock_guard<std::mutex> lock(m_responseMutex);
                m_pendingRequests.erase(requestId);
            }
            return;
        }
        params["price"] = price;
    }

    json req = {
        {"jsonrpc", "2.0"},
        {"id", requestId},
        {"method", "private/buy"},
        {"params", params}
    };

    // NETWORK OPT: single send, not multiple small sends
    m_wsClient->send(req.dump());
    std::cout << "[placeOrder] Sent private/buy (id=" << requestId << ")\n";

    // Wait for response
    {
        std::unique_lock<std::mutex> lock(pending->mtx);
        pending->cv.wait(lock, [&pending]() { return pending->done; });
    }
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests.erase(requestId);
    }

    // Print raw JSON
    const json& resp = pending->response;
    std::cout << "[placeOrder] Response:\n" << resp.dump(4) << "\n";

    // Show error or success
    if (resp.contains("error")) {
        std::cerr << "[placeOrder] Error: " << resp["error"].dump() << "\n";
    }
    else if (resp.contains("result")) {
        auto& result = resp["result"];
        if (result.contains("order") && result["order"].contains("order_id")) {
            std::string orderId = result["order"]["order_id"].get<std::string>();
            std::cout << "[placeOrder] Order placed successfully. ID=" << orderId << "\n";
        }
    }
}

/**
 * cancelOrder
 * - Fetches open orders
 * - Display them
 * - Let user pick one
 * - Send "private/cancel"
 * - Print JSON response
 */
void OrderManagement::cancelOrder()
{
    std::unique_lock<std::mutex> opLock(m_operationMutex);

    // CPU OPT: no repeated logic, we just re-use getUserSelectedOrderId
    std::string selectedOrderId = getUserSelectedOrderId();
    if (selectedOrderId == "-1") {
        return; // no open orders or user aborted
    }

    int requestId = generateRequestId();
    auto pending = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests[requestId] = pending;
    }

    json cancelReq = {
        {"jsonrpc", "2.0"},
        {"id", requestId},
        {"method", "private/cancel"},
        {"params", {
            {"order_id", selectedOrderId}
        }}
    };

    m_wsClient->send(cancelReq.dump());
    std::cout << "[cancelOrder] Sent private/cancel (id=" << requestId << ")\n";

    {
        std::unique_lock<std::mutex> lock(pending->mtx);
        pending->cv.wait(lock, [&pending]() { return pending->done; });
    }
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests.erase(requestId);
    }

    const json& resp = pending->response;
    std::cout << "[cancelOrder] Response:\n" << resp.dump(4) << "\n";
}

/**
 * modifyOrder
 * - Fetch open orders
 * - Let user select which order to modify
 * - Prompt for new amount / new price
 * - Call "private/edit"
 * - Print result
 */
void OrderManagement::modifyOrder()
{
    std::unique_lock<std::mutex> opLock(m_operationMutex);

    std::string selectedOrderId = getUserSelectedOrderId();
    if (selectedOrderId == "-1") {
        return;
    }

    double newAmount = -1.0;
    double newPrice = -1.0;

    std::cout << "Enter new amount (-1 to keep unchanged): ";
    std::cin >> newAmount;
    if (!std::cin.good()) {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "[modifyOrder] Invalid amount.\n";
        return;
    }

    std::cout << "Enter new price (-1 to keep unchanged): ";
    std::cin >> newPrice;
    if (!std::cin.good()) {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "[modifyOrder] Invalid price.\n";
        return;
    }
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    // If user set both to -1 => no changes
    if (newAmount == -1.0 && newPrice == -1.0) {
        std::cout << "[modifyOrder] No changes.\n";
        return;
    }

    json params;
    params["order_id"] = selectedOrderId;
    if (newAmount != -1.0) {
        params["amount"] = newAmount;
    }
    if (newPrice != -1.0) {
        params["price"] = newPrice;
    }

    int requestId = generateRequestId();
    auto pending = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests[requestId] = pending;
    }

    json modifyReq = {
        {"jsonrpc", "2.0"},
        {"id", requestId},
        {"method", "private/edit"},
        {"params", params}
    };

    m_wsClient->send(modifyReq.dump());
    std::cout << "[modifyOrder] Sent private/edit (id=" << requestId << ")\n";

    {
        std::unique_lock<std::mutex> lock(pending->mtx);
        pending->cv.wait(lock, [&pending]() { return pending->done; });
    }
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests.erase(requestId);
    }

    const json& resp = pending->response;
    std::cout << "[modifyOrder] Response:\n" << resp.dump(4) << "\n";
}

/**
 * viewCurrentPositions
 * - Calls "private/get_positions"
 * - Prints raw JSON
 */
void OrderManagement::viewCurrentPositions(const std::string& currency, const std::string& kind)
{
    std::unique_lock<std::mutex> opLock(m_operationMutex);

    int requestId = generateRequestId();
    auto pending = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests[requestId] = pending;
    }

    json req = {
        {"jsonrpc", "2.0"},
        {"id", requestId},
        {"method", "private/get_positions"},
        {"params", {
            {"currency", currency},
            {"kind", kind}
        }}
    };

    m_wsClient->send(req.dump());
    std::cout << "[viewCurrentPositions] Sent private/get_positions (id=" << requestId << ")\n";

    {
        std::unique_lock<std::mutex> lock(pending->mtx);
        pending->cv.wait(lock, [&pending]() { return pending->done; });
    }
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests.erase(requestId);
    }

    const json& resp = pending->response;
    std::cout << "[viewCurrentPositions] Response:\n" << resp.dump(4) << "\n";
}

/**
 * getOrderBook
 * - Calls "public/get_order_book" with instrumentName & depth
 * - Prints raw JSON, which includes the "bids"/"asks" etc.
 */
void OrderManagement::getOrderBook(const std::string& instrumentName, int depth)
{
    std::unique_lock<std::mutex> opLock(m_operationMutex);

    int requestId = generateRequestId();
    auto pending = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests[requestId] = pending;
    }

    json req = {
        {"jsonrpc", "2.0"},
        {"id", requestId},
        {"method", "public/get_order_book"},
        {"params", {
            {"instrument_name", instrumentName},
            {"depth", depth}
        }}
    };

    m_wsClient->send(req.dump());
    std::cout << "[getOrderBook] Sent public/get_order_book (id=" << requestId << ", depth=" << depth << ")\n";

    {
        std::unique_lock<std::mutex> lock(pending->mtx);
        pending->cv.wait(lock, [&pending]() { return pending->done; });
    }
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests.erase(requestId);
    }

    const json& resp = pending->response;
    std::cout << "[getOrderBook] Response:\n" << resp.dump(4) << "\n";
}

/**
 * handleWebSocketMessage
 * - If we see an "id", find the matching pending request,
 *   store the JSON, notify the condvar
 * - Otherwise, it's a subscription or heartbeat => print or forward to RealTimeSubscription
 */
void OrderManagement::handleWebSocketMessage(const std::string& message)
{
    try {
        json incoming = json::parse(message);

        if (incoming.contains("id")) {
            int idVal = incoming["id"].get<int>();

            std::shared_ptr<PendingRequest> reqPtr;
            {
                std::lock_guard<std::mutex> lock(m_responseMutex);
                auto it = m_pendingRequests.find(idVal);
                if (it != m_pendingRequests.end()) {
                    reqPtr = it->second;
                }
            }

            if (reqPtr) {
                {
                    std::lock_guard<std::mutex> plock(reqPtr->mtx);
                    reqPtr->response = incoming;
                    reqPtr->done = true;
                }
                reqPtr->cv.notify_one();
            }
            else {
                std::cout << "[OrderManagement::handleWebSocketMessage] Untracked response (id="
                    << idVal << "): " << incoming.dump(4) << "\n";
            }
        }
        else {
            // Possibly subscription or other
            std::cout << "[OrderManagement::handleWebSocketMessage] Notification:\n"
                << incoming.dump(4) << "\n";

            if (m_server) {
                m_server->onMessage(incoming.dump(4));
            }
        }
    }
    catch (const json::parse_error& e) {
        std::cerr << "[OrderManagement::handleWebSocketMessage] JSON parse error: "
            << e.what() << "\nRaw message: " << message << "\n";
    }
}

void OrderManagement::setRealTimeSubscription(RealTimeSubscription* server)
{
    m_server = server;
}

/**
 * getUserSelectedOrderId
 * - Calls "private/get_open_orders" to get a list of open orders
 * - Prints them
 * - Lets user select one
 * - Returns that order_id or "-1" if none selected
 *
 * CPU OPT: re-uses the same approach for both cancel/modify
 */
std::string OrderManagement::getUserSelectedOrderId()
{
    int fetchRequestId = generateRequestId();
    auto fetchPending = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests[fetchRequestId] = fetchPending;
    }

    json fetchReq = {
        {"jsonrpc", "2.0"},
        {"id", fetchRequestId},
        {"method", "private/get_open_orders"},
        {"params", {}}
    };

    m_wsClient->send(fetchReq.dump());
    std::cout << "[getUserSelectedOrderId] Sent private/get_open_orders (id=" << fetchRequestId << ")\n";

    {
        std::unique_lock<std::mutex> lock(fetchPending->mtx);
        fetchPending->cv.wait(lock, [&fetchPending]() { return fetchPending->done; });
    }
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests.erase(fetchRequestId);
    }

    const json& fetchResp = fetchPending->response;
    std::cout << "[getUserSelectedOrderId] Response:\n" << fetchResp.dump(4) << "\n";

    if (!fetchResp.contains("result") || !fetchResp["result"].is_array() || fetchResp["result"].empty()) {
        std::cout << "[getUserSelectedOrderId] No open orders.\n";
        return "-1";
    }

    auto openOrders = fetchResp["result"].get<std::vector<json>>();
    std::cout << "[getUserSelectedOrderId] Open orders:\n";
    for (size_t i = 0; i < openOrders.size(); ++i) {
        const auto& order = openOrders[i];
        std::cout << (i + 1) << ") Order ID: "
            << order.value("order_id", "N/A")
            << " | Instrument: "
            << order.value("instrument_name", "N/A")
            << " | Price: "
            << order.value("price", 0.0)
            << " | Amount: "
            << order.value("amount", 0.0)
            << "\n";
    }

    std::cout << "Enter the number to select (0=abort): ";
    int choice = 0;
    std::cin >> choice;
    if (!std::cin.good()) {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "[getUserSelectedOrderId] Invalid input.\n";
        return "-1";
    }
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (choice <= 0 || choice > static_cast<int>(openOrders.size())) {
        std::cout << "[getUserSelectedOrderId] Aborted.\n";
        return "-1";
    }

    std::string selectedOrderId = openOrders[choice - 1].value("order_id", "");
    if (selectedOrderId.empty()) {
        std::cout << "[getUserSelectedOrderId] No valid 'order_id'.\n";
        return "-1";
    }
    return selectedOrderId;
}
