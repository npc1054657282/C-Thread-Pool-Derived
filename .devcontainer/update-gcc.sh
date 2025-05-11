#!/usr/bin/env bash

sudo add-apt-repository ppa:ubuntu-toolchain-r/ppa -y
sudo apt install gcc-10 g++-10 -y
echo -e "export CC=gcc-10\nexport CXX=g++-10" >> ~/.bashrc