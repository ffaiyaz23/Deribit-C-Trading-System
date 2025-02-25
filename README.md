# Deribit C++ Trading System

This repository contains a high-performance C++ application for trading on the Deribit Test environment. It supports a single persistent WebSocket connection for low-latency order placement, order modifications, market data subscriptions, and a local WebSocket server for distributing real-time updates.

Overview
Order Management

Place Orders (market/limit)
Cancel/Modify Orders
Get Order Book (any supported symbol at custom depth)
View Current Positions
Real-Time Market Data

Subscribes to Deribit channels (e.g. "book.BTC-PERPETUAL.100ms")
Forwards streaming data to a local WebSocket server (MarketDataServer) so multiple local clients can subscribe and receive continuous updates.
Authentication

Manages refresh_token and automatically renews if expired.
Stores token details in simple text files for persistence.
Local WebSocket Server

Listens on port 9002.
Validates connections via simple query parameter (api_key=...).
Tracks channel subscriptions and broadcasts data to relevant clients.
Project Functionality
Single Menu Interface (CLI)

Prompts for symbol, quantity, price, etc.
Automatically sends JSON requests to Deribit, waits for the response, and prints the full JSON output.
Subscription Handling

Allows subscribe or unsubscribe calls to Deribit.
Forwards all “subscription” updates from Deribit to local WebSocket clients (for real-time order book updates).
Thread Management

Uses std::thread::hardware_concurrency() in the MarketDataServer to match available CPU cores, preventing oversubscription.
Error Logging

Prints out JSON error messages if requests are invalid, tokens have expired, or Deribit returns exceptions.
Tech Stack
C++17

Core language used for all order management, networking, and concurrency.
WebSocket++

Provides both client (for connecting to Deribit) and server (for the local market data feed) capabilities.
Built on top of Asio for asynchronous I/O.
CURL

Used for token refresh calls (public/auth endpoint at Deribit).
JsonCPP / nlohmann_json

JsonCPP is used in the authentication code for reading/writing tokens.
nlohmann_json is used in the WebSocket messaging logic for building and parsing JSON structures.
Boost

For threading (std::thread was used directly, but Boost can be part of the dependencies).
Also used for utilities like system, filesystem, etc.
OpenSSL

Required for TLS connections to the Deribit WebSocket endpoint (wss://test.deribit.com).
date (Howard Hinnant)

For date/time manipulations if needed (though standard <chrono> is also leveraged).
Build & Run
Clone this repo and ensure you have vcpkg or the needed libraries installed.
Configure and build with CMake:
bash
Copy
Edit
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build . --config Release
Run the resulting executable:
bash
Copy
Edit
./DeribitTradingSystem
On startup, the application attempts to:
Load tokens from disk, authenticate with Deribit if expired.
Start an interactive menu for order management.
Launch a MarketDataServer on port 9002.
Contributing
Pull requests and feature suggestions are welcome. In particular, if you have ideas for:

More advanced error handling
Additional performance instrumentation or caching
Multi-user load balancing
Feel free to submit issues or PRs!

Thank you for checking out this project. Happy trading on Deribit Test!