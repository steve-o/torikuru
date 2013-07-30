# CMake build script for Torikuru
# x64 Windows Server-only
# 2013/04/09 -- Steven.McCoy@thomsonreuters.com

cmake_minimum_required (VERSION 2.8.10)

set(CMAKE_C_COMPILER /home/steve-o/projects/gcc-4.8.1/rtf/bin/gcc)
set(CMAKE_CXX_COMPILER /home/steve-o/projects/gcc-4.8.1/rtf/bin/g++)

project (Torikuru)

# Thomson Reuters Robust Foundation API
set(RFA_ROOT /home/steve-o/rfa7.4.1.L1.linux.rrg)
set(RFA_INCLUDE_DIRS
	${RFA_ROOT}/Include
	${RFA_ROOT}/Include/rwf
)
set(RFA_LIBRARY_DIRS ${RFA_ROOT}/Libs/RHEL5_32_GCC412/Static)
set(RFA_LIBRARY_DIR ${RFA_LIBRARY_DIRS})
set(RFA_LIBRARIES
	RFA
# Real-time clock API
	rt
# Dynamic library API
	dl
)
set(BOOST_ROOT /home/steve-o/projects/gcc-4.8.1/rtf)
set(BOOST_LIBRARYDIR ${BOOST_ROOT}/lib)
set(Boost_USE_STATIC_LIBS ON)
find_package (Boost 1.44 COMPONENTS system thread REQUIRED)
find_package (Threads REQUIRED)
set(PROTOBUF_INCLUDE_DIR /home/steve-o/projects/protobuf/include)
set(PROTOBUF_LIBRARY /home/steve-o/projects/protobuf/lib/libprotobuf.a)
set(PROTOBUF_PROTOC_EXECUTABLE /home/steve-o/projects/protobuf/bin/protoc)
find_package (Protobuf REQUIRED)

message (PROTOBUF_LIBRARY)

#-----------------------------------------------------------------------------
# force off-tree build

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
message(FATAL_ERROR "CMake generation is not allowed within the source directory!
Remove the CMakeCache.txt file and try again from another folder, e.g.:

   del CMakeCache.txt
   mkdir build
   cd build
   cmake ..
")
endif(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})

#-----------------------------------------------------------------------------
# default to Release build

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
      FORCE)
endif(NOT CMAKE_BUILD_TYPE)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH  ${CMAKE_BINARY_DIR}/lib)

#-----------------------------------------------------------------------------
# platform specifics

add_definitions(
	-D_REENTRANT
# RFA on Linux
	-DLinux
# RFA version
        -DRFA_LIBRARY_VERSION="7.4.1."
# production release
#	-DOFFICIAL_BUILD
#	-DENABLE_LEAK_TRACKER
)

# 32-bit
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")

# C++11
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y")

# Static GCC and libstdc++
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")

#-----------------------------------------------------------------------------
# source files

set(gcc_sources
	src/chromium/atomicops_internals_x86_gcc.cc
)
set(posix_sources
	src/chromium/debug/stack_trace_posix.cc
	src/chromium/file_util_posix.cc
	src/chromium/safe_strerror_posix.cc
	src/chromium/synchronization/lock_impl_posix.cc
)
set(win32_sources
	src/chromium/debug/stack_trace_win.cc
	src/chromium/file_util_win.cc
	src/chromium/synchronization/lock_impl_win.cc
)
set(rhel5_sources
	src/compat/pipe2.c
)

PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS src/archive.proto)

set(cxx-sources
	src/torikuru.cc
	src/config.cc
	src/consumer.cc
	src/error.cc
	src/main.cc
	src/rfa.cc
	src/rfa_logging.cc
	src/chromium/chromium_switches.cc
	src/chromium/command_line.cc
	src/chromium/debug/stack_trace.cc
	src/chromium/file_util.cc
	src/chromium/memory/singleton.cc
	src/chromium/metrics/histogram.cc
	src/chromium/logging.cc
	src/chromium/string_piece.cc
	src/chromium/string_split.cc
	src/chromium/string_util.cc
	src/chromium/stringprintf.cc
	src/chromium/synchronization/lock.cc
	src/chromium/vlog.cc
	src/googleurl/url_parse.cc
	${gcc_sources}
	${posix_sources}
	${rhel5_sources}
)

include_directories(
	include
	${RFA_INCLUDE_DIRS}
	${Boost_INCLUDE_DIRS}
	${PROTOBUF_INCLUDE_DIRS}
	${CMAKE_CURRENT_BINARY_DIR}
)

link_directories(
	${RFA_LIBRARY_DIRS}
	${Boost_LIBRARY_DIRS}
)

#-----------------------------------------------------------------------------
# output

add_executable(Torikuru ${cxx-sources} ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(Torikuru
	${PROTOBUF_LIBRARY}
#	protobuf${CMAKE_STATIC_LIBRARY_SUFFIX}
	${RFA_LIBRARIES}
#	${Boost_LIBRARIES}
# explicit name is required to bypass dynamic linking to system copy
	${Boost_LIB_PREFIX}boost_system${CMAKE_STATIC_LIBRARY_SUFFIX}
	${Boost_LIB_PREFIX}boost_thread${CMAKE_STATIC_LIBRARY_SUFFIX}
# manually add threading dependencies
	${CMAKE_THREAD_LIBS_INIT}
)

# end of file
