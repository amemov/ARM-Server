#include "serial_interface.cpp"
#include "server_api.cpp"  // Assumes this includes DatabaseManager and HTTPServer definitions
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <signal.h>
#include <errno.h>

// Function to parse the received message - simplified
bool parseMessage(const std::string& message, float& pressure, float& temperature, float& velocity) {
    std::istringstream iss(message);
    char comma;
    return (iss >> pressure >> comma >> temperature >> comma >> velocity) && (comma == ',');
}

// Signal handler for graceful shutdown
volatile sig_atomic_t stop_flag = 0;
void signalHandler(int signum) {
    stop_flag = 1;
}

int main() {
    // Set up signal handling for SIGINT and SIGTERM
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Default configuration values
    uint8_t frequency = 115;  // Derived from 115000 / 1000 as in original code
    bool debug = false;       // Default: debug mode disabled

    try {
        // **Step 0: Get Environment Variables. Validate them
        // getEnv blah blah
        // **Step 0.5: Get CLI aguments. If valid, should overwrite Environment variables
        // mess with agc argv (oh no... =( )

        // **Step 1: Initialize SerialInterface**
        // Default port "/dev/ttyS11" and baud rate 115000
        SerialInterface serial("/dev/ttyS11", 38400);
        std::cout << "Serial port initialized: " << serial.getPortName() 
                  << (serial.isVirtual() ? " (virtual)" : " (physical)") << "\n";

        // **Step 2: Initialize DatabaseManager**
        // Default database path "database.db", using port name from SerialInterface
        DatabaseManager db_manager("database.db", serial.getPortName(), frequency, debug);

        // **Step 3: Initialize HTTPServer**
        // Default host "localhost" and port 7099
        HTTPServer server("localhost", 7099, db_manager, frequency, debug, serial);

        // **Step 4: Start the HTTP Server**
        server.start();
        std::cout << "HTTP server started on localhost:7099\n";


        // **Step 5: Serial Port Reading - Answers to requests from device and Messages**
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
                                // CMD matches, check where to map the status
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