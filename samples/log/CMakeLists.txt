cmake_minimum_required (VERSION 3.6)
project (sample)

set(SELF_LIBS_OUT ${CMAKE_SYSTEM_NAME}.out)

#include_directories(.)
#include_directories(WTL)
include_directories(../..)
include_directories(../../mars)
include_directories(../../mars/comm/windows)


include_directories(../../mars/comm)
include_directories(../../mars/openssl/include)

add_subdirectory(../../mars/xlog xlog.out)
#add_subdirectory(../../mars/app app.out)
#add_subdirectory(../../mars/baseevent baseevent.out)
add_subdirectory(../../mars/boost boost.out)
add_subdirectory(../../mars/comm comm.out)
add_subdirectory(../../mars/zstd/build/cmake zstd.out)
#add_subdirectory(../../mars/sdt sdt.out)
#add_subdirectory(../../mars/stn stn.out)

add_definitions(-D_WIN32 -DWIN32 -DUNICODE -D_UNICODE -D__WIN32__)
add_definitions(-D_CRT_NO_VA_START_VALIDATION -D_CRT_SECURE_NO_WARNINGS -D_SILENCE_FPOS_SEEKPOS_DEPRECATION_WARNING)

set(CompilerFlags
	CMAKE_CXX_FLAGS
	CMAKE_CXX_FLAGS_DEBUG
	CMAKE_CXX_FLAGS_RELEASE
	CMAKE_C_FLAGS
	CMAKE_C_FLAGS_DEBUG
	CMAKE_C_FLAGS_RELEASE)
foreach(CompilerFlag ${CompilerFlags})
	string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
endforeach()

set(SELF_SRC_FILES
    main.cpp)
    
add_executable(${PROJECT_NAME} ${SELF_SRC_FILES})

set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "/GL /Zc:wchar_t")
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/LTCG")

  
if(CMAKE_CL_64)
	add_definitions(-D_WIN64 -DWIN64)
endif()

set(CMAKE_FIND_LIBRARY_PREFIXES "")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")

link_directories(${SELF_LIBS_OUT})


#find_library(CRYPT_LIB crypto PATHS ../../mars/openssl/openssl_lib_windows/x86 NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
#find_library(SSL_LIB ssl PATHS ../../mars/openssl/openssl_lib_windows/x86 NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

get_filename_component(CRYPT_LIB ../../mars/openssl/openssl_lib_windows/x64/libcrypto.lib ABSOLUTE)
get_filename_component(SSL_LIB ../../mars/openssl/openssl_lib_windows/x64/libssl.lib ABSOLUTE)

target_link_libraries(${PROJECT_NAME} ws2_32.lib comsuppw.lib
						xlog
 #                       app
 #                       baseevent
                        mars-boost
                        comm
						libzstd_static
 #                       sdt
 #                       stn
                        ${CRYPT_LIB}
                        ${SSL_LIB})
  