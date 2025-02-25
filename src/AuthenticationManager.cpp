#include "AuthenticationManager.h"

AuthenticationManager::AuthenticationManager(const std::string& clientId, const std::string& clientSecret)
    : client_id(clientId)
    , client_secret(clientSecret)
    , expiry_timestamp(0)
{
    loadTokens(); // Attempt to load refresh_token from file
}

void AuthenticationManager::loadTokens() {

    // Load refresh token
    std::ifstream refreshTokenFile("D:/my-repo/DeribitTradingSystem/src/refresh_token.txt");
    if (refreshTokenFile.is_open()) {
        std::getline(refreshTokenFile, refresh_token);
        refreshTokenFile.close();
        std::cout << "[AuthenticationManager] Refresh token loaded.\n";
    }
    else {
        std::cerr << "[AuthenticationManager] No saved refresh token found.\n";
    }

    // If refresh_token is missing or expired, refresh it
    if (refresh_token.empty() || isTokenExpired()) {
        std::cout << "[AuthenticationManager] Token missing or expired, refreshing...\n";
        refreshAccessToken();
    }

    // Load expiry timestamp
    std::ifstream expiryFile("D:/my-repo/DeribitTradingSystem/src/expiry_timestamp.txt");
    if (expiryFile.is_open()) {
        expiryFile >> expiry_timestamp;
        expiryFile.close();
    }
    else {
        std::cerr << "[AuthenticationManager] Error reading expiry timestamp from file.\n";
    }
}

void AuthenticationManager::saveTokens() {

    // Save refresh token
    std::ofstream refreshTokenFile("D:/my-repo/DeribitTradingSystem/src/refresh_token.txt");
    if (refreshTokenFile.is_open()) {
        refreshTokenFile << refresh_token << std::endl;
        refreshTokenFile.close();
        std::cout << "[AuthenticationManager] Refresh token saved.\n";
    }
    else {
        std::cerr << "[AuthenticationManager] Error writing refresh token.\n";
    }

    // Save expiry timestamp
    std::ofstream expiryFile("D:/my-repo/DeribitTradingSystem/src/expiry_timestamp.txt");
    if (expiryFile.is_open()) {
        std::string timeString = std::to_string(expiry_timestamp);
        expiryFile << timeString << std::endl;
        expiryFile.close();
    }
    else {
        std::cerr << "[AuthenticationManager] Error writing expiry timestamp.\n";
    }
}

bool AuthenticationManager::isTokenExpired() {
    std::time_t currentTime = std::time(nullptr);
    return (currentTime >= expiry_timestamp);
}

void AuthenticationManager::authenticate() {
    if (isTokenExpired()) {
        std::cout << "[AuthenticationManager] Token expired; refreshing.\n";
        refreshAccessToken();
    }
}

void AuthenticationManager::refreshAccessToken() {
    const std::string url = "https://test.deribit.com/api/v2/public/auth";
    Json::Value payload;
    payload["id"] = 0;
    payload["jsonrpc"] = "2.0";
    payload["method"] = "public/auth";

    Json::Value params;
    params["grant_type"] = "client_credentials";
    params["scope"] = "session:apiconsole-03y7koodcyfr expires:2592000";
    params["client_id"] = client_id;
    params["client_secret"] = client_secret;

    payload["params"] = params;

    Json::StreamWriterBuilder writer;
    std::string postData = Json::writeString(writer, payload);

    std::string response = makePostRequest(url, postData);
    parseAuthResponse(response);
}

void AuthenticationManager::parseAuthResponse(const std::string& response) {
    Json::Reader reader;
    Json::Value root;
    if (reader.parse(response, root)) {
        if (root.isMember("result")) {
            Json::Value result = root["result"];

            if (result.isMember("refresh_token")) {
                refresh_token = result["refresh_token"].asString();
            }

            int expires_in = result["expires_in"].asInt();
            std::time_t currentTime = std::time(nullptr);
            expiry_timestamp = currentTime + expires_in;

            saveTokens();
        }
    }
    else {
        std::cerr << "[AuthenticationManager] parseAuthResponse - parse error: " << response << std::endl;
    }
}

std::string AuthenticationManager::makePostRequest(const std::string& url, const std::string& postData) {
    CURL* curl = curl_easy_init();
    std::string responseData;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "[AuthenticationManager] CURL POST request failed: "
                << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }
    return responseData;
}

std::string AuthenticationManager::getRefreshToken() {
    return refresh_token;
}

void AuthenticationManager::makeAuthenticatedApiCall() {
    // If needed, we can refresh if token is expired
    if (isTokenExpired()) {
        refreshAccessToken();
    }
}

size_t AuthenticationManager::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* responseData = static_cast<std::string*>(userp);
    responseData->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}
