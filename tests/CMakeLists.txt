add_executable(test_tables test_tables.c)
target_link_libraries(test_tables ptcore)
add_test(NAME tables COMMAND test_tables)

add_executable(test_protocol test_protocol.c)
target_link_libraries(test_protocol ptcore)
add_test(NAME protocol COMMAND test_protocol)

add_executable(test_raster test_raster.c)
target_link_libraries(test_raster ptcore)
add_test(NAME raster COMMAND test_raster)

add_executable(test_status test_status.c)
target_link_libraries(test_status ptcore)
add_test(NAME status COMMAND test_status)

add_executable(test_cutter test_cutter.c)
target_link_libraries(test_cutter ptcore)
add_test(NAME cutter COMMAND test_cutter)
