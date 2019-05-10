add_library(Parser
            Lexer.cpp
            Parser.cpp
            ParserContext.cpp
            )

target_link_libraries(Parser
                      PUBLIC
                      )
