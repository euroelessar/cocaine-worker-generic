CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
PROJECT(COCAINE-SLAVE-GENERIC)

SET(COCAINE_VERSION 0.10)

SET(CMAKE_MODULE_PATH 
    ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

FIND_PACKAGE(Boost 1.40.0 REQUIRED
    COMPONENTS
        filesystem
        thread
        system)

INCLUDE(cmake/locate_library.cmake)

LOCATE_LIBRARY(LIBCOCAINE "cocaine/context.hpp" "cocaine-core")
LOCATE_LIBRARY(LIBEV "ev++.h" "ev" "libev")
LOCATE_LIBRARY(LIBMSGPACK "msgpack.hpp" "msgpack")
LOCATE_LIBRARY(LIBZMQ "zmq.hpp" "zmq")

INCLUDE_DIRECTORIES(
    ${Boost_INCLUDE_DIRS}
    ${LIBEV_INCLUDE_DIRS}
    ${LIBMSGPACK_INCLUDE_DIRS}
    ${LIBZMQ_INCLUDE_DIRS}
    ${LIBCOCAINE_INCLUDE_DIRS})

INCLUDE_DIRECTORIES(BEFORE
    ${PROJECT_SOURCE_DIR}/include)

LINK_DIRECTORIES(
    ${LIBEV_LIBRARY_DIRS}
    ${Boost_LIBRARY_DIRS}
    ${LIBMSGPACK_LIBRARY_DIRS}
    ${LIBZMQ_LIBRARY_DIRS}
    ${LIBCOCAINE_LIBRARY_DIRS})

ADD_EXECUTABLE(cocaine-worker-generic
    src/worker
    src/main)

TARGET_LINK_LIBRARIES(cocaine-worker-generic
    boost_system-mt
    boost_program_options-mt
    boost_filesystem-mt
    ev
    uuid
    msgpack
    zmq
    cocaine-core)

SET_TARGET_PROPERTIES(cocaine-worker-generic PROPERTIES
    COMPILE_FLAGS "-std=c++0x")

INSTALL(
    TARGETS
        cocaine-worker-generic
    RUNTIME DESTINATION bin COMPONENT runtime)
