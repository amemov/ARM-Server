Requirements:
C++ 20
g++
CMAKE
Docker 
Google Test 
optional: (something for UI) 

To install the dependencies, open bash on the computer and type:
```
*write how to install g++, bazel(probably better CMAKE because ARM), sqllite3, and gtest - via bash scripts*
*write that boost library for C++ is needed to read the ports of device
        and to implement HTTP / HTTP with SSL - aka HTTPS                 *
```

To build the image:
```
 docker build -t mydevcontainer .
```


```
bazel test --cxxopt=-std=c++17 --test_output=all //:server_test
```

To initialize the server instance:
```
bazel test --cxxopt=-std=c++17 --test_output=all //:server_test
```