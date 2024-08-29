cmake_minimum_required(VERSION 3.20)
project(Finder)
#add_definitions( -DBOOST_ALL_DYN_LINK )
#set(Boost_USE_RELEASE_LIBS ON)
set(Boost_USE_MULTITHREADED ON)  
#set(Boost_USE_STATIC_RUNTIME OFF) 
set(Boost_USE_STATIC_LIBS ON)
set(BOOST_ROOT "C:/boost")
#set(BOOST_LIBRARYDIR "C:/boost/stage")

find_package(Boost REQUIRED COMPONENTS system locale thread)
if(Boost_FOUND)
	 message("${Boost_LIBRARY_DIRS} is found")
endif()
set(OPENSSL_ROOT_DIR "C:/Program Files/OpenSSL-Win64")
set(OPENSSL_USE_STATIC_LIBS TRUE)
set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/VC/x64/MTd/libcrypto.lib")
set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/VC/x64/MTd/libssl.lib")
set(LIBCONV_LIBRARY "D:/Finder/libiconv/libiconvStatic.lib")
set(PostgreSQL_ADDITIONAL_VERSIONS "16")
add_subdirectory("D:/add/libpqxx-7.9.1" libpqxx-build)

add_executable(Spider main.cpp Spider.cpp Table.cpp http_server.cpp)

target_compile_features(Spider PRIVATE cxx_std_20)
target_link_libraries(Spider PRIVATE Boost::system Boost::locale Boost::thread ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} ${LIBCONV_LIBRARY} pqxx)
#target_link_libraries(Spider PRIVATE Boost::system Boost::locale ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} pqxx)


include_directories(${Boost_INCLUDE_DIRS})
include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${BOOST_LIBRARYDIR})
include_directories("D:/Finder/include")
include_directories("D:/Finder/libiconv")

#OpenSSL::SSL OpenSSL::Crypto 
#
#target_link_libraries(Spider PRIVATE Boost::system Boost::locale ${BOOST_LIBRARYDIR} ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} pqxx)