add_executable(benchmark
                 benchmark.cpp
                 )

target_link_libraries(benchmark
                      PUBLIC
                        Backends
                        Optimizer
                        Parser
                        Program
                        Transforms
                        Bytecode
                        Analysis
                        gflags::gflags
                        )

