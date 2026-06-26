add_executable(test_tables test_tables.c)
target_link_libraries(test_tables ptcore)
add_test(NAME tables COMMAND test_tables)
