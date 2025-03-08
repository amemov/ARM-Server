#ifndef SERVER_API_HPP
#define SERVER_API_HPP

#include <sqlite3.h>
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <string>
#include <memory>
#include <chrono>
#include <iostream>
#include <cmath>
#include <vector>
#include <atomic>

class DatabaseManager {
private:
    sqlite3* db_;
    std::string port_name_;
    int& frequency_;  // Changed to reference to stay in sync with HTTPServer
    bool& debug_;     // Changed to reference to stay in sync with HTTPServer
    
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
                    int& frequency = *(new int(115)),  // Default value via reference
                    bool& debug = *(new bool(false))); // Default value via reference
    ~DatabaseManager();

    // Disable copy / assgin / move constructors

    void initializeDatabase();
    bool storeSensorData(const SensorData& data);
    std::vector<SensorData> getLastNMessages(int n); // New public method
};

class HTTPServer {
private:
    httplib::Server svr_;
    std::string host_;
    int port_;
    DatabaseManager& db_manager_;
    int& frequency_;
    bool& debug_;
    std::thread server_thread_;
    std::atomic<bool> is_reading_; 

public:
     HTTPServer(const std::string& host, int port,
          DatabaseManager& db_manager,
          int& frequency, bool& debug);
     ~HTTPServer();

     // Disable copy / assgin / move constructors
    
    void start();
    void stop();
    bool isReading() const;
    void registerEndpoints();
};

#endif // SERVER_API_HPP