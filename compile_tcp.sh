#!/bin/bash
g++ -Wall --std=c++11 -pthread tcp_server.cpp -o tcp_server
./tcp_server ${1-12345} ${2-duck.wav}
