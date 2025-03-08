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
    struct stat buffer;
    if(stat(port_name.c_str(), &buffer) == 0) {
        fd = open(port_name.c_str(), O_RDWR | O_NOCTTY);
        if(fd < 0) {
            throw std::runtime_error("Error opening port: " + std::string(strerror(errno)));
        }
        is_virtual = isPTY(fd);
    } else {
        // Create virtual port
        master_fd = posix_openpt(O_RDWR | O_NOCTTY);
        if(master_fd < 0 || grantpt(master_fd) < 0 || unlockpt(master_fd) < 0) {
            close(master_fd);
            throw std::runtime_error("PTY creation failed: " + std::string(strerror(errno)));
        }
        
        char* slaveName = ptsname(master_fd);
        if(!slaveName) {
            close(master_fd);
            throw std::runtime_error("PTY naming failed: " + std::string(strerror(errno)));
        }
        
        port_name = slaveName;
        fd = open(slaveName, O_RDWR | O_NOCTTY);
        if(fd < 0) {
            close(master_fd);
            throw std::runtime_error("Virtual port open failed: " + std::string(strerror(errno)));
        }
        is_virtual = true;
    }

    if(!is_virtual) {
        setCustomBaudRate();
    }
}

SerialInterface::~SerialInterface() {
    if(fd >= 0) close(fd);
    if(master_fd >= 0) close(master_fd);
}

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