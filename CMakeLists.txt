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

LOCATE_LIBRARY(LIBCOCAINE "cocaine/context.hpp" "cocaine-common")

INCLUDE_DIRECTORIES(
    ${LIBCOCAINE_INCLUDE_DIRS})

INCLUDE_DIRECTORIES(BEFORE
    ${PROJECT_SOURCE_DIR}/include)

ADD_EXECUTABLE(cocaine-generic-worker
    src/worker
    src/main)

TARGET_LINK_LIBRARIES(cocaine-generic-worker
    boost_program_options-mt
    cocaine-core)

SET_TARGET_PROPERTIES(cocaine-generic-worker PROPERTIES
    COMPILE_FLAGS "-std=c++0x")

INSTALL(
    TARGETS
        cocaine-generic-worker
    RUNTIME DESTINATION bin COMPONENT runtime)
