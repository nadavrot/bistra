add_library(LLVMBackend
            LLVMBackend.cpp
            Runner.cpp
           )

set_source_files_properties(Runner.cpp PROPERTIES COMPILE_FLAGS -fno-rtti)

target_link_libraries(LLVMBackend
                      PUBLIC
                      ${llvm_libs}
                      )

