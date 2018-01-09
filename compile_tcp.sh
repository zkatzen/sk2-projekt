#!/bin/bash
g++ -Wall --std=c++11 -pthread tcp_server.cpp -o tcp_server
if [ $? -ne 0 ]
then
	echo "Got errors, didn't compile."
	exit 1
fi

./tcp_server ${1-12345} ${2-duck.wav} ${1-54321}
