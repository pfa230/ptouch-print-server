# CMake generated Testfile for 
# Source directory: /home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests
# Build directory: /home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(tables "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/tests/test_tables")
set_tests_properties(tables PROPERTIES  _BACKTRACE_TRIPLES "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/CMakeLists.txt;6;add_test;/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/CMakeLists.txt;0;")
add_test(protocol "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/tests/test_protocol")
set_tests_properties(protocol PROPERTIES  _BACKTRACE_TRIPLES "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/CMakeLists.txt;10;add_test;/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/CMakeLists.txt;0;")
add_test(raster "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/tests/test_raster")
set_tests_properties(raster PROPERTIES  _BACKTRACE_TRIPLES "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/CMakeLists.txt;14;add_test;/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/CMakeLists.txt;0;")
add_test(status "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/tests/test_status")
set_tests_properties(status PROPERTIES  _BACKTRACE_TRIPLES "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/CMakeLists.txt;18;add_test;/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/CMakeLists.txt;0;")
add_test(cutter "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/tests/test_cutter")
set_tests_properties(cutter PROPERTIES  _BACKTRACE_TRIPLES "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/CMakeLists.txt;22;add_test;/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/CMakeLists.txt;0;")
add_test(cut_probe_capture "/usr/bin/python3.14" "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/test_cut_probe.py" "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/build-adapter/tests/ptouch-cut-probe")
set_tests_properties(cut_probe_capture PROPERTIES  _BACKTRACE_TRIPLES "/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/CMakeLists.txt;28;add_test;/home/pfa/projects/ptouch-print-server/.worktrees/match-printed-length/tests/CMakeLists.txt;0;")
