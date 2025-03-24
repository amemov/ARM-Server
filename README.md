# Server for ARM-based Computers (Raspberry Pi)
Full Documentation is available in documentation.md file. 
#### Requirements:
```
C++ 20
g++
CMAKE
Docker 
Google Test 
```

Quickstart - no Docker
------------------------
To install the dependencies, open bash on the computer and type:
```
apt-get update && \
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
```
After run this to create a folder for CMake build and executables:
```
mkdir build && \
    cd build && \
    cmake .. && \
    make && \
    ctest --output-on-failure && \
    mv server ..
```

After creating folder, run these commands to set up Environment variables:
```
export PORT_NAME=${PORT_NAME:-/dev/ttyUSB0}
export BAUDRATE=${BAUDRATE:-115000}
export HOST_NAME=${HOST_NAME:-localhost}
export HTTP_PORT=${HTTP_PORT:-7100}
export DB_PATH=${DB_PATH:-database.db}
```
After defining the Environment variables, run the CMake while being in /build folder:
```
cmake ..
make
```
Befor running ```./server``` or ```./tests```, open a new terminal window and initialize virtual port (socat):
```
socat -d -d PTY,raw,echo=0,link=/dev/ttyUSB0 PTY,raw,echo=0,link=/dev/ttyUSB1
```
Now we can return back to the window, where we ran CMake, and can execute either executable.
Once server is running, open a new window to send curl instructions or echo messages to virtual port. E.g.:
```
curl http://localhost:7100/start 
echo '$0,ok' > /dev/ttyUSB1
```
Quickstart - using Docker / Docker Desktop
------------------------
To build the image:
```
 docker build -t clone-server .
```
To run the container:
```
docker run -t clone-server
```
Now you can open a new bash window and type the code below to open a new bash inside the container. Using it you can send curl and echo messages to the server:
```
docker exec -it [container_id] bash
```
After successfully opening a bash in the container, run socat to initialize the default virtual port:
```
socat -d -d PTY,raw,echo=0,link=/dev/ttyUSB0 PTY,raw,echo=0,link=/dev/ttyUSB1 &
```
Run again a ```docker exec``` command to open a new bash window and type:
```
./server 
```
Now with server and socat running, open a new bash using ```docker exec``` command above to send message over UART or signals to the server, send HTTP requests:
```
curl http://localhost:7100/start
echo '$0,ok' >> /dev/ttyUSB1
```




