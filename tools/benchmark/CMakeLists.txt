add_executable(benchmark
                 benchmark.cpp
                 )

target_link_libraries(benchmark
                      PUBLIC
                        Analysis
                        Backends
                        Program
                        Transforms
                        Parser
                        Optimizer
                        )

