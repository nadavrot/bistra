add_executable(
    parser_test
    parser_test.cpp
    )

target_link_libraries(
    parser_test
    gtest_main
    Program
    Parser
    )

add_test(
  NAME
    parser_test
  COMMAND
    ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/parser_test
  )


add_executable(
    basic_test
    basic_test.cpp
    )

target_link_libraries(
    basic_test
    gtest_main
    Program
    Backends
    Transforms
    Optimizer
    )

add_test(
  NAME
    basic_test
  COMMAND
    ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/basic_test
  )
