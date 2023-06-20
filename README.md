# Virtual File System

Simple vfs, Tested on Windows (but shoud be POSIX compatible), C++20 required

## Usage

Build

```bash
g++ -std=c++20 vfs.cpp
```

Use same paths for same files. If you created file with absolute path, use same path to open it.

Note: Reusing functions Create() and Open() on already opened file, will retrun you already existing File*, not new.
