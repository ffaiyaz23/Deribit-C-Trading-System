#include "MarketDataServer.h"
#include <iostream>
#include <thread>

MarketDataServer::MarketDataServer(uint16_t port, RealTimeSubscription* realTimeSub, size_t io_threads)
    : m_port(port)
    , m_realTimeSubscription(realTimeSub)
    // *** CHANGED *** default to hardware_concurrency if 0
    , m_ioThreads(io_threads == 0 ? std::thread::hardware_concurrency() : io_threads)
{
    try {
        m_server.clear_access_channels(websocketpp::log::alevel::all);
        m_server.init_asio();

        m_server.set_open_handler(std::bind(&MarketDataServer::on_open, this, std::placeholders::_1));
        m_server.set_close_handler(std::bind(&MarketDataServer::on_close, this, std::placeholders::_1));
        m_server.set_message_handler(std::bind(&MarketDataServer::on_message, this, std::placeholders::_1, std::placeholders::_2));
        m_server.set_fail_handler(std::bind(&MarketDataServer::on_fail, this, std::placeholders::_1));

        {
            std::lock_guard<std::mutex> lock(m_apiKeysMutex);
            m_validApiKeys.insert("API_KEY_12345");
            m_validApiKeys.insert("API_KEY_67890");
        }

        std::cout << "[MarketDataServer] Initialized on port=" << m_port
            << " with io_threads=" << m_ioThreads << "\n";
    }
    catch (const websocketpp::exception& e) {
        std::cerr << "[MarketDataServer] Exception in constructor: " << e.what() << "\n";
    }
}

MarketDataServer::~MarketDataServer()
{
    stop();
}

void MarketDataServer::run()
{
    try {
        m_server.listen(m_port);
        m_server.start_accept();

        std::cout << "[MarketDataServer] Server listening on " << m_port << "\n";

        for (size_t i = 0; i < m_ioThreads; ++i) {
            m_asioThreads.emplace_back([this]() {
                try {
                    m_server.run();
                }
                catch (const websocketpp::exception& e) {
                    std::cerr << "[MarketDataServer] Exception in thread: " << e.what() << "\n";
                }
                });
        }
        std::cout << "[MarketDataServer] " << m_ioThreads << " I/O thread(s) launched.\n";
    }
    catch (const websocketpp::exception& e) {
        std::cerr << "[MarketDataServer] run() exception: " << e.what() << "\n";
    }
}

void MarketDataServer::stop()
{
    try {
        m_server.stop_listening();
        std::cout << "[MarketDataServer] Stop listening on port " << m_port << "\n";

        {
            std::lock_guard<std::mutex> lock(m_connectionsMutex);
            for (auto& hdl : m_connections) {
                websocketpp::lib::error_code ec;
                m_server.close(hdl, websocketpp::close::status::normal, "Server shutting down", ec);
                if (ec) {
                    std::cerr << "[MarketDataServer] Error closing connection: " << ec.message() << "\n";
                }
            }
            m_connections.clear();
        }

        m_server.stop();

        for (auto& t : m_asioThreads) {
            if (t.joinable()) {
                t.join();
            }
        }
        m_asioThreads.clear();

        std::cout << "[MarketDataServer] Stopped gracefully.\n";
    }
    catch (const websocketpp::exception& e) {
        std::cerr << "[MarketDataServer] stop() exception: " << e.what() << "\n";
    }
}

bool MarketDataServer::authenticateClient(const std::string& apiKey)
{
    std::lock_guard<std::mutex> lock(m_apiKeysMutex);
    return (m_validApiKeys.find(apiKey) != m_validApiKeys.end());
}

void MarketDataServer::on_open(websocketpp::connection_hdl hdl)
{
    auto con = m_server.get_con_from_hdl(hdl);
    std::string query = con->get_uri()->get_query();

    // Look for "api_key=xxx" in the query string
    std::regex apiKeyRegex("api_key=([A-Za-z0-9_]+)");
    std::smatch match;
    std::string apiKey;
    if (std::regex_search(query, match, apiKeyRegex) && match.size() > 1) {
        apiKey = match[1];
    }

    if (!authenticateClient(apiKey)) {
        std::cerr << "[MarketDataServer] Authentication failed for " << apiKey << "\n";
        websocketpp::lib::error_code ec;
        m_server.close(hdl, websocketpp::close::status::policy_violation, "Invalid API Key", ec);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        m_connections.insert(hdl);
    }

    std::cout << "[MarketDataServer] Authenticated client connected. Total clients: "
        << m_connections.size() << "\n";
}

void MarketDataServer::on_close(websocketpp::connection_hdl hdl)
{
    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        m_connections.erase(hdl);
    }

    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        auto clientIt = m_clientSubscriptions.find(hdl);
        if (clientIt != m_clientSubscriptions.end()) {
            for (auto& channel : clientIt->second) {
                auto symIt = m_channelSubscribers.find(channel);
                if (symIt != m_channelSubscribers.end()) {
                    symIt->second.erase(hdl);
                    if (symIt->second.empty()) {
                        m_channelSubscribers.erase(symIt);
                        if (m_realTimeSubscription) {
                            m_realTimeSubscription->unsubscribeSymbol(channel);
                            std::cout << "[MarketDataServer] Unsubscribed from " << channel
                                << " (no more subscribers)\n";
                        }
                    }
                }
            }
            m_clientSubscriptions.erase(clientIt);
        }
    }

    std::cout << "[MarketDataServer] Client disconnected. Total clients: "
        << m_connections.size() << "\n";
}

void MarketDataServer::on_fail(websocketpp::connection_hdl hdl)
{
    auto con = m_server.get_con_from_hdl(hdl);
    std::string uri = con->get_uri()->str();

    std::cerr << "[MarketDataServer] Connection failed for URI: " << uri << "\n";

    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        m_connections.erase(hdl);
    }

    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        auto clientIt = m_clientSubscriptions.find(hdl);
        if (clientIt != m_clientSubscriptions.end()) {
            for (auto& channel : clientIt->second) {
                auto symIt = m_channelSubscribers.find(channel);
                if (symIt != m_channelSubscribers.end()) {
                    symIt->second.erase(hdl);
                    if (symIt->second.empty()) {
                        m_channelSubscribers.erase(symIt);
                        if (m_realTimeSubscription) {
                            m_realTimeSubscription->unsubscribeSymbol(channel);
                            std::cout << "[MarketDataServer] Unsubscribed from " << channel
                                << " (no more subscribers)\n";
                        }
                    }
                }
            }
            m_clientSubscriptions.erase(clientIt);
        }
    }

    std::cout << "[MarketDataServer] Connection failed. Remaining clients: "
        << m_connections.size() << "\n";
}

// *** CHANGED ***
// We now allow the client to send {"method": "close"} to forcibly close just that connection.
void MarketDataServer::on_message(websocketpp::connection_hdl hdl, server::message_ptr msg)
{
    std::string payload = msg->get_payload();
    std::cout << "[MarketDataServer] Received: " << payload << "\n";

    try {
        auto j = nlohmann::json::parse(payload);
        std::string method = j.value("method", "");

        if (method == "subscribe" || method == "unsubscribe") {
            std::string instrument_name = j.value("instrument_name", "");
            std::string group = j.value("group", "none");
            std::string depth = j.value("depth", "1");
            std::string interval = j.value("interval", "100ms");

            if (instrument_name.empty()) {
                std::cerr << "[MarketDataServer] Missing instrument_name.\n";
                return;
            }
            std::string channel = "book." + instrument_name + "." + group + "." + depth + "." + interval;

            if (method == "subscribe") {
                addSubscription(hdl, channel);
            }
            else {
                removeSubscription(hdl, channel);
            }
        }
        // *** NEW: client wants to close their connection
        else if (method == "close") {
            std::cout << "[MarketDataServer] Client requested close.\n";
            websocketpp::lib::error_code ec;
            m_server.close(hdl, websocketpp::close::status::normal, "Closed by client request", ec);
            if (ec) {
                std::cerr << "[MarketDataServer] Error closing connection: " << ec.message() << "\n";
            }
        }
        else {
            // If it's not subscribe/unsubscribe/close, ignore or log:
            std::cout << "[MarketDataServer] Unknown method: " << method << "\n";
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[MarketDataServer] JSON parse error: " << e.what() << "\n";
    }
}

void MarketDataServer::addSubscription(websocketpp::connection_hdl hdl, const std::string& channel)
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    auto& clientChans = m_clientSubscriptions[hdl];
    if (clientChans.find(channel) == clientChans.end()) {
        clientChans.insert(channel);

        auto& subsSet = m_channelSubscribers[channel];
        subsSet.insert(hdl);

        if (subsSet.size() == 1 && m_realTimeSubscription) {
            m_realTimeSubscription->subscribeSymbol(channel);
            std::cout << "[MarketDataServer] Subscribed to " << channel << " at RealTimeSubscription.\n";
        }
        std::cout << "[MarketDataServer] Client subscribed: " << channel << "\n";
    }
    else {
        std::cout << "[MarketDataServer] Already subscribed: " << channel << "\n";
    }
}

void MarketDataServer::removeSubscription(websocketpp::connection_hdl hdl, const std::string& channel)
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    auto clientIt = m_clientSubscriptions.find(hdl);
    if (clientIt != m_clientSubscriptions.end()) {
        auto& clientChans = clientIt->second;
        if (clientChans.find(channel) != clientChans.end()) {
            clientChans.erase(channel);
            auto subsIt = m_channelSubscribers.find(channel);
            if (subsIt != m_channelSubscribers.end()) {
                subsIt->second.erase(hdl);
                if (subsIt->second.empty()) {
                    m_channelSubscribers.erase(subsIt);
                    if (m_realTimeSubscription) {
                        m_realTimeSubscription->unsubscribeSymbol(channel);
                        std::cout << "[MarketDataServer] No more subscribers; unsubscribed from " << channel << "\n";
                    }
                }
            }
            std::cout << "[MarketDataServer] Client unsubscribed: " << channel << "\n";
        }
        else {
            std::cout << "[MarketDataServer] Client wasn't subscribed to: " << channel << "\n";
        }
    }
}

void MarketDataServer::sendUpdateToClients(const std::string& channel, const nlohmann::json& data)
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    auto it = m_channelSubscribers.find(channel);
    if (it == m_channelSubscribers.end()) {
        std::cout << "[MarketDataServer] No subscribers for " << channel << "\n";
        return;
    }

    std::string message = data.dump();
    for (auto& hdl : it->second) {
        websocketpp::lib::error_code ec;
        m_server.send(hdl, message, websocketpp::frame::opcode::text, ec);
        if (ec) {
            std::cerr << "[MarketDataServer] Error sending to " << channel << ": " << ec.message() << "\n";
        }
    }
}
