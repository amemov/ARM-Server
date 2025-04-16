C++ HTTP Server 

- Specs of the Project's Image 
    Target Architecture: ARM64, since the code will be deployed on micro-computers like Raspberry-Pi
    This can be changed in 'Dockerfile' and should work on other architectures ( would need to test that to confirm )
    OS: Ubuntu 24.04.1 LTS
    Docker v4.38.0
    Required Packages:
        CMake 3.28.3
        Google Test 1.14.0-1
        g++ 13.3.0
        cpp-httplib
        Nlohmann JSON 3.11.3
        SQLite 3.45.1
        socat version 1.8.0.0
    
    To build the Docker image:
        docker build -t clone-server .

    To run the Docker image:
        docker run -t clone-server
    
    Refer to quickstart for steps on what to do next to start the server, run the commands, etc. Covers both using Docker and without Docker (assuming that computer is on ARM chip, and running on Ubuntu / Debian Linux)

    Testing: 
        All test file(-s) located in 'build/' folder, which is in the same directory as all code files. Test files are intended to be ran when server is not running in the back. To run tests, 'cd build/' then './tests'
        NOTE: Make sure default virtual port is running in the background - /dev/ttyUSB0 


    Ways to change overwrite some default parameters on server: 
        1. Modifying and recompiling the code
        2. Environment variables
        3. PUT /configure
    
    CLI Arguments:
        To run the server with overriden parameters in bash, type the following, where each value separated by 1 space
        ./server [PORT_NAME] [BAUDRATE] [HOST_NAME] [HTTP_PORT] [DB_PATH]
    
    Environment Variables:  PORT_NAME - port name, expressed as string. Default = '/dev/ttyUSB0'
                            BAUDRATE - baud rate of the port, expressed as positive integer. Default = 115000
                            HOST_NAME - name of HTTP host, expressed as string. Default = 'localhost'
                            HTTP_PORT - port of the server, expressed as positive integer > 1023. Default = 7100
                            DB_PATH - path to 'database.db', expressed as string. Default = 'database.db'

                            Make sure they are exported in current terminal session before you run the server executable. You can do this running the following commands:
                                export PORT_NAME=${PORT_NAME:-/dev/ttyUSB0}
                                export BAUDRATE=${BAUDRATE:-115000}
                                export HOST_NAME=${HOST_NAME:-localhost}
                                export HTTP_PORT=${HTTP_PORT:-7100}
                                export DB_PATH=${DB_PATH:-database.db}

Serial Interface - UART Parameters:
- Default parameters: Port = "/dev/ttyUSB0", Baudrate = 115000. 
The provided parameters, can be overwritten via CLI arguments, or using environment variables. Has to be valid.
Otherwise, default parameter is going to be used. 
In case both CLI and Environment variables are defined, the CLI arguments are prioritzed.
                         
- To use virtual ports:
    # Terminal 1: Create virtual ports
    socat -d -d PTY,raw,echo=0,link=/dev/ttyUSB0 PTY,raw,echo=0,link=/dev/ttyUSB1

    # Terminal 2: Echo messages 'from device' to the server (if using virtual interface)
    echo '$1,ok' > /dev/ttyUSB1

    # Terminal 3 (optional): Listen to the port that is connected to the device
    cat /dev/ttyUSB1

Server specifications: 
- Default parameters: host = "localhost", port = 7100
The provided parameters, can be overwritten via CLI arugments, or using environment variables. Has to be valid.
Otherwise, default parameter is going to be used. 
In case both CLI and Environment variables are defined, the CLI arguments are prioritized.
- Environment Variables: HOST_NAME - name of the host, expressed as string
                         PORT_NUMBER - number of the port, integer, has to be available


- Libraries: cpp-httplib (https://github.com/yhirose/cpp-httplib) (httplib.h)
             Nlohmann JSON (https://github.com/nlohmann/json) (nlohmann folder)
- Statuses: Technically, should be more than 3 to denote inside where exactly the error or success happened, but for sake of simplicity I choose to use just 3 statuses that describe well what happened (server also returns messages with errors)
            500 - error within server was detected, prints out what exactly triggered it.
            400 - incorrect input was given through request
            200 - success. Prints out that the confirmation message that it was executed.
- Commands:
        GET /start - sends '$0' command to device over UART. Starts stream of messages once receives the '$0,ok', returns error otherwise (either '$0,invalid command' or '$0,blahblah' - both result in "GET /start: Device error - *ERROR MESSAGE*"). If success, status  200 and a confirmation - "GET /start: Reading started", and starts listening to the messages being sent and stores only valid ones. Timeout error occurs if the server gets no response in 10 seconds from the device.Also, throws error if user requests /start when server is already reading messages
        GET /stop -  sends '$1' command to device over UART. Stops stream of messages once receives the '$1,ok', returns error otherwise (either '$1,invalid command' or '$1,blahblah' - both result in "GET /stop: Device error - *ERROR MESSAGE*"). If success, status 200 and a confirmation - "GET /stop: Reading stopped", and stops listening to the messages. Timeout error occurs if the server gets no response in 10 seconds from the device. Also, throws error if user requests /stop when server is not reading messages
        GET /messages?limit=[limit] - returns limit last messages received from the device, returns error or 200
                      Example:
                      {
                        "pressure": 123.4,
                        "temperature": 567.8,
                        "velocity": 999.9
                      }
        GET /device - returns the meta data of device as described in the task doc, except without first debug
                      ( I guess it was a typo, so that's why I just left debug in curr_config JSON). For mean_last_10:
                      returns 0 if and only if there are less than 10 entries in the table for given port, freq, and debug flag. If no entires in the table with given port, frequency, debug, puts null in these JSONs (except current config). Returns error when no limit is passed, limit negative, no message associated with given port name, frequency, debug flag. 200 is sent when successfully returns the messages. 
                      Example: 
                      {
                        "curr_config": {
                            "frequency": 10,
                            "debug": true
                        },
                        "mean_last_10": {
                            "pressure": 123.4,
                            "temperature": 567.8,
                            "velocity": 999.9
                        },
                        "latest": {
                            "pressure": 123.4,
                            "temperature": 567.8,
                            "velocity": 999.9
                        }
                      }
        PUT /configure - changes frequency (KHz and baud rate), and debug flag. This is a strange requirement, 
                         especially in context of virtual ports. This option if used with non virtual ports, should
                         be used with caution, since if both devices don't have the same frequency for both reading and 
                         writing data, the data can be corrupted on server's end. I altered the /configure and instead of expressing the frequency in Hz it is expressed as KHz, because uint8_t can fit values in range [0:255], which makes sense if and only if frequency is 
                         in KHz. 
                         Sends command to a device e.g. '$2,100,1', and waits for response. Error if timeout, incorrect command, incorrect response received from device (e.g. "$2 hm,oke"). Returns 200 if success, and updates SerialInterface and DatabaseManager values to makes sure the device reads correctly, and the messages are stored with right parameters. 
                         
        Curl Commands to interact with server: 
                    curl http://localhost:7100/start

                    curl http://localhost:7100/stop

                    curl http://localhost:7100/messages?limit=1

                    curl http://localhost:7100/device

                    curl -X PUT http://localhost:7100/configure \
                        -H "Content-Type: application/json" \
                        -d '{"frequency": 1000, "debug": true}'
        Example to communicate via bash (assuming default parameters):
                    echo '$0,ok' >> /dev/ttyUSB1   


SQLite Storage:
DB File (default): database.db
Table: 
Port[TEXT] , Frequency[INTEGER], Debug[INTEGER], Pressure[BLOB], Temperature[BLOB], Velocity[BLOB], Timestamp[INTEGER]

Table name: SensorData 
- Default parameters: db_path = "database.db"
The provided parameters, can be overwritten via CLI arugments, or using environment variables. Has to be valid.
Otherwise, default parameter is going to be used. 
In case both CLI and Environment variables are defined, the CLI arguments are prioritzed.
- Environment Variables: DATABASE_PATH - name of the path to .db file, expressed as string. The .db files has to
exist in the given directory, otherwise the Programm will revert to default settings. If database.db won't be found anywhere, the server will create a new .db file in the server.cpp folder 

- Table parameters:
1. Port - expressed as TEXT, e.g. "dev/ttyUSB0". Could probably make it more efficient storing as integers, and having
a separate table to lookup which string corresponds to which integer - that way could save thousands of bytes on storage. For now I have it as TEXT, to make sure I have enough time to complete remaining requirements
2. Frequency - Stored as KHz, e.g. 115
3. Debug - is a flag for LED that is in range [0:1], where 0 = false, and 1 = true
4,5,6. Pressure, Temperature, Velocity - float16 that are expressed as BLOBs to ensure efficient storage
7. Timestamp - expressed as UNIX timestamp 

- Schema: 
    "CREATE TABLE IF NOT EXISTS SensorData ("
                                            "Port TEXT NOT NULL, "
                                            "Frequency INTEGER NOT NULL, "
                                            "Debug INTEGER NOT NULL CHECK (Debug IN (0, 1)), "
                                            "Pressure BLOB, "
                                            "Temperature BLOB, "
                                            "Velocity BLOB, "
                                            "Timestamp INTEGER NOT NULL"
                                            ");"

