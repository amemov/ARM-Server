#include "server_api.hpp" 
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <signal.h>
#include <errno.h>
#include <cstdlib> 

// Function to parse the received message - simplified
bool parseMessage(const std::string& message, float& pressure, float& temperature, float& velocity) {
    std::istringstream iss(message);
    char comma;
    return (iss >> pressure >> comma >> temperature >> comma >> velocity) && (comma == ',');
}

// Signal handler for graceful shutdown
// Can shutdown using: pgrep -f server 
//                     kill -SIGINT 'number of the process'
volatile sig_atomic_t stop_flag = 0;
void signalHandler(int signum) {
    stop_flag = 1;
}

int main(int argc, char* argv[]) {
    // Set up signal handling for SIGINT and SIGTERM
    signal(SIGINT, signalHandler); 
    signal(SIGTERM, signalHandler);
    bool debug = false;       // Default: debug mode disabled

    // Define default configuration values
    const std::string default_port_name = "/dev/ttyUSB0";
    const int default_baud_rate = 115000;
    const std::string default_host_name = "localhost";
    const std::string default_db_path = "database.db";
    const int default_server_port = 7100;

    // Configuration values that can be overriden via CLI and Environment Vars
    // Except frequency - is is a derivative of baud_rate
    std::string port_name = default_port_name;
    int baud_rate = default_baud_rate;
    // validate frequency before assigning. if baud to big, set big to lesser value
    uint8_t frequency = 115;  // Derived from 115000 / 1000 as in original code. 
    std::string host_name = default_host_name;
    std::string db_path = default_db_path;
    int server_port = default_server_port;

    try {
        /*Step 0: Get Environment Variables. Validate them */
        // PORT_NAME (string)
        if (const char* env_port = std::getenv("PORT_NAME")) {
            port_name = env_port;
        }
        
        // BAUDRATE (numeric, must be <= 255000)
        if (const char* env_baud = std::getenv("BAUDRATE")) {
            try {
                int candidate = std::stoi(env_baud);
                if (candidate > 255000) {
                    std::cerr << "BAUDRATE value " << candidate 
                            << " exceeds maximum allowed (255000); using default " 
                            << default_baud_rate << "\n";
                } else {
                    baud_rate = candidate;
                    frequency = static_cast<uint8_t>(baud_rate / 1000);
                }
            } catch (const std::invalid_argument& e) {
                std::cerr << "Invalid BAUDRATE value (" << env_baud 
                        << "); using default " << default_baud_rate << "\n";
            } catch (const std::out_of_range& e) {
                std::cerr << "BAUDRATE value out of range (" << env_baud 
                        << "); using default " << default_baud_rate << "\n";
            }
        }
        
        // HOST_NAME (string)
        if (const char* env_host = std::getenv("HOST_NAME")) {
            host_name = env_host;
        }
        
        // HTTP_PORT (numeric)
        if (const char* env_http_port = std::getenv("HTTP_PORT")) {
            try {
                server_port = std::stoi(env_http_port);
            } catch (const std::invalid_argument& e) {
                std::cerr << "Invalid HTTP_PORT value (" << env_http_port 
                        << "); using default " << default_server_port << "\n";
            } catch (const std::out_of_range& e) {
                std::cerr << "HTTP_PORT value out of range (" << env_http_port 
                        << "); using default " << default_server_port << "\n";
            }
        }
        
        // DB_PATH (string)
        if (const char* env_db = std::getenv("DB_PATH")) {
            db_path = env_db;
        }    
        /* Step 0.5: Get CLI aguments. If valid, should overwrite Environment variables */
        // Expected order: [Port-Name] [Baud-Rate] [HTTP-Host-Name] [HTTP-Port] [Database-Path]
        if (argc > 1) {
            port_name = argv[1];
        }
        if (argc > 2) {
            try {
                int candidate = std::stoi(argv[2]);
                if (candidate > 255000) {
                    std::cerr << "CLI BAUDRATE value " << candidate 
                              << " exceeds maximum allowed (255000); using previous value " 
                              << baud_rate << "\n";
                } else {
                    baud_rate = candidate;
                    frequency = static_cast<uint8_t>(baud_rate / 1000);
                }
            } catch (const std::exception& e) {
                std::cerr << "Invalid CLI BAUDRATE value (" << argv[2] 
                          << "); using previous value " << baud_rate << "\n";
            }
        }
        if (argc > 3) {
            host_name = argv[3];
        }
        if (argc > 4) {
            try {
                server_port = std::stoi(argv[4]);
            } catch (const std::exception& e) {
                std::cerr << "Invalid CLI HTTP_PORT value (" << argv[4] 
                          << "); using previous value " << server_port << "\n";
            }
        }
        if (argc > 5) {
            db_path = argv[5];
        }
        // DEBUG
        std::cout << "Final Configuration:" << std::endl;
        std::cout << "Port Name: " << port_name << std::endl;
        std::cout << "Baud Rate: " << baud_rate << std::endl;
        std::cout << "Frequency: " << static_cast<int>(frequency) << std::endl;
        std::cout << "HTTP Host Name: " << host_name << std::endl;
        std::cout << "HTTP Port: " << server_port << std::endl;
        std::cout << "Database Path: " << db_path << std::endl;

        /* Step 1: Initialize SerialInterface */
        SerialInterface serial(port_name, baud_rate);
        std::cout << "Serial port initialized: " << serial.getPortName() 
                  << (serial.isVirtual() ? " (virtual)" : " (physical)") << "\n";

        /* Step 2: Initialize DatabaseManager */
        DatabaseManager db_manager(db_path, serial.getPortName(), frequency, debug);

        /* Step 3: Initialize HTTPServer */
        HTTPServer server(host_name, server_port, db_manager, frequency, debug, serial);

        /* Step 4: Start the HTTP Server */
        server.start();
        std::cout << "HTTP server started on "<< server.getHost() << ":" << server.getPort() << "\n";


        /* Step 5: Serial Port Reading - Answers to requests from device and Messages */
        char buffer[256];
        std::string data;
        while (!stop_flag) {
            int bytesRead = read(serial.getFileDescriptor(), buffer, sizeof(buffer) - 1);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                data += buffer;

                size_t startPos = data.find('$');
                while (startPos != std::string::npos) {
                    data = data.substr(startPos);
                    startPos = 0;

                    size_t endPos = data.find('\n', startPos);
                    if (endPos != std::string::npos) {
                        std::string message = data.substr(startPos, endPos - startPos);
                        data.erase(0, endPos + 1);

                        if (message[0] == '$') {
                            std::lock_guard<std::mutex> lock(server.cmd_mutex_); // Access server's cmd variables
                            if (!server.pending_cmd_.empty()) {
                                // Process as command response
                                size_t first_comma = message.find(',');
                                std::string received_prefix = message.substr(0, first_comma);
                                std::string status;

                                // Base case - we got invalid response to our prompt
                                if (received_prefix != server.pending_cmd_){
                                    server.cmd_response_ = "invalid_response - commands don't match";
                                    server.cmd_response_received_ = true;
                                    server.cmd_cv_.notify_one();
                                    server.pending_cmd_.clear(); // Reset pending command
                                    continue;
                                }
                                // CMD matches, get the status of the request
                                if (received_prefix == "$2") {
                                    size_t last_comma = message.find_last_of(',');
                                    if (last_comma != std::string::npos && last_comma > first_comma) {
                                        received_prefix = message.substr(0, last_comma);
                                        status = message.substr(last_comma + 1);
                                    }
                                } else if (received_prefix == "$0" || received_prefix == "$1") {
                                    if (first_comma != std::string::npos) {
                                        status = message.substr(first_comma + 1);
                                    }
                                }

                                status = trim(status);
                                std::transform(status.begin(), status.end(), status.begin(), ::tolower);
                                
                                if (received_prefix == server.pending_cmd_) {
                                    if (status == "ok") {
                                        server.cmd_response_ = "ok";
                                    } else if (status == "invalid command") {
                                        server.cmd_response_ = "invalid command";
                                    } else {
                                        server.cmd_response_ = "invalid_response - undefined status";
                                    }
                                    server.cmd_response_received_ = true;
                                    server.cmd_cv_.notify_one();
                                    server.pending_cmd_.clear(); // Reset pending command
                                }
                            } else if (server.isReading()) {
                                // Process as sensor data
                                std::string sensor_message = message.substr(1); // Remove '$'
                                float pressure, temperature, velocity;
                                if (parseMessage(sensor_message, pressure, temperature, velocity)) {
                                    __fp16 h_pressure = static_cast<__fp16>(pressure);
                                    __fp16 h_temperature = static_cast<__fp16>(temperature);
                                    __fp16 h_velocity = static_cast<__fp16>(velocity);

                                    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch()).count();

                                    DatabaseManager::SensorData sensorData = {
                                        h_pressure, h_temperature, h_velocity, timestamp
                                    };
                                    if (db_manager.storeSensorData(sensorData)) {
                                        std::cout << "Data stored: P=" << pressure 
                                                  << ", T=" << temperature 
                                                  << ", V=" << velocity << "\n";
                                    } else {
                                        std::cerr << "Failed to store data\n";
                                    }
                                } else {
                                    std::cerr << "Invalid message format: " << sensor_message << "\n";
                                }
                            }
                        }
                        startPos = data.find('$');
                    } else {
                        break; // Incomplete message
                    }
                }
            } else if (bytesRead < 0) {
                std::cerr << "Read error: " << strerror(errno) << "\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        server.stop();
        std::cout << "HTTP server stopped\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}