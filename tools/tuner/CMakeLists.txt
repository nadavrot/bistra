add_executable(tuner
                 tuner.cpp
                 )

target_link_libraries(tuner
                      PUBLIC
                        Analysis
                        Backends
                        Program
                        Transforms
                        Parser
                        Optimizer
                        )

