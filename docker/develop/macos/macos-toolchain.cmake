set(CMAKE_SYSTEM_NAME Darwin)
set(TOOLCHAIN_PREFIX x86_64-apple-darwin16)
set(MACPORTS_PREFIX /opt/osxcross/target/macports/pkgs/opt/local)

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-clang)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-clang++)

set(CMAKE_OSX_SYSROOT /opt/osxcross/target/SDK/MacOSX10.12.sdk)
set(CMAKE_STAGING_PREFIX /opt/staging)
set(CMAKE_FIND_ROOT_PATH ${MACPORTS_PREFIX} ${CMAKE_STAGING_PREFIX} ${MACPORTS_PREFIX}/Library/Frameworks/Python.framework/Versions/3.6 ${MACPORTS_PREFIX}/Library/Frameworks/Python.framework/Versions/2.7 /usr/lib/jvm/java-8-openjdk-amd64)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

cmake_policy(SET CMP0025 NEW)
#SET(CMAKE_EXE_LINKER_FLAGS "-Wl,-rpath,/opt/osxcross/target/SDK/MacOSX10.12.sdk/usr/lib")
