include("${ClickHouse_SOURCE_DIR}/cmake/dbms_glob_sources.cmake")

add_headers_and_sources(clickhouse_parsers .)
add_headers_and_sources(clickhouse_parsers ./Access)
add_headers_and_sources(clickhouse_parsers ./MySQL)
add_headers_and_sources(clickhouse_parsers ./Kusto)
add_headers_and_sources(clickhouse_parsers ./PRQL)
add_headers_and_sources(clickhouse_parsers ./Kusto/KustoFunctions)
add_headers_and_sources(clickhouse_parsers ./Prometheus)
add_headers_and_sources(clickhouse_parsers ./graph)

# GQL grammar: build generated ANTLR4 parser as a separate library
if (TARGET ch_contrib::antlr4_cpp_runtime)
    file(GLOB GQL_GENERATED_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/graph/generated/*.cpp")
    file(GLOB GQL_GENERATED_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/graph/generated/*.h")

    add_library(_gql_grammar ${GQL_GENERATED_SOURCES} ${GQL_GENERATED_HEADERS})
    target_include_directories(_gql_grammar SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/graph/generated")
    target_link_libraries(_gql_grammar PUBLIC ch_contrib::antlr4_cpp_runtime)
    target_compile_options(_gql_grammar PRIVATE -w)

    add_library(ch_contrib::gql_grammar ALIAS _gql_grammar)
endif ()

add_library(clickhouse_parsers ${clickhouse_parsers_headers} ${clickhouse_parsers_sources})
target_link_libraries(clickhouse_parsers PUBLIC clickhouse_common_io clickhouse_common_access)

if (TARGET ch_rust::prql)
    target_link_libraries(clickhouse_parsers PRIVATE ch_rust::prql)
endif ()

if (TARGET ch_contrib::antlr4_grammars)
    target_link_libraries(clickhouse_parsers PRIVATE ch_contrib::antlr4_grammars)
endif ()

if (TARGET ch_contrib::gql_grammar)
    target_link_libraries(clickhouse_parsers PRIVATE ch_contrib::gql_grammar)
endif ()

if(ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()

if (ENABLE_FUZZING)
    add_subdirectory(fuzzers)
endif()

if (ENABLE_LEXER_TEST)
    set(LEXER_STANDALONE_BUILD ON CACHE BOOL "Enable standalone lexer build")
endif ()

# Lexer standalone library
if (LEXER_STANDALONE_BUILD)
    add_library(lexer-c Lexer.cpp)
    target_include_directories(lexer-c PRIVATE ..)
    target_compile_options(lexer-c PRIVATE -Os -fno-exceptions -fno-rtti -nostdlib -DLEXER_STANDALONE_BUILD)

    # Check that Lexer compiles to WASM. See the usage in programs/server/play.html
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Lexer.wasm
        COMMAND ${CMAKE_CXX_COMPILER}
        -Os -fno-exceptions -fno-rtti -DLEXER_STANDALONE_BUILD --target=wasm32 -flto -nostdlib
        -I${CMAKE_SOURCE_DIR}/src
        -Wl,--no-entry -Wl,--export-all
        ${CMAKE_CURRENT_SOURCE_DIR}/Lexer.cpp
        -o ${CMAKE_CURRENT_BINARY_DIR}/Lexer.wasm
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Lexer.cpp
        COMMENT "Building Lexer.wasm"
    )

    add_custom_target(lexer_wasm ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/Lexer.wasm
    )
endif ()

