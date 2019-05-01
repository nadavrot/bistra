add_executable(tuner
                 tuner.cpp
                 )

target_link_libraries(tuner
                      PUBLIC
                        CodeGen
                        Program
                        )

