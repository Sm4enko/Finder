cmake_minimum_required(VERSION 3.20)
project(Finder)
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_LIBS ON)
set(BOOST_ROOT "C:/finder-main/boost_1_85_0")


find_package(Boost REQUIRED COMPONENTS system locale thread)
if(Boost_FOUND)
	 message("${Boost_LIBRARY_DIRS} is found")
endif()
set(LIBCONV_LIBRARY "C:/finder-main/libs/libiconv/libiconvStatic.lib")
set(PostgreSQL_ADDITIONAL_VERSIONS "16")
add_subdirectory("../${CMAKE_INCLUDE_CURRENT_DIR}/libs/libpqxx-7.9.1" libpqxx-build)

add_executable(Finder main.cpp http_server.cpp easylogging++.cc)

target_compile_features(Finder PRIVATE cxx_std_20)
target_link_libraries(Finder PRIVATE Boost::system Boost::locale Boost::thread ${LIBCONV_LIBRARY} pqxx)

#include_directories("../${CMAKE_INCLUDE_CURRENT_DIR}/include")
include_directories("../${CMAKE_INCLUDE_CURRENT_DIR}/libs/libiconv")