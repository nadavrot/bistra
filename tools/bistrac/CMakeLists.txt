add_executable(bistrac
                 bistrac.cpp
                 )

target_link_libraries(bistrac
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



