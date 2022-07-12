#!/bin/bash

sudo modprobe nbd
sudo nbd-client -N local_test 127.0.0.1 10000 /dev/nbd1 -b 4096 -t 300 -p -C 1
