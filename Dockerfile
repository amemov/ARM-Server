# Explicit ARM64 platform, since will be executed on micro-computers
FROM --platform=linux/arm64 ubuntu:latest

# Install build-essential and g++ for compiling C++ code
# Install dependencies
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    g++ \
    sqlite3 \
    libsqlite3-dev \
    socat && \
    rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source code
COPY . .

# Build C++ server
RUN g++ -std=c++20 -o server server.cpp -lsqlite3 -I.

# Set default Environment Variables - should be persisten between terminals in the Image
ENV PORT_NAME=/dev/ttyUSB0
ENV BAUDRATE=115000
ENV HOST_NAME=localhost
ENV HTTP_PORT=7100
ENV DB_PATH=database.db

# Create a startup script
RUN echo '#!/bin/bash\n\
socat -d -d PTY,raw,echo=0,link=/dev/ttyUSB0 PTY,raw,echo=0,link=/dev/ttyUSB1 &\n\
echo "Socat started in background (PID $!)"\n\
./server\n\
' > /app/startup.sh && \
    chmod +x /app/startup.sh

# Run the server through the startup script
CMD ["/app/startup.sh"]