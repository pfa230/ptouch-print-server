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

# The PAPPL app (M2+) builds only where PAPPL + libusb + libpng are present (the container).
# On the Mac (no PAPPL) the pure core + tests still build.
find_package(PkgConfig)
if(PkgConfig_FOUND)
  pkg_check_modules(PAPPL pappl)
  pkg_check_modules(USB libusb-1.0)
  pkg_check_modules(PNG libpng)  # our image/png filter (#40) calls libpng directly
  if(PAPPL_FOUND AND USB_FOUND)
    # Optional live transport for the test-only probe, using the existing scheme.
    target_sources(ptouch-cut-probe PRIVATE src/device_usb.c)
    target_compile_definitions(ptouch-cut-probe PRIVATE PT_PROBE_TRANSPORT=1)
    target_link_libraries(ptouch-cut-probe PRIVATE ${PAPPL_LIBRARIES} ${USB_LIBRARIES})
    target_include_directories(ptouch-cut-probe PRIVATE ${PAPPL_INCLUDE_DIRS} ${USB_INCLUDE_DIRS})
    target_link_directories(ptouch-cut-probe PRIVATE ${PAPPL_LIBRARY_DIRS} ${USB_LIBRARY_DIRS})
    target_compile_options(ptouch-cut-probe PRIVATE ${PAPPL_CFLAGS_OTHER})
  endif()
  if(PAPPL_FOUND AND USB_FOUND AND PNG_FOUND)
    add_executable(ptouch-app src/main.c src/driver.c src/device_usb.c src/filter_png.c)
    target_link_libraries(ptouch-app PRIVATE ptcore ${PAPPL_LIBRARIES} ${USB_LIBRARIES} ${PNG_LIBRARIES})
    target_include_directories(ptouch-app PRIVATE ${PAPPL_INCLUDE_DIRS} ${USB_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})
    target_link_directories(ptouch-app PRIVATE ${PAPPL_LIBRARY_DIRS} ${USB_LIBRARY_DIRS} ${PNG_LIBRARY_DIRS})
    target_compile_options(ptouch-app PRIVATE ${PAPPL_CFLAGS_OTHER})
  endif()
endif()
