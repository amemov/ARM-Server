#include "server_api.hpp"

// DatabaseManager Implementation
DatabaseManager::DatabaseManager(const std::string& db_path, 
                                const std::string& port_name,
                                uint8_t& frequency, bool& debug)
    : port_name_(port_name), frequency_(frequency), debug_(debug) {
    
        const std::string default_db_path = "database.db";
    std::string final_db_path;

    // Step 1: Validate user-provided path (do that if path is not default)
    bool use_default = false;
    if(db_path != default_db_path){
        try {
            if (isPathRestricted(db_path)) {
                throw std::runtime_error("Path is in restricted directory");
            }
    
            // Create parent directories if needed
            fs::path user_path(db_path);
            fs::create_directories(user_path.parent_path());
    
            final_db_path = db_path;
        } catch (const std::exception& e) {
            std::cerr << "Invalid database path '" << db_path << "': " << e.what()
                      << "\nFalling back to default path: " << default_db_path << "\n";
            use_default = true;
        }
    }else{
        use_default = true;
    }
    

    // Step 2: Backtrack to default path if needed
    if (use_default) {
        final_db_path = default_db_path;
    }

    // Step 3: Open the database
    if (sqlite3_open(final_db_path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("Database error: " + std::string(sqlite3_errmsg(db_)));
    }
    createTableIfNotExists();
    prepareStatements();

    std::cout << "Database initialized at: " << final_db_path << "\n";
}

DatabaseManager::~DatabaseManager() {
    sqlite3_finalize(insert_stmt_);
    sqlite3_close(db_);
}

void DatabaseManager::createTableIfNotExists() {
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS SensorData ("
        "Port TEXT NOT NULL, "
        "Frequency INTEGER NOT NULL, "
        "Debug INTEGER NOT NULL CHECK (Debug IN (0, 1)), "
        "Pressure BLOB, "
        "Temperature BLOB, "
        "Velocity BLOB, "
        "Timestamp INTEGER NOT NULL);";

    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::string error = "SQL error: " + std::string(err_msg);
        sqlite3_free(err_msg);
        throw std::runtime_error(error);
    }
    std::cout << "DatabaseManager: Table created (or verified) successfully.\n";
}

// Bind Insertion Statement 
void DatabaseManager::prepareStatements() {
    const char* sql = 
        "INSERT INTO SensorData (Port, Frequency, Debug, Pressure, Temperature, Velocity, Timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";
    
    if (sqlite3_prepare_v2(db_, sql, -1, &insert_stmt_, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare insert statement: " + 
                                std::string(sqlite3_errmsg(db_)));
    }
}

// Validate if a path is in a restricted directory
bool DatabaseManager::isPathRestricted(const fs::path& path) {
    try {
        const fs::path abs_path = fs::absolute(path);
        const fs::path parent_dir = abs_path.parent_path();
        const fs::path canonical_parent = fs::canonical(parent_dir);

        // Normalize paths with trailing slashes for accurate comparison
        auto normalize = [](const fs::path& p) {
            std::string s = p.string();
            if (s.back() != '/') s += '/';
            return s;
        };

        const std::string parent_path = normalize(canonical_parent);

        for (const auto& restricted : restricted_dirs) {
            const fs::path canonical_restricted = fs::canonical(restricted);
            const std::string restricted_path = normalize(canonical_restricted);

            // Check if parent is inside a restricted directory
            if (parent_path.starts_with(restricted_path)) {
                return true;
            }
        }
    } catch (const fs::filesystem_error&) {
        // Invalid path (e.g., non-existent parent directory)
        return true;
    }
    return false;
}

bool DatabaseManager::storeSensorData(const SensorData& data) {
    sqlite3_reset(insert_stmt_);
    
    sqlite3_bind_text(insert_stmt_, 1, port_name_.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(insert_stmt_, 2, frequency_);
    sqlite3_bind_int(insert_stmt_, 3, debug_ ? 1 : 0);
    sqlite3_bind_blob(insert_stmt_, 4, &data.pressure, sizeof(__fp16), SQLITE_STATIC);
    sqlite3_bind_blob(insert_stmt_, 5, &data.temperature, sizeof(__fp16), SQLITE_STATIC);
    sqlite3_bind_blob(insert_stmt_, 6, &data.velocity, sizeof(__fp16), SQLITE_STATIC);
    sqlite3_bind_int64(insert_stmt_, 7, data.timestamp);

    return sqlite3_step(insert_stmt_) == SQLITE_DONE;
}

std::vector<DatabaseManager::SensorData> DatabaseManager::getLastNMessages(int n) {
    std::vector<SensorData> result;
    
    std::string sql = "SELECT Pressure, Temperature, Velocity, Timestamp FROM SensorData "
                    "WHERE Port = ? AND Frequency = ? AND Debug = ? ORDER BY Timestamp DESC LIMIT ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }
    sqlite3_bind_text(stmt, 1, port_name_.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, frequency_);
    sqlite3_bind_int(stmt, 3, (debug_ == true) ? 1 : 0);
    sqlite3_bind_int(stmt, 4, n);

    // We are probably going to have more calls that actually request the existing data
    // So the reserve should help us speed up the push_backs below. 
    result.reserve(n); 
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        SensorData data;
        const void* blobPressure = sqlite3_column_blob(stmt, 0);
        const void* blobTemperature = sqlite3_column_blob(stmt, 1);
        const void* blobVelocity = sqlite3_column_blob(stmt, 2);
        if (blobPressure && blobTemperature && blobVelocity) {
            data.pressure = *reinterpret_cast<const __fp16*>(blobPressure);
            data.temperature = *reinterpret_cast<const __fp16*>(blobTemperature);
            data.velocity = *reinterpret_cast<const __fp16*>(blobVelocity);
            data.timestamp = sqlite3_column_int64(stmt, 3);
            result.push_back(data);
        }
    }
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Query error: " + std::string(sqlite3_errmsg(db_)));
    }
    sqlite3_finalize(stmt);
    return result;
}

// Updates the value using validated result in PUT /config command
void DatabaseManager::updFrequency(const uint8_t& freq) { frequency_ = freq; }
void DatabaseManager::updDebug(const bool& debug) { debug_ = debug; }

// HTTPServer Implementation. By default, doesn't read until /start command
HTTPServer::HTTPServer(const std::string& host, int port,
                        DatabaseManager& db_manager,
                        uint8_t& frequency, bool& debug,
                        SerialInterface& serial)
    : host_(host), port_(port), db_manager_(db_manager),
    frequency_(frequency), debug_(debug), is_reading_(false),
    serial_(serial) {

        // Validate server name (hostname)
        if (!isValidHostname(host_) || host_.length() == 0) {
            std::cerr << "Invalid host name '" << host_ 
                    << "'. Falling back to default host: localhost\n";
            host_ = "localhost";
        }

        // Validate the port is not priviliged 
        int default_port = 7100;
        if(port_ < 1023){
            std::cerr << "Invalid port '" << port_ << "': " << "Attempt to access priviliged port."
                  << "\nFalling back to default port: " << 7100 << "\n";
            port_ = 7100;
        }

    }

HTTPServer::~HTTPServer(){
    stop();
}

void HTTPServer::start() {
    registerEndpoints();
    server_thread_ = std::thread([this]() {
        svr_.listen(host_.c_str(), port_);
    });
}

void HTTPServer::stop() {
    svr_.stop();
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

// GET /start invoked successfully at some point and no GET /stop so far?
bool HTTPServer::isReading() const { 
    return is_reading_.load(); 
}

// Defines the HTTP commands for server
void HTTPServer::registerEndpoints() {
    svr_.Get("/start", [&](const httplib::Request &, httplib::Response &res) {
        if (isReading()) {
            res.set_content("GET /start: Already reading\n", "text/plain");
            std::cout << "GET /start: Already reading\n";
            res.status = 400; // Bad Request
            return;
        }
        try {
            std::unique_lock<std::mutex> lock(cmd_mutex_);
            // Send Start command request
            pending_cmd_ = "$0";
            cmd_response_received_ = false;
            serial_.sendData("$0\n"); 

            // Wait for response or timeout
            bool response_valid = cmd_cv_.wait_for(
                lock, 
                std::chrono::seconds(10),
                [&] { return cmd_response_received_; }
            );
            if (!response_valid) {
                std::cout << "GET /start: Timeout - No response from device\n";
                res.set_content("GET /start: Timeout - No response from device\n", "text/plain");
                res.status = 500;
                return;
            }

            // Check device's response
            if (cmd_response_.find("ok") != std::string::npos) {
                is_reading_.store(true);  // Enable reading flag
                std::cout << "GET /start: Reading started\n";
                res.set_content("GET /start: Reading started\n", "text/plain");
                res.status = 200;
            } else {
                std::cout << "GET /start: Device error - " << cmd_response_  << "\n";
                res.set_content("GET /start: Device error - " + cmd_response_ +"\n", "text/plain");
                res.status = 500;
            }
            
        } catch (const std::exception& e) {
            std::cout << "GET /start: Error sending start command - " << e.what() << "\n";
            res.set_content("GET /start: Error sending start command: " + std::string(e.what()) + "\n", "text/plain");
            res.status = 500; // Internal Server Error
        }
    });

    svr_.Get("/stop", [&](const httplib::Request &, httplib::Response &res) {
        if (!isReading()) {
            res.set_content("GET /stop: Already stopped - was not reading before request\n", "text/plain");
            std::cout << "GET /stop: Already stopped - was not reading before request\n";
            res.status = 400; // Bad Request
            return;
        }
        try {
            std::unique_lock<std::mutex> lock(cmd_mutex_);
            // Send Stop command request
            pending_cmd_ = "$1";
            cmd_response_received_ = false;
            serial_.sendData("$1\n"); 

            // Wait for response or timeout
            bool response_valid = cmd_cv_.wait_for(
                lock, 
                std::chrono::seconds(10),
                [&] { return cmd_response_received_; }
            );
            if (!response_valid) {
                std::cout << "GET /stop: Timeout - No response from device\n";
                res.set_content("GET /stop: Timeout - No response from device\n", "text/plain");
                res.status = 500;
                return;
            }

            // Check device's response
            if (cmd_response_.find("ok") != std::string::npos) {
                is_reading_.store(false);  // Disable reading flag
                std::cout << "GET /stop: Reading stopped\n";
                res.set_content("GET /stop: Reading stopped\n", "text/plain");
                res.status = 200;
            } else {
                std::cout << "GET /stop: Device error - " << cmd_response_ << "\n";
                res.set_content("GET /stop: Device error - " + cmd_response_ +"\n", "text/plain");
                res.status = 500;
            }
            
        } catch (const std::exception& e) {
            std::cout << "GET /stop: Error sending stop command - " << e.what() << "\n";
            res.set_content("GET /stop: Error sending stop command - " + std::string(e.what()) + "\n", "text/plain");
            res.status = 500;
        }
    });
    
    svr_.Get("/messages", [&](const httplib::Request &req, httplib::Response &res) {
        if (!req.has_param("limit")) {
            res.status = 400; // Bad Request
            std::cout << "GET /messages: Missing 'limit' parameter\n";
            res.set_content("GET /messages:  Missing 'limit' parameter\n", "text/plain");
            return;
        }
        int limit;
        try {
            limit = std::stoi(req.get_param_value("limit"));
            if (limit <= 0) throw std::invalid_argument("Limit must be positive");
        } catch (const std::exception &e) {
            res.status = 400; // Bad Request
            std::cout << "GET /messages: Invalid 'limit' parameter: " << e.what() << "\n";
            res.set_content("GET /messages: Invalid 'limit' parameter: " + std::string(e.what()) + "\n", "text/plain");
            return;
        }
        try {
            auto messages = db_manager_.getLastNMessages(limit);
            if(messages.empty()){
                res.status = 200;
                std::cout << "GET /messages: No Messages with Given Port,Frequency,Debug\n";
                res.set_content("GET /messages: No Messages with Given Port,Frequency,Debug\n", "text/plain");
                return;
            }
            nlohmann::json jsonArray = nlohmann::json::array();
            for (const auto& msg : messages) {
                nlohmann::json jsonObj;
                jsonObj["pressure"] = static_cast<float>(msg.pressure);
                jsonObj["temperature"] = static_cast<float>(msg.temperature);
                jsonObj["velocity"] = static_cast<float>(msg.velocity);
                jsonObj["timestamp"] = msg.timestamp;
                jsonArray.push_back(jsonObj);
            }
            res.status = 200;
            std::cout << "GET /messages: Returned " << limit << " Message(-s) Successfully\n";
            res.set_content(jsonArray.dump(), "application/json");
        } catch (const std::exception &e) {
            res.status = 500; // Internal Server Error
            std::cout << "GET /messages: Error retrieving messages - " << e.what() << "\n";
            res.set_content("GET /messages: Error retrieving messages - " + std::string(e.what()) + "\n", "text/plain");
        }
    });
    
    svr_.Get("/device", [&](const httplib::Request &req, httplib::Response &res) {
        try {
            auto last10 = db_manager_.getLastNMessages(10);
            nlohmann::json responseJson;
            responseJson["curr_config"] = {
                {"frequency", frequency_},
                {"debug", debug_}
            };
            if (!last10.empty()) {
                const auto& latest = last10.front(); // Latest is the first due to DESC order
                responseJson["latest"] = {
                    {"pressure", static_cast<float>(latest.pressure)},
                    {"temperature", static_cast<float>(latest.temperature)},
                    {"velocity", static_cast<float>(latest.velocity)}
                };
                // Sum up everything from last 10 messages
                double sumP = 0.0, sumT = 0.0, sumV = 0.0;
                for (const auto& msg : last10) {
                    sumP += static_cast<float>(msg.pressure);
                    sumT += static_cast<float>(msg.temperature);
                    sumV += static_cast<float>(msg.velocity);
                }
                // Divide everything by 10 (if exists)
                int count = last10.size();
                if(last10.size() < 10){
                    sumP = 0;
                    sumT = 0;
                    sumV = 0;
                }
                responseJson["mean_last_10"] = {
                    {"pressure", sumP / count},
                    {"temperature", sumT / count},
                    {"velocity", sumV / count}
                };
            } else {
                responseJson["latest"] = {
                    {"pressure", nullptr},
                    {"temperature", nullptr},
                    {"velocity", nullptr}
                };
                responseJson["mean_last_10"] = {
                    {"pressure", nullptr},
                    {"temperature", nullptr},
                    {"velocity", nullptr}
                };
            }
            res.status = 200;
            std::cout << "GET /device: Returned Metadata Successfully\n";
            res.set_content(responseJson.dump(), "application/json");
        } catch (const std::exception &e) {
            res.status = 500;
            std::cout << "GET /device: Error retrieving device metadata - " << e.what() << "\n";
            res.set_content("GET /device: Error retrieving device metadata - " + std::string(e.what()) + "\n", "text/plain");
        }
    });
    
    svr_.Put("/configure", [&](const httplib::Request &req, httplib::Response &res) {
        try {
            auto jsonBody = nlohmann::json::parse(req.body);
            if (!jsonBody.contains("frequency") || !jsonBody.contains("debug")) {
                res.status = 400;
                std::cout << "PUT /configure: Missing required parameters: frequency and debug\n";
                res.set_content("PUT /configure: Missing required parameters: frequency and debug\n", "text/plain");
                return;
            }
            int newFrequency = jsonBody["frequency"];
            bool newDebug = jsonBody["debug"];
            if (newFrequency <= 0 || newFrequency > 255) {
                res.status = 400;
                std::cout << "PUT /configure: Frequency must be between 1 and 255\n";
                res.set_content("PUT /configure: Frequency must be between 1 and 255\n", "text/plain");
                return;
            }
            std::unique_lock<std::mutex> lock(cmd_mutex_);
            // Prepare and send the configure command
            pending_cmd_ = "$2," + std::to_string(newFrequency) + "," + (newDebug ? "1" : "0");
            cmd_response_received_ = false;
            serial_.sendData(pending_cmd_ + "\n"); // E.g, sends "$2,v1,v2\n" over UART

            // Wait for response or timeout (10 seconds)
            bool response_valid = cmd_cv_.wait_for(
                lock, 
                std::chrono::seconds(10),
                [&] { return cmd_response_received_; }
            );
            
            if (!response_valid) {
                res.status = 500;
                std::cout << "PUT /configure: Timeout: No response from device\n";
                res.set_content("PUT /configure: Timeout: No response from device\n", "text/plain");
                
            } else {
                if (cmd_response_ == "ok") {
                    // Upd server configuration after successful response
                    frequency_ = newFrequency;
                    debug_ = newDebug;

                    // Upd database-manager
                    db_manager_.updFrequency(frequency_);
                    db_manager_.updDebug(debug_);

                    // Upd serial port
                    serial_.updBaudRate(frequency_ * 1000);

                    std::cout << "PUT /configure: Configuration updated and sent to device successfully\n";
                    res.set_content("PUT /configure: Configuration updated and sent to device successfully\n", "text/plain");
                    res.status = 200;
                    
                } else if (cmd_response_ == "invalid command") {
                    res.status = 400;
                    std::cout << "PUT /configure: Device rejected the configuration\n";
                    res.set_content("PUT /configure: Device rejected the configuration\n", "text/plain");
                    
                } else {
                    res.status = 500;
                    std::cout << "PUT /configure: Unexpected response: " << cmd_response_ << "\n";
                    res.set_content("PUT /configure: Unexpected response: " + cmd_response_ + "\n", "text/plain");
                }
            }

            
        } catch (const std::exception& e) {
            res.status = 500;
            std::cout << "PUT /configure: Error - " << e.what() << "\n";
            res.set_content("PUT /configure: Error - " + std::string(e.what()) + "\n", "text/plain");
        }
    });
}
// Returns true if the hostname is valid (i.e. resolvable)
bool HTTPServer::isValidHostname(const std::string &hostname) {
    if (hostname.empty()) return false;  // Reject empty names

    struct addrinfo hints;
    struct addrinfo *result = nullptr;

    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // Any type of stream socket

    // Try to resolve the hostname using a well-known service (e.g., "http")
    int ret = getaddrinfo(hostname.c_str(), "http", &hints, &result);
    if (ret != 0) {
        return false;
    }
    freeaddrinfo(result);
    return true;
}

// Getters
int HTTPServer::getPort() const { return port_; }
std::string HTTPServer::getHost() const { return host_; }

// Reads '$[command],[status]' from serial after /start /stop /configure
// Sent a request to the connected device 
std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}
