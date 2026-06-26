cmake_minimum_required(VERSION 3.13)
project(ptouch_stub C)

set(CMAKE_C_STANDARD 11)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PAPPL REQUIRED pappl)

add_executable(stub stub.c)
target_include_directories(stub PRIVATE ${PAPPL_INCLUDE_DIRS})
target_link_directories(stub PRIVATE ${PAPPL_LIBRARY_DIRS})
target_link_libraries(stub PRIVATE ${PAPPL_LIBRARIES})
target_compile_options(stub PRIVATE ${PAPPL_CFLAGS_OTHER})
