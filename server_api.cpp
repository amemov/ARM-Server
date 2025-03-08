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
                      "WHERE Port = ? AND Frequency = ? ORDER BY Timestamp DESC LIMIT ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
    }
    sqlite3_bind_text(stmt, 1, port_name_.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, frequency_);
    sqlite3_bind_int(stmt, 3, n);
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

// HTTPServer Implementation
HTTPServer::HTTPServer(const std::string& host, int port,
                       DatabaseManager& db_manager,
                       int& frequency, bool& debug)
    : host_(host), port_(port), db_manager_(db_manager),
      frequency_(frequency), debug_(debug) {}

void HTTPServer::registerEndpoints() {
    svr_.Get("/start", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("'/start' got triggered\n", "text/plain");
    });

    svr_.Get("/stop", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("'/stop' got triggered\n", "text/plain");
        res.status = 200;
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
            std::cout << "GET /messages: Returned " << limit << " Messages Successfully\n";
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
                double sumP = 0.0, sumT = 0.0, sumV = 0.0;
                for (const auto& msg : last10) {
                    sumP += static_cast<float>(msg.pressure);
                    sumT += static_cast<float>(msg.temperature);
                    sumV += static_cast<float>(msg.velocity);
                }
                int count = last10.size();
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
            frequency_ = newFrequency;
            debug_ = newDebug;
            res.status = 200;
            std::cout << "PUT /configure: Configuration updated successfully\n";
            res.set_content("Configuration updated successfully", "text/plain");
        } catch (const std::exception &e) {
            res.status = 400;
            std::cout << "PUT /configure: Invalid JSON format: " << e.what() << "\n";
            res.set_content("Invalid JSON format: " + std::string(e.what()), "text/plain");
        }
    });
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