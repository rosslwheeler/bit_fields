cmake_minimum_required(VERSION 3.20)

add_executable(test_bitstream test_bitstream.cpp)
target_link_libraries(test_bitstream PRIVATE bit_fields)

add_test(NAME bitstream_span_of_field COMMAND test_bitstream)
