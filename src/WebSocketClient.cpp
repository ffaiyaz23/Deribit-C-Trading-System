#include "WebSocketClient.h"
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <unordered_map>

struct PendingRequest {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    nlohmann::json response;
};

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
using json = nlohmann::json;

WebSocketClient& WebSocketClient::getInstance() {
    static WebSocketClient instance;
    return instance;
}

WebSocketClient::WebSocketClient()
    : is_connected(false)
    , m_connected(false)
    , m_failed(false)
    , m_authenticated(false)
    , m_authFailed(false)
    , m_authRequestId(-1)
{
    m_client.clear_access_channels(websocketpp::log::alevel::all);
    m_client.clear_error_channels(websocketpp::log::elevel::all);

    m_client.init_asio();
    m_client.start_perpetual();
    m_thread = std::thread([this]() {
        m_client.run();
        });

    m_client.set_tls_init_handler(bind(&WebSocketClient::on_tls_init, this, _1));
    m_client.set_open_handler(bind(&WebSocketClient::on_open, this, _1));
    m_client.set_message_handler(bind(&WebSocketClient::on_message, this, _1, _2));
    m_client.set_close_handler(bind(&WebSocketClient::on_close, this, _1));
    m_client.set_fail_handler(bind(&WebSocketClient::on_fail, this, _1));
}

WebSocketClient::~WebSocketClient()
{
    close();
    m_client.stop_perpetual();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void WebSocketClient::addMessageHandler(MessageHandler handler)
{
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    m_messageHandlers.push_back(handler);
}

void WebSocketClient::connect(const std::string& uri, const std::string& refreshToken)
{
    m_uri = uri;
    m_refreshToken = refreshToken;

    websocketpp::lib::error_code ec;
    client::connection_ptr con = m_client.get_connection(uri, ec);
    if (ec) {
        std::cerr << "[WebSocketClient] connect() - Error creating connection: "
            << ec.message() << std::endl;
        return;
    }

    m_hdl = con->get_handle();
    m_client.connect(con);
}

bool WebSocketClient::connectBlocking(const std::string& uri, const std::string& refreshToken, int timeoutSeconds)
{
    {
        std::lock_guard<std::mutex> lock(m_connectionMutex);
        m_connected = false;
        m_failed = false;
        m_authenticated = false;
        m_authFailed = false;
        m_authRequestId = -1;
    }

    connect(uri, refreshToken);

    {
        std::unique_lock<std::mutex> lock(m_connectionMutex);
        bool connected = m_connectionCV.wait_for(
            lock,
            std::chrono::seconds(timeoutSeconds),
            [this] { return m_connected || m_failed; }
        );

        if (!connected) {
            std::cerr << "[WebSocketClient] connectBlocking() timed out after "
                << timeoutSeconds << " seconds.\n";
            return false;
        }

        if (m_failed) {
            std::cerr << "[WebSocketClient] connectBlocking() - Connection failed.\n";
            return false;
        }
    }

    int authId = generateRequestId();
    json authRequest = {
        {"jsonrpc", "2.0"},
        {"method", "public/auth"},
        {"id", authId},
        {"params", {
            {"grant_type", "refresh_token"},
            {"refresh_token", m_refreshToken}
        }}
    };

    auto pendingAuth = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests[authId] = pendingAuth;
    }

    send(authRequest.dump());

    bool authSuccess = false;
    bool authTimedOut = false;

    {
        std::unique_lock<std::mutex> lock(pendingAuth->mtx);
        bool done = pendingAuth->cv.wait_for(
            lock,
            std::chrono::seconds(timeoutSeconds),
            [&pendingAuth]() { return pendingAuth->done; }
        );

        if (done) {
            if (pendingAuth->response.contains("result")) {
                authSuccess = true;
            }
            else if (pendingAuth->response.contains("error")) {
                std::cerr << "[WebSocketClient] Authentication failed: "
                    << pendingAuth->response["error"]["message"] << std::endl;
            }
        }
        else {
            std::cerr << "[WebSocketClient] Authentication timed out after "
                << timeoutSeconds << " seconds.\n";
            authTimedOut = true;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests.erase(authId);
    }

    if (!authSuccess) {
        if (authTimedOut || m_authFailed) {
            std::cerr << "[WebSocketClient] Authentication failed or timed out.\n";
        }
        return false;
    }

    std::cout << "[WebSocketClient] Authentication successful.\n";
    return true;
}

nlohmann::json WebSocketClient::sendBlockingRequest(const nlohmann::json& requestJson, int timeoutSeconds)
{
    nlohmann::json result;
    int reqId = (requestJson.contains("id")) ? requestJson["id"].get<int>() : generateRequestId();

    auto pendingReq = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests[reqId] = pendingReq;
    }

    nlohmann::json mutableRequest = requestJson;
    mutableRequest["id"] = reqId;

    send(mutableRequest.dump());

    {
        std::unique_lock<std::mutex> lk(pendingReq->mtx);
        bool done = pendingReq->cv.wait_for(
            lk,
            std::chrono::seconds(timeoutSeconds),
            [&pendingReq] { return pendingReq->done; }
        );
        if (done) {
            result = pendingReq->response;
        }
        else {
            std::cerr << "[WebSocketClient] sendBlockingRequest() timed out for id=" << reqId << std::endl;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_responseMutex);
        m_pendingRequests.erase(reqId);
    }

    return result;
}

void WebSocketClient::send(const std::string& message, websocketpp::lib::error_code& ec)
{
    if (!is_connected) {
        ec = websocketpp::error::make_error_code(websocketpp::error::invalid_state);
        std::cerr << "[WebSocketClient] send() - Not connected.\n";
        return;
    }
    m_client.send(m_hdl, message, websocketpp::frame::opcode::text, ec);
}

void WebSocketClient::send(const std::string& message)
{
    websocketpp::lib::error_code ec;
    send(message, ec);
    if (ec) {
        std::cerr << "[WebSocketClient] send() - Error sending message: " << ec.message() << std::endl;
    }
}

void WebSocketClient::close()
{
    if (!is_connected) {
        return;
    }
    websocketpp::lib::error_code ec;
    m_client.close(m_hdl, websocketpp::close::status::normal, "Closed by local request", ec);
    if (ec) {
        std::cerr << "[WebSocketClient] close() - Error: " << ec.message() << std::endl;
    }
    is_connected = false;
}

websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>
WebSocketClient::on_tls_init(websocketpp::connection_hdl)
{
    namespace asio = websocketpp::lib::asio;
    auto ctx = websocketpp::lib::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);

    try {
        ctx->set_options(asio::ssl::context::default_workarounds
            | asio::ssl::context::no_sslv2
            | asio::ssl::context::no_sslv3
            | asio::ssl::context::single_dh_use);
    }
    catch (std::exception& e) {
        std::cerr << "[WebSocketClient] TLS init error: " << e.what() << std::endl;
    }
    return ctx;
}

void WebSocketClient::on_open(websocketpp::connection_hdl hdl)
{
    std::cout << "[WebSocketClient] on_open - Connection established.\n";
    is_connected = true;
    {
        std::lock_guard<std::mutex> lock(m_connectionMutex);
        m_connected = true;
    }
    m_connectionCV.notify_all();
}

void WebSocketClient::on_message(websocketpp::connection_hdl hdl, typename client::message_ptr msg)
{
    // Handle internal pendingRequests:
    try {
        json incoming = json::parse(msg->get_payload());
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
        }
    }
    catch (json::parse_error& e) {
        std::cerr << "[WebSocketClient] JSON parse error: " << e.what()
            << "\nRaw message: " << msg->get_payload() << std::endl;
    }

    // Call all external handlers:
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    for (auto& handler : m_messageHandlers) {
        handler(msg->get_payload());
    }
}

void WebSocketClient::on_close(websocketpp::connection_hdl hdl)
{
    std::cout << "[WebSocketClient] on_close - Connection closed.\n";
    is_connected = false;
}

void WebSocketClient::on_fail(websocketpp::connection_hdl hdl)
{
    std::cerr << "[WebSocketClient] on_fail - Connection failed.\n";
    is_connected = false;
    {
        std::lock_guard<std::mutex> lock(m_connectionMutex);
        m_failed = true;
    }
    m_connectionCV.notify_all();
}

int WebSocketClient::generateRequestId()
{
    static std::atomic<int> currentId(1);
    return currentId++;
}
