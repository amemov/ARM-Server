#include "serial_interface.hpp"

// termios2 structure definition
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

// IOCTL definitions
#ifndef TCGETS2
#define TCGETS2 _IOR('T', 0x2A, struct termios2)
#endif
#ifndef TCSETS2
#define TCSETS2 _IOW('T', 0x2B, struct termios2)
#endif
#ifndef BOTHER
#define BOTHER 0x1000
#endif

SerialInterface::SerialInterface(const std::string& port, int baud) 
    : port_name(port), baud_rate(baud), is_virtual(false), fd(-1), master_fd(-1) 
{
    const std::string default_port = "/dev/ttyUSB0"; 
    struct stat buffer;

    // First check if the user-provided port exists
    if (stat(port.c_str(), &buffer) == 0) {
        // Try to open user-provided port
        fd = open(port.c_str(), O_RDWR | O_NOCTTY);
        if (fd < 0) {
            std::cerr << "Error opening port '" << port << "'. Trying default port '" << default_port << "'\n";
            port_name = default_port;
            fd = open(default_port.c_str(), O_RDWR | O_NOCTTY);
            if (fd < 0) {
                throw std::runtime_error("Failed to open default port: " + std::string(strerror(errno)));
            }
        }
    } else {
        // User-provided port doesn't exist, try default
        std::cerr << "Port '" << port << "' does not exist. Trying default port '" << default_port << "'\n";
        port_name = default_port;
        fd = open(default_port.c_str(), O_RDWR | O_NOCTTY);
        if (fd < 0) {
            throw std::runtime_error("Failed to open default port: " + std::string(strerror(errno)));
        }
    }

    // After successful open, check if virtual
    is_virtual = isPTY(fd);

    // Set baud rate if physical port
    if (!is_virtual) {
        setCustomBaudRate();
    }
}

SerialInterface::~SerialInterface() {
    if(fd >= 0) close(fd);
    if(master_fd >= 0) close(master_fd);
}

// Tries to send data - in case it didn't succeed, throws an error.
void SerialInterface::sendData(const std::string& data){
    if (fd < 0) {
        throw std::runtime_error("Serial port not open");
    }
    ssize_t bytes_written = write(fd, data.c_str(), data.size());
    if (bytes_written < 0) {
        throw std::runtime_error("Failed to write to serial port: " + std::string(strerror(errno)));
    }
    if (static_cast<size_t>(bytes_written) != data.size()) {
        std::cerr << "Warning: Not all bytes were written to serial port\n";
    }
}

// Used for physical ports only
void SerialInterface::setCustomBaudRate() {
    struct termios2 tio;
    if(ioctl(fd, TCGETS2, &tio) < 0) {
        throw std::runtime_error("TCGETS2 failed: " + std::string(strerror(errno)));
    }

    tio.c_cflag &= ~CBAUD;
    tio.c_cflag |= BOTHER;
    tio.c_ispeed = tio.c_ospeed = static_cast<speed_t>(baud_rate);

    if(ioctl(fd, TCSETS2, &tio) < 0) {
        throw std::runtime_error("TCSETS2 failed: " + std::string(strerror(errno)));
    }
}

bool SerialInterface::isPTY(int fd) {
    struct stat st;
    if(fstat(fd, &st) < 0) return false;
    return major(st.st_rdev) == 136; // PTY major number
}

// Getters
int SerialInterface::getFileDescriptor() const { return fd; }
const std::string& SerialInterface::getPortName() const { return port_name; }
bool SerialInterface::isVirtual() const { return is_virtual; }
int SerialInterface::getBaudRate() const { return baud_rate; }

// Setter
void SerialInterface::updBaudRate(const int& baud) {
    baud_rate = baud;
    
    // If we are using a physical port, update the hardware settings
    if (!is_virtual) {
        setCustomBaudRate();
    } else {
        std::cout << "Virtual port: local baud rate updated to " << baud << std::endl;
    }
}
