#!/bin/bash
make
if [ $? -ne 0 ]
then
	echo "Got errors, didn't compile."
	exit 1
fi

./tcp_server ${1-12345} ${1-54321}
