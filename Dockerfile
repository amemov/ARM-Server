# Explicit ARM64 platform, since will be executed on micro-computers
FROM --platform=linux/arm64 ubuntu:latest

# Install build-essential and g++ for compiling C++ code
# Install dependencies
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    cmake \
    g++ \
    sqlite3 \
    libsqlite3-dev \
    socat \
    curl \
    libcurl4-openssl-dev \
    googletest \
    libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

# Build and install Google Test
RUN cd /usr/src/googletest && \
    mkdir build && \
    cd build && \
    cmake .. && \
    make && \
    make install

# Set working directory
WORKDIR /app

# Copy source code
COPY . .

# Build the server
RUN set -e && \
    rm -rf build && \
    mkdir build && \
    cd build && \
    cmake .. && \
    make && \
    ctest --output-on-failure && \
    mv server .. && \
    chmod +x /app/server  # Ensure permissions

# Set default Environment Variables - should be persisten between terminals in the Image
ENV PORT_NAME=/dev/ttyUSB0
ENV BAUDRATE=115000
ENV HOST_NAME=localhost
ENV HTTP_PORT=7100
ENV DB_PATH=database.db

# Create a startup script (kept for manual use - docker doesn't call it)
RUN echo '#!/bin/bash\n\
socat -d -d PTY,raw,echo=0,link=/dev/ttyUSB0 PTY,raw,echo=0,link=/dev/ttyUSB1 &\n\
echo "Socat started in background (PID $!)"\n\
./server\n\
' > /app/startup.sh && \
    chmod +x /app/startup.sh

# Removed automatic server startup
# CMD ["/app/startup.sh"]

# Keep container running without starting server
CMD ["sleep", "infinity"]