add_executable(
    basic_test
    basic.cpp
    )

target_link_libraries(
    basic_test
    gtest_main
    Program
    )

add_test(
  NAME
    basic_test
  COMMAND
    ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/basic_test
  )