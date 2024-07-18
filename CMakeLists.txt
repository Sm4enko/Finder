cmake_minimum_required(VERSION 3.20)
project(Finder)

set(BOOST_ROOT "C:/Boost")
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS system locale)

set(OPENSSL_ROOT_DIR "C:/Program Files/OpenSSL-Win64")
set(OPENSSL_USE_STATIC_LIBS TRUE)
set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/VC/x64/MTd/libcrypto.lib")
set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/VC/x64/MTd/libssl.lib")

set(PostgreSQL_ADDITIONAL_VERSIONS "16")
add_subdirectory("D:/Finder/libpqxx-7.9.1" libpqxx-build)

add_executable(Spider main.cpp Spider.cpp Table.cpp)
target_compile_features(Spider PRIVATE cxx_std_20)

target_link_libraries(Spider PRIVATE Boost::system Boost::locale ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} pqxx)

include_directories(${Boost_INCLUDE_DIRS})
include_directories(${OPENSSL_INCLUDE_DIR})