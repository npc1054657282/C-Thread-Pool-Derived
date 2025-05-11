#!/usr/bin/env bash

add-apt-repository ppa:ubuntu-toolchain-r/ppa -y
sudo apt install gcc-10 -y
echo "export CC=gcc-10" >> ~/.bashrc