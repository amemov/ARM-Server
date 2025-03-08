#ifndef SERIAL_INTERFACE_HPP
#define SERIAL_INTERFACE_HPP

#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <pty.h>
#include <iostream>
#include <cstring>
#include <sys/stat.h>
#include <sys/sysmacros.h>

// Forward declaration for termios2
struct termios2;

class SerialInterface {
private:
    std::string port_name;
    int baud_rate;
    bool is_virtual;
    int fd;
    int master_fd;

    // Private methods
    bool validatePort();
    bool setupPhysicalPort();
    bool setupVirtualPort();
    void setCustomBaudRate();
    static bool isPTY(int fd);

public:
    SerialInterface(const std::string& port = "/dev/ttyS11", int baud = 115000);
    ~SerialInterface();

    // Getters
    int getFileDescriptor() const;
    const std::string& getPortName() const;
    bool isVirtual() const;
    int getBaudRate() const;

    // Disallow copying
    SerialInterface(const SerialInterface&) = delete;
    SerialInterface& operator=(const SerialInterface&) = delete;
};

#endif // SERIAL_INTERFACE_HPP