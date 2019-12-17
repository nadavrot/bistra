add_executable(benchmark
                 benchmark.cpp
                 )

target_link_libraries(benchmark
                      PUBLIC
                        Analysis
                        Backends
                        Optimizer
                        Parser
                        Program
                        Transforms
                        Bytecode
                        gflags::gflags
                        )

