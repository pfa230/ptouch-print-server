# CMake generated Testfile for 
# Source directory: /home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length
# Build directory: /home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(driver_finishings "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/test_driver")
set_tests_properties(driver_finishings PROPERTIES  TIMEOUT "90" _BACKTRACE_TRIPLES "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/CMakeLists.txt;41;add_test;/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/CMakeLists.txt;0;")
add_test(driver_finishings_p710bt "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/test_driver" "PT-P710BT")
set_tests_properties(driver_finishings_p710bt PROPERTIES  TIMEOUT "90" _BACKTRACE_TRIPLES "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/CMakeLists.txt;42;add_test;/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/CMakeLists.txt;0;")
add_test(pappl_raster "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/test_pappl_raster")
set_tests_properties(pappl_raster PROPERTIES  TIMEOUT "90" _BACKTRACE_TRIPLES "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/CMakeLists.txt;49;add_test;/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/CMakeLists.txt;0;")
add_test(pappl_finishings "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/test_pappl_finishings")
set_tests_properties(pappl_finishings PROPERTIES  TIMEOUT "90" _BACKTRACE_TRIPLES "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/CMakeLists.txt;56;add_test;/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/CMakeLists.txt;0;")
add_test(png_geometry "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/test_png_geometry")
set_tests_properties(png_geometry PROPERTIES  TIMEOUT "90" _BACKTRACE_TRIPLES "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/CMakeLists.txt;63;add_test;/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/CMakeLists.txt;0;")
add_test(device_usb "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/test_device_usb")
set_tests_properties(device_usb PROPERTIES  _BACKTRACE_TRIPLES "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/CMakeLists.txt;70;add_test;/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/CMakeLists.txt;0;")
subdirs("tests")
