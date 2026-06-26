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
  src/cutter.c
)
target_include_directories(ptcore PUBLIC src)

enable_testing()
add_subdirectory(tests)

# The PAPPL app (M2+) builds only where PAPPL + libusb are present (the container).
# On the Mac (no PAPPL) the pure core + tests still build.
find_package(PkgConfig)
if(PkgConfig_FOUND)
  pkg_check_modules(PAPPL pappl)
  pkg_check_modules(USB libusb-1.0)
  if(PAPPL_FOUND AND USB_FOUND)
    add_executable(ptouch-app src/main.c src/driver.c src/device_usb.c)
    target_link_libraries(ptouch-app PRIVATE ptcore ${PAPPL_LIBRARIES} ${USB_LIBRARIES})
    target_include_directories(ptouch-app PRIVATE ${PAPPL_INCLUDE_DIRS} ${USB_INCLUDE_DIRS})
    target_link_directories(ptouch-app PRIVATE ${PAPPL_LIBRARY_DIRS} ${USB_LIBRARY_DIRS})
    target_compile_options(ptouch-app PRIVATE ${PAPPL_CFLAGS_OTHER})
  endif()
endif()
