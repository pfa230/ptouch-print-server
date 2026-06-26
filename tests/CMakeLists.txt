add_executable(test_tables test_tables.c)
target_link_libraries(test_tables ptcore)
add_test(NAME tables COMMAND test_tables)

add_executable(test_protocol test_protocol.c)
target_link_libraries(test_protocol ptcore)
add_test(NAME protocol COMMAND test_protocol)

add_executable(test_raster test_raster.c)
target_link_libraries(test_raster ptcore)
add_test(NAME raster COMMAND test_raster)
