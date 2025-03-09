#ifndef SERVER_API_HPP
#define SERVER_API_HPP

#include <sqlite3.h>
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <string>
#include <algorithm>
#include <memory>
#include <chrono>
#include <iostream>
#include <cmath>
#include <vector>
#include <atomic>
#include <mutex> 
#include <condition_variable>

class DatabaseManager {
private:
    sqlite3* db_;
    std::string port_name_;
    uint8_t& frequency_;  
    bool& debug_;     
    
    void createTableIfNotExists();
    void prepareStatements();
    sqlite3_stmt* insert_stmt_ = nullptr;

public:
    struct SensorData {
        __fp16 pressure;
        __fp16 temperature;
        __fp16 velocity;
        int64_t timestamp;
    };

    DatabaseManager(const std::string& db_path = "database.db", 
                    const std::string& port_name = "/dev/ttyS11", 
                    uint8_t& frequency = *(new uint8_t(115)),  // Default value via reference
                    bool& debug = *(new bool(false))); // Default value via reference
    ~DatabaseManager();

    // Disable copy / assgin / move constructors
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    DatabaseManager(DatabaseManager&&) = delete;
    DatabaseManager& operator=(DatabaseManager&&) = delete;

    void initializeDatabase();
    bool storeSensorData(const SensorData& data);
    std::vector<SensorData> getLastNMessages(int n); // Return N messages that match port, freq, debug
    
    // Setters - used ONLY during /configure call
    void updFrequency(const uint8_t& freq);
    void updDebug(const bool& debug);
};

class HTTPServer {
private:
    httplib::Server svr_;
    DatabaseManager& db_manager_;
    SerialInterface& serial_;
    std::string host_;
    int port_;
    uint8_t& frequency_;
    bool& debug_;

    std::thread server_thread_;                // Thread to run the server ops
    std::atomic<bool> is_reading_;             // Flag to check if can read messages from device

public:
     HTTPServer(const std::string& host, int port,
               DatabaseManager& db_manager,
               uint8_t& frequency, bool& debug,
               SerialInterface& serial);
     ~HTTPServer();

    // Temporary measure
    std::mutex cmd_mutex_;                     // Protects pending command data
    std::condition_variable cmd_cv_;           // Notifies when response arrives
    std::string pending_cmd_;                  // Currently awaited command (e.g., "$0")
    std::string cmd_response_;                 // Response from device (e.g., "ok")
    bool cmd_response_received_{false};        // Flag to check if response arrived

    // Disable copy / assgin / move constructors
    HTTPServer(const HTTPServer&) = delete;
    HTTPServer& operator=(const HTTPServer&) = delete;
    HTTPServer(HTTPServer&&) = delete;
    HTTPServer& operator=(HTTPServer&&) = delete;
    
    void start();
    void stop();
    bool isReading() const;
    void registerEndpoints();
};

// Some functions to work with strings - might be a good idea to create a separate API for it
std::string trim(const std::string &s);

#endif // SERVER_API_HPP