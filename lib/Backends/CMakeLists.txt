add_library(Backends
            Backends.cpp
            )

add_subdirectory(LLVMBackend/)

target_link_libraries(Backends
                      PUBLIC
                      LLVMBackend
                      )

