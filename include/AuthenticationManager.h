#ifndef AUTHENTICATIONMANAGER_H
#define AUTHENTICATIONMANAGER_H

#include <string>
#include <ctime>
#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include <json/json.h>

class AuthenticationManager {
private:
    std::string client_id;
    std::string client_secret;
    std::string refresh_token;
    std::time_t expiry_timestamp;

public:
    // Constructor
    AuthenticationManager(const std::string& clientId, const std::string& clientSecret);

    // Simple actions
    void makeAuthenticatedApiCall();
    void authenticate();

    // Load & save token/expiry from files
    void loadTokens();
    void saveTokens();

    bool isTokenExpired();
    void refreshAccessToken();

    std::string getRefreshToken();

    // CURL callback
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);

private:
    void parseAuthResponse(const std::string& response);
    std::string makePostRequest(const std::string& url, const std::string& postData);
};

#endif // AUTHENTICATIONMANAGER_H
