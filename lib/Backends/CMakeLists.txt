add_library(Backends
            Backends.cpp
            )

add_subdirectory(CBackend/)

target_link_libraries(Backends
                      PUBLIC
                      CBackend
                      )

