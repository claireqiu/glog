# GLog

This project is a fork of <a href="https://github.com/karmaresearch/vlog">VLog</a>.

[![Build Status](https://travis-ci.org/karmaresearch/vlog.svg?branch=master)](https://travis-ci.org/karmaresearch/vlog)

## Installation 

We used CMake to ease the installation process. To build GLog, the following
commands should suffice:

```
mkdir build
cd build
cmake ..
make
```

External libraries should be automatically downloaded and installed in the same directory. The only library that should be already installed is zlib, which is necessary to read gzip files. This library is usually already present by default.

To enable the web-interface, you need to use the -DWEBINTERFACE=1 option to cmake.

If you want to build the DEBUG version of the program, including the web interface: proceed as follows:

```
mkdir build_debug
cd build_debug
cmake -DWEBINTERFACE=1 -DCMAKE_BUILD_TYPE=Debug ..
make
```
