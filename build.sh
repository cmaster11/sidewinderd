#!/bin/bash

mkdir build 
cd build && cmake .. && make && sudo make install && sudo cp ../etc/sidewinderd.cmaster11.conf /etc/sidewinderd.conf

