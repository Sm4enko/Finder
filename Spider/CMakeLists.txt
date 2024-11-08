cmake_minimum_required(VERSION 3.20)
project(Spider)
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_LIBS ON)
set(BOOST_ROOT "C:/finder-main/boost_1_85_0")
add_definitions(${LOGGING_CXX_FLAGS})

find_package(Boost REQUIRED COMPONENTS system locale thread)
if(Boost_FOUND)
	 message("${Boost_LIBRARY_DIRS} is found")
endif()
set(OPENSSL_USE_STATIC_LIBS TRUE)
set(OPENSSL_ROOT_DIR "C:/Program Files/OpenSSL-Win64")
set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/VC/x64/MTd/libcrypto.lib")
set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/VC/x64/MTd/libssl.lib")
set(LIBCONV_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../libs/libiconv/libiconvStatic.lib")
set(PostgreSQL_ADDITIONAL_VERSIONS "16")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../libs/libpqxx-7.9.1" libpqxx-build)

add_executable(Spider main.cpp Spider.cpp Table.cpp easylogging++.cc)

target_compile_features(Spider PRIVATE cxx_std_20)
target_link_libraries(Spider PRIVATE Boost::system Boost::locale Boost::thread ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} ${LIBCONV_LIBRARY} pqxx)

include_directories("${OPENSSL_INCLUDE_DIR}")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs/libiconv")
