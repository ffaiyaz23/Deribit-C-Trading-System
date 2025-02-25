#include "AuthenticationManager.h"
#include "OrderManagement.h"
#include "WebSocketClient.h"
#include "MarketDataServer.h"
#include "RealTimeSubscription.h"

#include <nlohmann/json.hpp>
#include <limits>
#include <iostream>
#include <thread>  // for sleep etc.

void displayMenu() {
    std::cout << "\n===== Order Management Menu =====\n";
    std::cout << "1. Place Order\n";
    std::cout << "2. Cancel Order\n";
    std::cout << "3. Modify Order\n";
    std::cout << "4. Get Order Book\n";
    std::cout << "5. View Current Positions\n";
    std::cout << "6. Exit\n";
    std::cout << "Please enter your choice: ";
}

int main()
{
    const std::string client_id = "BhmphQJY";
    const std::string client_secret = "pQdpINcyo1kWui6tf8Zi1JvW_yXsD5RqraGamlARPio";
    const std::string uri = "wss://test.deribit.com/ws/api/v2";

    AuthenticationManager authManager(client_id, client_secret);
    authManager.authenticate();

    std::string refreshToken = authManager.getRefreshToken();
    if (refreshToken.empty()) {
        std::cerr << "Failed to obtain refresh token. Exiting.\n";
        return -1;
    }

    WebSocketClient& wc = WebSocketClient::getInstance();
    bool ok = wc.connectBlocking(uri, refreshToken, 10);
    if (!ok) {
        std::cerr << "[main] Connection failed.\n";
    }
    else {
        std::cout << "[main] Connected & Authenticated.\n";
    }

    // Set up everything
    OrderManagement orderManager;
    RealTimeSubscription rts;
    rts.start();
    orderManager.setRealTimeSubscription(&rts);

    // *** CHANGED *** Thread mgmt in MarketDataServer: using hardware_concurrency by default
    MarketDataServer marketDataServer(9002, &rts);
    marketDataServer.run();
    rts.setMarketDataServer(&marketDataServer);

    // Simple menu loop
    bool running = true;
    while (running) {
        displayMenu();
        int choice;
        std::cin >> choice;
        if (!std::cin.good()) {
            std::cin.clear();
            std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
            continue;
        }
        std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');

        switch (choice) {
        case 1: {
            std::cout << "Place Order selected.\n";
            std::string symbol;
            int quantity;
            std::string orderType;
            std::string label;
            double price = -1.0;

            std::cout << "Enter symbol (e.g., BTC-PERPETUAL): ";
            std::getline(std::cin, symbol);

            std::cout << "Enter quantity: ";
            std::cin >> quantity;
            if (!std::cin.good() || quantity <= 0) {
                std::cin.clear();
                std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
                std::cerr << "Invalid quantity.\n";
                break;
            }
            std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');

            std::cout << "Enter order type (market/limit): ";
            std::getline(std::cin, orderType);

            std::cout << "Enter label: ";
            std::getline(std::cin, label);

            if (orderType == "limit") {
                std::cout << "Enter price: ";
                std::cin >> price;
                if (!std::cin.good() || price <= 0.0) {
                    std::cin.clear();
                    std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
                    std::cerr << "Invalid price.\n";
                    break;
                }
                std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
            }

            orderManager.placeOrder(symbol, quantity, orderType, label, price);
            break;
        }
        case 2:
            std::cout << "Cancel Order selected.\n";
            orderManager.cancelOrder();
            break;
        case 3:
            std::cout << "Modify Order selected.\n";
            orderManager.modifyOrder();
            break;
        case 4: {
            std::cout << "Get Order Book selected.\n";
            std::string symbol;
            int depth;
            std::cout << "Enter symbol: ";
            std::getline(std::cin, symbol);

            std::cout << "Enter depth: ";
            std::cin >> depth;
            if (!std::cin.good() || depth <= 0) {
                std::cin.clear();
                std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
                std::cerr << "Invalid depth.\n";
                break;
            }
            std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');

            orderManager.getOrderBook(symbol, depth);
            break;
        }
        case 5: {
            std::cout << "View Current Positions selected.\n";
            std::string currency;
            std::string kind;

            std::cout << "Enter Currency: ";
            std::getline(std::cin, currency);

            std::cout << "Enter kind: ";
            std::getline(std::cin, kind);

            orderManager.viewCurrentPositions(currency, kind);
            break;
        }
        case 6:
            running = false;
            break;
        default:
            std::cerr << "Unknown choice.\n";
            break;
        }
    }

    std::cout << "Press ENTER to stop server...\n";
    std::cin.get();

    // Cleanup
    marketDataServer.stop();
    rts.stop();
    wc.close();

    std::cout << "[main] Exiting.\n";
    return 0;
}
