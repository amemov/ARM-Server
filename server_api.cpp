#include "server_api.hpp"

// DatabaseManager Implementation
DatabaseManager::DatabaseManager(const std::string& db_path, 
                                const std::string& port_name,
                                int& frequency, bool& debug)
    : port_name_(port_name), frequency_(frequency), debug_(debug) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("Database error: " + std::string(sqlite3_errmsg(db_)));
    }
    createTableIfNotExists();
    prepareStatements();
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

void DatabaseManager::prepareStatements() {
    const char* sql = 
        "INSERT INTO SensorData (Port, Frequency, Debug, Pressure, Temperature, Velocity, Timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";
    
    if (sqlite3_prepare_v2(db_, sql, -1, &insert_stmt_, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare insert statement: " + 
                                 std::string(sqlite3_errmsg(db_)));
    }
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
void DatabaseManager::updFrequency(const int& freq){
    frequency_ = freq;
}
void DatabaseManager::updDebug(const bool& debug){
    debug_ = debug;
}

// HTTPServer Implementation. By default, doesn't read until /start command
HTTPServer::HTTPServer(const std::string& host, int port,
                        DatabaseManager& db_manager,
                        int& frequency, bool& debug,
                        SerialInterface& serial)
    : host_(host), port_(port), db_manager_(db_manager),
    frequency_(frequency), debug_(debug), is_reading_(false),
    serial_(serial) {}

HTTPServer::~HTTPServer(){
    stop();
}

void HTTPServer::start() {
    registerEndpoints();
    server_thread_ = std::thread([this]() {
        svr_.listen(host_.c_str(), port_);
    });
    read_thread_ = std::thread([this]() { 
        readFromSerial(); 
    });
}

void HTTPServer::stop() {
    read_cmd = true;
    svr_.stop();
    if (read_thread_.joinable()){
        read_thread_.join();
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
}
bool HTTPServer::isReading() const { 
    return is_reading_.load(); 
}

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
                res.set_content("GET /start: Device error - " + cmd_response_ +"\n", "text/plain");
                res.status = 500;
            }
            
        } catch (const std::exception& e) {
            res.set_content("Error sending start command: " + std::string(e.what()), "text/plain");
            std::cout << "GET /start: Error: " << e.what() << "\n";
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
                res.set_content("GET /stop: Device error - " + cmd_response_ +"\n", "text/plain");
                res.status = 500;
            }
            
        } catch (const std::exception& e) {
            res.set_content("Error sending stop command: " + std::string(e.what()), "text/plain");
            std::cout << "GET /stop: Error: " << e.what() << "\n";
            res.status = 500;
        }
    });
    
    svr_.Get("/messages", [&](const httplib::Request &req, httplib::Response &res) {
        if (!req.has_param("limit")) {
            res.status = 400; // Bad Request
            std::cout << "GET /messages: Missing 'limit' parameter\n";
            res.set_content("Missing 'limit' parameter", "text/plain");
            return;
        }
        int limit;
        try {
            limit = std::stoi(req.get_param_value("limit"));
            if (limit <= 0) throw std::invalid_argument("Limit must be positive");
        } catch (const std::exception &e) {
            res.status = 400; // Bad Request
            std::cout << "GET /messages: Invalid 'limit' parameter: " << e.what() << "\n";
            res.set_content("Invalid 'limit' parameter: " + std::string(e.what()), "text/plain");
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
            std::cout << "GET /messages: Error retrieving messages: " << e.what() << "\n";
            res.set_content("Error retrieving messages: " + std::string(e.what()), "text/plain");
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
            std::cout << "GET /device: Error retrieving device metadata: " << e.what() << "\n";
            res.set_content("Error retrieving device metadata: " + std::string(e.what()), "text/plain");
        }
    });
    
    svr_.Put("/configure", [&](const httplib::Request &req, httplib::Response &res) {
        try {
            auto jsonBody = nlohmann::json::parse(req.body);
            if (!jsonBody.contains("frequency") || !jsonBody.contains("debug")) {
                res.status = 400;
                std::cout << "PUT /configure: Missing required parameters: frequency and debug\n";
                res.set_content("Missing required parameters: frequency and debug", "text/plain");
                return;
            }
            int newFrequency = jsonBody["frequency"];
            bool newDebug = jsonBody["debug"];
            if (newFrequency <= 0 || newFrequency > 255) {
                res.status = 400;
                std::cout << "PUT /configure: Frequency must be between 1 and 255\n";
                res.set_content("Frequency must be between 1 and 255", "text/plain");
                return;
            }
            std::unique_lock<std::mutex> lock(cmd_mutex_);
            // Prepare and send the configure command
            pending_cmd_ = "$2," + std::to_string(newFrequency) + "," + (newDebug ? "1" : "0");
            cmd_response_received_ = false;
            serial_.sendData(pending_cmd_ + "\n"); // Send "$2,v1,v2\n"

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
                    // Update server configuration only after successful response
                    frequency_ = newFrequency;
                    debug_ = newDebug;
                    std::cout << "PUT /configure: Configuration updated and sent to device successfully\n";
                    res.set_content("PUT /configure: Configuration updated and sent to device successfully\n", "text/plain");
                    res.status = 200;
                    
                } else if (cmd_response_ == "invalid command") {
                    res.status = 400;
                    std::cout << "PUT /configure: Device rejected the configuration\n";
                    res.set_content("PUT /configure: Device rejected the configuration\n", "text/plain");
                    
                } else {
                    res.status = 500;
                    std::cout << "PUT /configure:  Unexpected response: " << cmd_response_ << "\n";
                    res.set_content("PUT /configure:  Unexpected response: " + cmd_response_ + "\n", "text/plain");
                }
            }

            
        } catch (const std::exception& e) {
            res.status = 500;
            std::cout << "PUT /configure: Error: " << e.what() << "\n";
            res.set_content("Error: " + std::string(e.what()), "text/plain");
        }
    });
}
// Reads '$[command],[status]' from serial after /start /stop /configure
// Sent a request to the connected device 
std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}
void HTTPServer::readFromSerial() {
    char buffer[256];
    std::string remaining_data;
    while (!read_cmd) {  // Use proper termination condition
        int bytes_read = read(serial_.getFileDescriptor(), buffer, sizeof(buffer));
        if (bytes_read > 0) {
            remaining_data += std::string(buffer, bytes_read);
            size_t newline_pos;
            while ((newline_pos = remaining_data.find('\n')) != std::string::npos) {
                std::string line = remaining_data.substr(0, newline_pos);
                remaining_data.erase(0, newline_pos + 1);

                // Debug raw input
                std::cout << "DEBUG: Processing line: " << line << "\n";

                if (line.empty()) continue;
                
                // Handle all command responses
                if (line[0] == '$') {
                    std::lock_guard<std::mutex> lock(cmd_mutex_);
                    
                    // Split into command and status
                    size_t first_comma = line.find(',');
                    std::string received_prefix = line.substr(0, first_comma);
                    std::string status;

                    // For configure command ($2)
                    if (received_prefix == "$2") {
                        size_t last_comma = line.find_last_of(',');
                        if (last_comma != std::string::npos && last_comma > first_comma) {
                            received_prefix = line.substr(0, last_comma);
                            status = line.substr(last_comma + 1);
                        }
                    } 
                    // For start/stop commands ($0/$1)
                    else if (received_prefix == "$0" || received_prefix == "$1") {
                        if (first_comma != std::string::npos) {
                            status = line.substr(first_comma + 1);
                        }
                    }

                    // Normalize status
                    status = trim(status);
                    std::transform(status.begin(), status.end(), status.begin(), ::tolower);

                    // Match with pending command
                    if (received_prefix == pending_cmd_) {
                        if (status == "ok") {
                            cmd_response_ = "ok";
                        } else if (status == "invalid command") {
                            cmd_response_ = "invalid command";
                        } else {
                            cmd_response_ = "invalid_response";
                        }
                        cmd_response_received_ = true;
                        cmd_cv_.notify_one();
                    }
                }
            }
        }
    }
}