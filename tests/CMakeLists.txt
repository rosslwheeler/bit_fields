cmake_minimum_required(VERSION 3.20)

add_executable(test_bitstream test_bitstream.cpp)
target_link_libraries(test_bitstream PRIVATE bit_fields)

add_test(NAME bitstream_span_of_field COMMAND test_bitstream)

add_executable(bench_bitstream bench_bitstream.cpp)
target_link_libraries(bench_bitstream PRIVATE bit_fields)
