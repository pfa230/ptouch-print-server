cmake_minimum_required(VERSION 3.13)
project(ptouch_print_server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

# Pure, hardware-free core (libc only). The PAPPL + raw-libusb adapter (M2) links this.
add_library(ptcore STATIC
  src/tables.c
  src/protocol.c
  src/raster.c
  src/status.c
)
target_include_directories(ptcore PUBLIC src)

enable_testing()
add_subdirectory(tests)
