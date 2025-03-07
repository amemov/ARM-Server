# Use the official Ubuntu image with explicit ARM64 platform
FROM --platform=linux/arm64 ubuntu:latest

# Install build-essential and g++ for compiling C++ code
RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    && rm -rf /var/lib/apt/lists/*

# Install dependencies: SQLite, and SOCAT
RUN apt-get update && apt-get install -y \
    sqlite3 \
    libsqlite3-dev \
    socat

# Set working directory
WORKDIR /app

# Copy source code
COPY . .

# Build C++ server
RUN g++ -std=c++20 -o server server.cpp -lsqlite3 -Inlohmann

# Run server
CMD ["./server"]