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
    int frequency = 115;  // Derived from 115000 / 1000 as in original code
    bool debug = false;    // Default debug mode enabled

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


        // **Step 5: Serial Port Reading (if start invoked)**
        char buffer[256];
        std::string data;
        while (!stop_flag) {
            if (server.isReading()) {
                int bytesRead = read(serial.getFileDescriptor(), buffer, sizeof(buffer) - 1);
                if (bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    std::cout << "Read: " << buffer << "\n";  // Debug
                    data += buffer;

                    size_t startPos = data.find('$');
                    while (startPos != std::string::npos) {
                        // Discard any data before the '$'
                        data = data.substr(startPos);
                        startPos = 0; // Now process from the start of the remaining data

                        size_t endPos = data.find('\n', startPos);
                        if (endPos != std::string::npos) {
                            std::string message = data.substr(startPos + 1, endPos - startPos - 1);
                            data.erase(0, endPos + 1);

                            float pressure, temperature, velocity;
                            if (parseMessage(message, pressure, temperature, velocity)) {
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
                                std::cerr << "Invalid message format: " << message << "\n";
                            }
                            // Look for next message in remaining data
                            startPos = data.find('$');
                        } else {
                            // Incomplete message, wait for more data
                            break;
                        }
                    }
                } else if (bytesRead < 0) {
                    std::cerr << "Read error: " << strerror(errno) << "\n";
                } else {
                    std::cout << "No data available\n";  // Debug
                }
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