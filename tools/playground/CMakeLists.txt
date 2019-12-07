add_executable(playground
                 playground.cpp
                 )

target_link_libraries(playground
                      PUBLIC
                        Analysis
                        Backends
                        Program
                        Transforms
                        Parser
                        Optimizer
                        )

