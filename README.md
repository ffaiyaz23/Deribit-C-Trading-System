# Deribit C++ Trading System

This repository contains a **high-performance** C++ application for trading on the **Deribit Test** environment. It supports a **single persistent WebSocket** connection for low-latency order placement, order modifications, market data subscriptions, and a local WebSocket server for distributing real-time updates.

---

## Overview

1. **Order Management**
   - **Place Orders** (market/limit)  
   - **Cancel/Modify Orders**  
   - **Get Order Book** (any supported symbol at custom depth)  
   - **View Current Positions**  

2. **Real-Time Market Data**
   - Subscribes to **Deribit** channels (e.g. `book.BTC-PERPETUAL.100ms`)
   - Forwards streaming data to a **local WebSocket server** (`MarketDataServer`) so multiple local clients can subscribe and receive continuous updates.

3. **Authentication**
   - Manages **refresh_token** and automatically renews if expired.
   - Stores token details in text files for persistence.

4. **Local WebSocket Server**
   - Listens on **port 9002**.
   - Validates connections via a simple query parameter (`api_key=...`).
   - Tracks channel subscriptions and **broadcasts** data to relevant clients.

---

## Project Functionality

1. **Single Menu Interface (CLI)**
   - Prompts for symbol, quantity, price, etc.
   - Automatically sends JSON requests to Deribit, waits for the response, and prints the full JSON output.

2. **Subscription Handling**
   - Allows **subscribe** or **unsubscribe** calls to Deribit.
   - Forwards all “subscription” updates from Deribit to local WebSocket clients (for real-time order book updates).

3. **Thread Management**
   - Uses `std::thread::hardware_concurrency()` in the `MarketDataServer` to match available CPU cores, preventing oversubscription.

4. **Error Logging**
   - Prints out JSON error messages if requests are invalid, tokens have expired, or Deribit returns exceptions.

---

## Tech Stack

1. **C++17**
   - Core language used for all order management, networking, and concurrency.

2. **WebSocket++**
   - Provides both **client** (for connecting to Deribit) and **server** (for the local market data feed) capabilities.
   - Built on top of **Asio** for asynchronous I/O.

3. **CURL**
   - Used for **token refresh** calls (`public/auth` endpoint at Deribit).

4. **JsonCPP / nlohmann_json**
   - **JsonCPP** is used in the authentication code for reading/writing tokens.
   - **nlohmann_json** is used in the **WebSocket** messaging logic for building and parsing JSON structures.

5. **Boost**
   - For threading (`std::thread` was used directly, but Boost can be part of the dependencies).
   - Also used for utilities like system, filesystem, etc.

6. **OpenSSL**
   - Required for **TLS** connections to the Deribit WebSocket endpoint (`wss://test.deribit.com`).

7. **date (Howard Hinnant)**
   - For date/time manipulations if needed (though standard `<chrono>` is also leveraged).

---

## Build & Run

1. **Clone this repo** and ensure you have **vcpkg** or the needed libraries installed.
2. Configure and build with **CMake**:

   ```bash
   mkdir build && cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
   cmake --build . --config Release
