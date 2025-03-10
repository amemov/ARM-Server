// server_integration_test.cpp

#include <gtest/gtest.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <pty.h>
#include <fcntl.h>
#include <cstring>
#include <stdexcept>
#include <string>

// Structure to hold PTY info.
struct PtyPair {
    int master_fd;
    std::string slave_name;
};

// Create a PTY pair and return the master file descriptor along with the slave name.
PtyPair createPtyPair() {
    PtyPair p;
    int master_fd, slave_fd;
    char slave_name[128] = {0};
    int ret = openpty(&master_fd, &slave_fd, slave_name, nullptr, nullptr);
    if (ret == -1) {
        throw std::runtime_error("openpty failed");
    }
    p.master_fd = master_fd;
    p.slave_name = std::string(slave_name);
    // Close parent's duplicate of the slave fd.
    close(slave_fd);
    return p;
}

TEST(ServerIntegrationTest, GracefulShutdown) {
    // Create a PTY pair so the server can open a valid serial port.
    PtyPair ptyPair = createPtyPair();
    std::string fakePort = ptyPair.slave_name;

    // Prepare a temporary database file name for testing.
    std::string testDbPath = "test_database.db";

    // Fork a child process to run the server.
    pid_t pid = fork();
    ASSERT_NE(pid, -1) << "Fork failed";

    if (pid == 0) {
        // Child process:
        // Set environment variables so the server uses our fake port and test DB.
        setenv("PORT_NAME", fakePort.c_str(), 1);
        setenv("DB_PATH", testDbPath.c_str(), 1);
        // You may set other environment variables (e.g., HOST_NAME, HTTP_PORT) as needed.

        // Provide CLI arguments:
        // Expected order: [Port-Name] [Baud-Rate] [HTTP-Host-Name] [HTTP-Port] [Database-Path]
        // Here we pass: fakePort, "115000", "localhost", "7100", testDbPath.
        execl("./server", "./server", fakePort.c_str(), "115000", "localhost", "7100", testDbPath.c_str(), (char*)NULL);
        // If execl fails, exit with error.
        exit(1);
    } else {
        // Parent process:
        // Allow the server some time to start up.
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // Write a newline to the master side of the PTY to ensure that the blocking read
        // in the server process returns. This lets the while loop check the stop flag.
        ssize_t written = write(ptyPair.master_fd, "\n", 1);
        ASSERT_GT(written, 0) << "Failed to write to PTY master";

        // Send SIGINT to the child process to trigger graceful shutdown.
        kill(pid, SIGINT);

        // Wait for the child process to exit.
        int status;
        waitpid(pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);

        // Close our master side of the PTY.
        close(ptyPair.master_fd);
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
