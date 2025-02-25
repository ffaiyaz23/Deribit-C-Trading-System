cmake_minimum_required(VERSION 3.10)

# Specify the compilers (assuming you want to use MSVC)
set(CMAKE_C_COMPILER "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.42.34433/bin/Hostx64/x64/cl.exe")
set(CMAKE_CXX_COMPILER "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.42.34433/bin/Hostx64/x64/cl.exe")

# Set the Boost package root path when using vcpkg
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()
    
# Specify the project name
project(DeribitTradingSystem)

# Find Boost via vcpkg
find_package(Boost 1.86 REQUIRED COMPONENTS system thread chrono atomic filesystem regex random)


# Find jsoncpp
find_package(jsoncpp CONFIG REQUIRED)

# Find nlohmann_json
find_package(nlohmann_json CONFIG REQUIRED)

# Find libcurl
find_package(CURL REQUIRED)

# Find date library via vcpkg
find_package(date CONFIG REQUIRED)

# Specify the OpenSSL root directory
set(OPENSSL_ROOT_DIR "C:/Program Files/OpenSSL-Win64")

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# WebSocket++ (from local path)
# Ensure that WEB_SOCKETPP_PATH points to the root directory containing the 'websocketpp' folder
set(WEB_SOCKETPP_PATH "D:/my-repo/DeribitTradingSystem/websocketpp")  # Update path as needed
# Add the parent directory to include WebSocket++ headers correctly
include_directories(${WEB_SOCKETPP_PATH})  # Add WebSocket++ headers path

# Specify the required C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

# Include directories
include_directories(
    ${Boost_INCLUDE_DIRS}
    ${nlohmann_json_INCLUDE_DIRS}
    ${jsoncpp_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/include
    ${date_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${WEB_SOCKETPP_PATH}
)


# Add the source files for the project (Only .cpp files)
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/AuthenticationManager.cpp
    src/OrderManagement.cpp
    src/MarketDataServer.cpp
    src/WebSocketClient.cpp
    src/RealTimeSubscription.cpp
)

# Link Boost libraries, jsoncpp, nlohmann_json, CURL, date, OpenSSL, and WebSocket++
target_link_libraries(${PROJECT_NAME} PRIVATE 
    Boost::system 
    Boost::thread 
    Boost::chrono
    Boost::atomic
    Boost::filesystem
    Boost::regex
    Boost::random  # Added Boost.Random
    jsoncpp_lib
    nlohmann_json::nlohmann_json
    CURL::libcurl
    date::date
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Optional: Print some debug information to verify the configuration
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
message(STATUS "jsoncpp include dirs: ${jsoncpp_INCLUDE_DIRS}")
message(STATUS "nlohmann_json include dirs: ${nlohmann_json_INCLUDE_DIRS}")
message(STATUS "date include dirs: ${date_INCLUDE_DIRS}")
message(STATUS "OpenSSL include dirs: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "WebSocket++ include dirs: ${WEB_SOCKETPP_PATH}")
