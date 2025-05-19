# Thread-Safe Data Management System

A modern C++ implementation of a thread-safe data management system with advanced smart locking mechanisms and queue operations.

## Features
- Thread-safe data access with read/write lock support(RAII data wrapper)
- Smart mutex management with deadlock prevention (Automatic lock acquisition and release)
- Queued operation system
- Automatic thread tracking and lock management
- Reentrant lock support and many more
- Lock conversion capabilities (read to write)
- Mutex tracking per thread
- Auto lock order management

## Build Requirements
- C++17

## Usage (in cmd)
> mkdir build
> cd build
> cmake ..

For detailed example implementation see example.cpp in the `Source/Improved` directory.
