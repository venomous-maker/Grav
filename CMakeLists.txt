cmake_minimum_required(VERSION 3.20)
project(Practice CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(gravc
        main.cpp
        src/common/types.cpp
        src/ast/ast.cpp
        src/lexer/token.cpp
        src/lexer/lexer.cpp
        src/lexer/preprocess.cpp
        src/parser/parser_core.cpp
        src/parser/parser_stmt.cpp
        src/parser/parser_expr.cpp
        src/sema/symbols.cpp
        src/sema/typechecker.cpp
        src/sema/typechecker_expr.cpp
        src/codegen/mangle.cpp
        src/codegen/codegen_types.cpp
        src/codegen/codegen_func.cpp
        src/codegen/codegen_stmt.cpp
        src/codegen/codegen_expr.cpp
)
target_include_directories(gravc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
