# Lua
CMake based build of Lua 5.4.6
| Build as C | Build as C++ |
| --: | --: |
| ![Build Linux](https://github.com/walterschell/Lua/actions/workflows/build-linux.yml/badge.svg?branch=master) | ![Build Linux as C++](https://github.com/walterschell/Lua/actions/workflows/build-linux-cxx.yml/badge.svg?branch=master) |
| ![Build Windows](https://github.com/walterschell/Lua/actions/workflows/build-windows.yml/badge.svg?branch=master) | ![Build Windows as C++](https://github.com/walterschell/Lua/actions/workflows/build-windows-cxx.yml/badge.svg?branch=master) |
| ![Build OSX](https://github.com/walterschell/Lua/actions/workflows/build-osx.yml/badge.svg?branch=master) | ![Build OSX as C++](https://github.com/walterschell/Lua/actions/workflows/build-osx-cxx.yml/badge.svg?branch=master) |
| ![Build Emscripten](https://github.com/walterschell/Lua/actions/workflows/build-emscripten.yml/badge.svg?branch=master) | ![Build Emscripten as C++](https://github.com/walterschell/Lua/actions/workflows/build-emscripten-cxx.yml/badge.svg?branch=master) |
# Usage
Inside of your project's CMakeLists.txt
```cmake
add_subdirectory(lua)
...
target_link_libraries(<YOURTARGET> lua_static)
```
