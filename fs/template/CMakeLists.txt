cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(PROJECT_NAME VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64 -no-pie")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall --pedantic -g")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" ${CMAKE_MODULE_PATH})
# set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
# set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

find_package(FUSE REQUIRED)
include_directories(${FUSE_INCLUDE_DIR} ./include ./bonus/include)
aux_source_directory(./src DIR_SRCS)
add_executable(PROJECT_NAME ${DIR_SRCS}
    bonus/src/bonus.c
    bonus/src/path.c
    bonus/src/checkpoint_support.c
    bonus/src/vector_support.c)
target_compile_features(PROJECT_NAME PRIVATE c_std_11)
message("FUSE_INCLUDE_DIR ${FUSE_INCLUDE_DIR}")
message("FUSE_LIBRARIES ${FUSE_LIBRARIES}")
message("DIR_SRCS ${DIR_SRCS}")
message("!!!!!**CMAKE_GENERATOR** ${CMAKE_GENERATOR}")
target_link_libraries(PROJECT_NAME ${FUSE_LIBRARIES} $ENV{HOME}/lib/libddriver.a m)
