#include "server_api.cpp"
#include "serial_interface.cpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <termios.h>
#include <pty.h>
#include <errno.h>
#include <cstring>
#include <sys/stat.h>
#include <iostream>
#include <string>

#include <sstream>
#include <vector>

#include <sqlite3.h>
#include <chrono>
// Manual definitions for termios2 and constants
struct termios2 {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[19];
    speed_t c_ispeed;
    speed_t c_ospeed;
};

#ifndef TCGETS2
#define TCGETS2 _IOR('T', 0x2A, struct termios2)
#endif
#ifndef TCSETS2
#define TCSETS2 _IOW('T', 0x2B, struct termios2)
#endif
#ifndef BOTHER
#define BOTHER 0x1000
#endif

// Checks if physical port exists (assuming no one messes with image before server launch)
bool fileExists(const std::string &filename) {
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0);
}

// Check if device is a PTY using its major number
bool isPTY(int fd) {
    struct stat st;
    if (fstat(fd, &st) < 0) return false;
    
    // Major number for PTY devices (136 on most Linux systems)
    constexpr int PTY_MAJOR = 136;  // Adjust this for your system if needed
    return major(st.st_rdev) == PTY_MAJOR;
}

// Function to parse the received message
bool parseMessage(const std::string &message, float &pressure, float &temperature, float &velocity) {
    std::stringstream ss(message);
    std::string token;
    std::vector<float> values;

    while (std::getline(ss, token, ',')) {
        try {
            values.push_back(std::stof(token));  // Convert string to float
        } catch (const std::exception &e) {
            std::cerr << "Error parsing value: " << e.what() << "\n";
            return false;
        }
    }

    if (values.size() != 3) return false;

    pressure = values[0];
    temperature = values[1];
    velocity = values[2];
    return true;
}

int main() {
    /* INTERFCAE SPECIFIC CODE */
    const std::string portName = "/dev/ttyS11";
    int fd = -1;
    int master_fd = -1;
    bool isVirtualPort = false;

    if (fileExists(portName)) {
        fd = open(portName.c_str(), O_RDWR | O_NOCTTY);
        if (fd < 0) {
            std::cerr << "Error opening " << portName << ": " << strerror(errno) << "\n";
            return 1;
        }
        std::cout << portName << " exists. Opening it...\n";
        
        // Determine if it's a PTY even if it existed previously
        isVirtualPort = isPTY(fd);
    } else {
            master_fd = posix_openpt(O_RDWR | O_NOCTTY);
            if (master_fd < 0) {
                std::cerr << "Error creating PTY master: " << strerror(errno) << "\n";
                return 1;
            }
            if (grantpt(master_fd) < 0 || unlockpt(master_fd) < 0) {
                std::cerr << "PTY setup error: " << strerror(errno) << "\n";
                close(master_fd);
                return 1;
            }
            char *slaveName = ptsname(master_fd);
            if (!slaveName) {
                std::cerr << "Error getting slave PTY name: " << strerror(errno) << "\n";
                close(master_fd);
                return 1;
            }
            std::cout << "Created virtual port: " << slaveName << "\n";
            fd = open(slaveName, O_RDWR | O_NOCTTY);
            if (fd < 0) {
                std::cerr << "Error opening virtual port: " << strerror(errno) << "\n";
                close(master_fd);
                return 1;
            }
            isVirtualPort = true;
    }

    // Only set baud rate for non-PTY devices
    if (!isVirtualPort) {
        struct termios2 tio2;
        if (ioctl(fd, TCGETS2, &tio2) < 0) {
            std::cerr << "TCGETS2 error: " << strerror(errno) << "\n";
            close(fd);
            if (master_fd >= 0) close(master_fd);
            return 1;
        }

        tio2.c_cflag &= ~CBAUD;
        tio2.c_cflag |= BOTHER;

        //speeds are unsigned integers - use size_t 
        tio2.c_ispeed = tio2.c_ospeed = 115000; 

        if (ioctl(fd, TCSETS2, &tio2) < 0) {
            std::cerr << "TCSETS2 error: " << strerror(errno) << "\n";
            close(fd);
            if (master_fd >= 0) close(master_fd);
            return 1;
        }
        std::cout << "Baud rate set to 115000.\n";
    } else {
        std::cout << "PTY detected. Skipping baud rate settings.\n";
    }

    //std::cout << "Press Enter to exit...\n";
    //std::cin.get();

    /* MESSAGE TRANSMISSION OVER UART */
    /* STORE MESSAGES IN DATABASE */
    /*Messages are being sent from the embedded device continuously over UART.
    Each message contains 3 values separated by a comma. Each value is a
    number between 0.0 and 1000.0 (float16). Each value represents a different
    sensor reading: pressure, temperature, velocity*/
    
    // TODO: CONVERT TO FLOAT16: Range [-65504:65504]
    // TODO: SANITIZE TABLE NAME TO PREVENET ATTACKS 
    // REFORMAT MESSAGE READING! Follow format - '$123.4,567.8,999.9\n'
    //float f = 3.14159f;  // Read as float
    //__fp16 h = f;  // Convert data into float16
    
    // Open Database
    sqlite3 *db = nullptr;
    int rc = sqlite3_open("database.db", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return 1;
    }

    // Build your dynamic table name from command-line or environment variables
    std::string tableName = "SensorData"; // Default name
    // Assume user can override tableName here...

    // Create SQL statement dynamically
    std::stringstream sqlStream;
    sqlStream << "CREATE TABLE IF NOT EXISTS " << tableName << " ("
            << "Port TEXT NOT NULL, "
            << "Frequency INTEGER NOT NULL, "
            << "Debug INTEGER NOT NULL CHECK (Debug IN (0, 1)), "
            << "Pressure BLOB, "
            << "Temperature BLOB, "
            << "Velocity BLOB, "
            << "Timestamp INTEGER NOT NULL"  // <-- Use INTEGER instead of TEXT
            << ");";
    std::string sql = sqlStream.str();

    // Execute the SQL command
    char *errMsg = nullptr;
    rc = sqlite3_exec(db, sql.c_str(), nullptr, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        //sqlite3_close(db);
        return 1;
    } else {
        std::cout << "Table created (or verified) successfully." << std::endl;
    }

    int debug = 1;
    int freq = 115000 / 1000;

    char buffer[256];
    std::string data;
    int counter = 0; // tmp stuff for testing DB
    while (counter < 2) {
        int bytesRead = read(fd, buffer, sizeof(buffer) - 1);
        if (bytesRead > 0) {
            counter += 1;
            buffer[bytesRead] = '\0';  // Null-terminate the string
            data += buffer;  // Append new data to the buffer

            size_t newlinePos;
            while ((newlinePos = data.find('\n')) != std::string::npos) {
                std::string message = data.substr(0, newlinePos);
                data.erase(0, newlinePos + 1);  // Remove processed data

                float pressure, temperature, velocity;
                if (parseMessage(message, pressure, temperature, velocity)) {
                    std::cout << "Received -> Pressure: " << pressure
                            << " | Temperature: " << temperature
                            << " | Velocity: " << velocity << "\n";
                    
                    __fp16 h_pressure = static_cast<__fp16>(pressure);
                    __fp16 h_temperature = static_cast<__fp16>(temperature);
                    __fp16 h_velocity = static_cast<__fp16>(velocity);
                    auto now = std::chrono::system_clock::now();
                    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                        now.time_since_epoch()
                    ).count();

                    // SQL insert statement with placeholders
                    const char* insertSQL =
                    "INSERT INTO SensorData (Port, Frequency, Debug, Pressure, Temperature, Velocity, Timestamp) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?);";

                    sqlite3_stmt *stmt = nullptr;
                    int rc = sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr);
                    if (rc != SQLITE_OK) {
                    std::cerr << "Failed to prepare insert statement: " << sqlite3_errmsg(db) << "\n";
                    // Handle error appropriately (cleanup, exit, etc.)
                    }

                    // Bind the parameters to the statement
                    rc = sqlite3_bind_text(stmt, 1, portName.c_str(), -1, SQLITE_STATIC);
                    if (rc != SQLITE_OK) {
                    std::cerr << "Failed to bind Port: " << sqlite3_errmsg(db) << "\n";
                    }
                    rc = sqlite3_bind_int(stmt, 2, freq);
                    if (rc != SQLITE_OK) {
                    std::cerr << "Failed to bind Frequency: " << sqlite3_errmsg(db) << "\n";
                    }
                    rc = sqlite3_bind_int(stmt, 3, debug);
                    if (rc != SQLITE_OK) {
                    std::cerr << "Failed to bind Debug: " << sqlite3_errmsg(db) << "\n";
                    }
                    // Bind BLOB data for pressure, temperature, and velocity.
                    // Here we're binding the address of the float variables, and the size is sizeof(float).
                    rc = sqlite3_bind_blob(stmt, 4, &h_pressure, sizeof(h_pressure), SQLITE_STATIC);
                    if (rc != SQLITE_OK) {
                    std::cerr << "Failed to bind Pressure: " << sqlite3_errmsg(db) << "\n";
                    }
                    rc = sqlite3_bind_blob(stmt, 5, &h_temperature, sizeof(h_temperature), SQLITE_STATIC);
                    if (rc != SQLITE_OK) {
                    std::cerr << "Failed to bind Temperature: " << sqlite3_errmsg(db) << "\n";
                    }
                    rc = sqlite3_bind_blob(stmt, 6, &h_velocity, sizeof(h_velocity), SQLITE_STATIC);
                    if (rc != SQLITE_OK) {
                    std::cerr << "Failed to bind Velocity: " << sqlite3_errmsg(db) << "\n";
                    }
                    rc = sqlite3_bind_int64(stmt, 7, timestamp); 
                    if (rc != SQLITE_OK) {
                    std::cerr << "Failed to bind Timestamp: " << sqlite3_errmsg(db) << "\n";
                    }

                    // Execute the statement
                    rc = sqlite3_step(stmt);
                    if (rc != SQLITE_DONE) {
                    std::cerr << "Insert failed: " << sqlite3_errmsg(db) << "\n";
                    } else {
                    std::cout << "Data inserted successfully." << std::endl;
                    }

                    // Clean up the statement
                    sqlite3_finalize(stmt);
                    
                } else {
                    std::cerr << "Invalid message format: " << message << "\n";
                }
            }
        }
        usleep(10000);  // Prevent CPU overuse (adjust as needed)
    }

    /* SERVER - HTTP / HTTPS */
    // HTTP
    httplib::Server svr;

    // HTTPS
    //httplib::SSLServer svr;

    // Commands
    svr.Get("/start", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("'/start' got triggered\n", "text/plain");
    });

    svr.Get("/stop", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("'/stop' got triggered\n", "text/plain");
    });
    
    // check how to get it working. everything else works
    svr.Get("/messages?limit=[limit]", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("'/messages?limit=[limit]' got triggered\n", "text/plain");
    });

    svr.Get("/device", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("'/device' got triggered\n", "text/plain");
    });

    // TODO: PUT CONFIGURE

    svr.listen("localhost", 7099); // default params

    while (true){

    }



    // Clean-Up
    sqlite3_close(db);
    close(fd);
    if (master_fd >= 0) close(master_fd);
    return 0;
}