#ifndef WEBSOCKETCLIENT_H
#define WEBSOCKETCLIENT_H

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <functional>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/common/memory.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <atomic>
#include <map>

// Forward declaration for PendingRequest
struct PendingRequest;

class WebSocketClient {
public:
    using client = websocketpp::client<websocketpp::config::asio_tls_client>;
    using MessageHandler = std::function<void(const std::string&)>;

    // Singleton
    static WebSocketClient& getInstance();

    // Non-blocking connect
    void connect(const std::string& uri, const std::string& refreshToken);

    // Blocking connect
    bool connectBlocking(const std::string& uri, const std::string& refreshToken, int timeoutSeconds = 10);

    // Multiple message handlers
    void addMessageHandler(MessageHandler handler);

    // Sending
    void send(const std::string& message);
    void send(const std::string& message, websocketpp::lib::error_code& ec);

    // Blocking request
    nlohmann::json sendBlockingRequest(const nlohmann::json& requestJson, int timeoutSeconds = 5);

    void close();
    int generateRequestId();

private:
    // Private constructor
    WebSocketClient();
    ~WebSocketClient();

    // Disallow copying
    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    // TLS init
    websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>on_tls_init(websocketpp::connection_hdl);

    // Handlers
    void on_open(websocketpp::connection_hdl hdl);
    void on_message(websocketpp::connection_hdl hdl, typename client::message_ptr msg);
    void on_close(websocketpp::connection_hdl hdl);
    void on_fail(websocketpp::connection_hdl hdl);

    client m_client;
    websocketpp::connection_hdl m_hdl;

    // For multiple external callbacks
    std::vector<MessageHandler> m_messageHandlers;
    std::mutex m_handlersMutex;

    std::thread m_thread;
    bool is_connected;
    std::string m_refreshToken;
    std::string m_uri;

    // For connectBlocking
    std::mutex m_connectionMutex;
    std::condition_variable m_connectionCV;
    bool m_connected;
    bool m_failed;

    bool m_authenticated;
    bool m_authFailed;
    int m_authRequestId;

    std::mutex m_responseMutex;
    std::unordered_map<int, std::shared_ptr<PendingRequest>> m_pendingRequests; // changed from map -> unordered_map (For faster lookups)
};

#endif // WEBSOCKETCLIENT_H
