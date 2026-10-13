cmake_minimum_required(VERSION 3.13)
project(ptouch_print_server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)
option(PTOUCH_CORE_ONLY "Build only the hardware-free core and capture tests" OFF)

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

# The default build must include the app and adapter tests. Opt into the core
# explicitly when building on a machine without the printer dependencies.
if(NOT PTOUCH_CORE_ONLY)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PAPPL REQUIRED pappl)
  pkg_check_modules(USB REQUIRED libusb-1.0)
  pkg_check_modules(PNG REQUIRED libpng)
  # Optional live transport for the test-only probe, using the existing scheme.
  target_sources(ptouch-cut-probe PRIVATE src/device_usb.c)
  target_compile_definitions(ptouch-cut-probe PRIVATE PT_PROBE_TRANSPORT=1)
  target_link_libraries(ptouch-cut-probe PRIVATE ${PAPPL_LIBRARIES} ${USB_LIBRARIES})
  target_include_directories(ptouch-cut-probe PRIVATE ${PAPPL_INCLUDE_DIRS} ${USB_INCLUDE_DIRS})
  target_link_directories(ptouch-cut-probe PRIVATE ${PAPPL_LIBRARY_DIRS} ${USB_LIBRARY_DIRS})
  target_compile_options(ptouch-cut-probe PRIVATE ${PAPPL_CFLAGS_OTHER})
  add_executable(test_driver tests/test_driver.c src/filter_png.c)
  target_link_libraries(test_driver PRIVATE ptcore ${PAPPL_LIBRARIES} ${PNG_LIBRARIES})
  target_include_directories(test_driver PRIVATE ${PAPPL_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})
  target_link_directories(test_driver PRIVATE ${PAPPL_LIBRARY_DIRS} ${PNG_LIBRARY_DIRS})
  target_compile_options(test_driver PRIVATE ${PAPPL_CFLAGS_OTHER})
  add_test(NAME driver_finishings COMMAND test_driver)
  set_tests_properties(driver_finishings PROPERTIES TIMEOUT 90)
  add_executable(test_pappl_raster tests/test_pappl_raster.c)
  target_link_libraries(test_pappl_raster PRIVATE ${PAPPL_LIBRARIES} ${PNG_LIBRARIES})
  target_include_directories(test_pappl_raster PRIVATE ${PAPPL_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})
  target_link_directories(test_pappl_raster PRIVATE ${PAPPL_LIBRARY_DIRS} ${PNG_LIBRARY_DIRS})
  target_compile_options(test_pappl_raster PRIVATE ${PAPPL_CFLAGS_OTHER})
  add_test(NAME pappl_raster COMMAND test_pappl_raster)
  set_tests_properties(pappl_raster PROPERTIES TIMEOUT 90)
  add_executable(test_pappl_finishings tests/test_pappl_finishings.c)
  target_link_libraries(test_pappl_finishings PRIVATE ${PAPPL_LIBRARIES})
  target_include_directories(test_pappl_finishings PRIVATE ${PAPPL_INCLUDE_DIRS})
  target_link_directories(test_pappl_finishings PRIVATE ${PAPPL_LIBRARY_DIRS})
  target_compile_options(test_pappl_finishings PRIVATE ${PAPPL_CFLAGS_OTHER})
  add_test(NAME pappl_finishings COMMAND test_pappl_finishings)
  set_tests_properties(pappl_finishings PROPERTIES TIMEOUT 90)
  add_executable(test_png_geometry tests/test_png_geometry.c src/driver.c src/filter_png.c)
  target_link_libraries(test_png_geometry PRIVATE ptcore ${PAPPL_LIBRARIES} ${PNG_LIBRARIES})
  target_include_directories(test_png_geometry PRIVATE ${PAPPL_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})
  target_link_directories(test_png_geometry PRIVATE ${PAPPL_LIBRARY_DIRS} ${PNG_LIBRARY_DIRS})
  target_compile_options(test_png_geometry PRIVATE ${PAPPL_CFLAGS_OTHER})
  add_test(NAME png_geometry COMMAND test_png_geometry)
  set_tests_properties(png_geometry PROPERTIES TIMEOUT 90)
  add_executable(test_device_usb tests/test_device_usb.c)
  target_link_libraries(test_device_usb PRIVATE ptcore ${PAPPL_LIBRARIES} ${USB_LIBRARIES})
  target_include_directories(test_device_usb PRIVATE ${PAPPL_INCLUDE_DIRS} ${USB_INCLUDE_DIRS})
  target_link_directories(test_device_usb PRIVATE ${PAPPL_LIBRARY_DIRS} ${USB_LIBRARY_DIRS})
  target_compile_options(test_device_usb PRIVATE ${PAPPL_CFLAGS_OTHER})
  add_test(NAME device_usb COMMAND test_device_usb)
  # Test assertions include fixture setup and must stay active in Release too.
  foreach(test_target test_driver test_pappl_raster test_pappl_finishings test_png_geometry test_device_usb)
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
  endforeach()
  add_executable(ptouch-app src/main.c src/driver.c src/device_usb.c src/filter_png.c)
  target_link_libraries(ptouch-app PRIVATE ptcore ${PAPPL_LIBRARIES} ${USB_LIBRARIES} ${PNG_LIBRARIES})
  target_include_directories(ptouch-app PRIVATE ${PAPPL_INCLUDE_DIRS} ${USB_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})
  target_link_directories(ptouch-app PRIVATE ${PAPPL_LIBRARY_DIRS} ${USB_LIBRARY_DIRS} ${PNG_LIBRARY_DIRS})
  target_compile_options(ptouch-app PRIVATE ${PAPPL_CFLAGS_OTHER})
endif()
