add_executable(bistrac
                 bistrac.cpp
                 )

target_link_libraries(bistrac
                      PUBLIC
                        Backends
                        Program
                        Transforms
                        Parser
                        Optimizer
                        gflags::gflags
                     )



