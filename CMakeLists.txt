cmake_minimum_required(VERSION 3.20)
project(Practice CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(gravc
        main.cpp
        src/driver/modules.cpp
        src/common/types.cpp
        src/ast/ast.cpp
        src/lexer/token.cpp
        src/lexer/lexer.cpp
        src/lexer/preprocess.cpp
        src/parser/parser_core.cpp
        src/parser/parser_stmt.cpp
        src/parser/parser_expr.cpp
        src/parser/parser_enum.cpp
        src/sema/monomorph.cpp
        src/sema/symbols.cpp
        src/sema/typechecker.cpp
        src/sema/typechecker_expr.cpp
        src/sema/typechecker_ops.cpp
        src/sema/typechecker_lambda.cpp
        src/sema/typechecker_infer.cpp
        src/sema/sanitize.cpp
        src/sema/enums.cpp
        src/sema/typechecker_match.cpp
        src/codegen/mangle.cpp
        src/codegen/codegen_runtime.cpp
        src/codegen/codegen_types.cpp
        src/codegen/codegen_flow.cpp
        src/codegen/codegen_dispatch.cpp
        src/codegen/codegen_lambda.cpp
        src/codegen/codegen_owned.cpp
        src/codegen/codegen_func.cpp
        src/codegen/codegen_stmt.cpp
        src/codegen/codegen_expr.cpp
)
target_include_directories(gravc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# `import "std"` falls back to this tree's lib/ when no installed copy is found.
target_compile_definitions(gravc PRIVATE GRAV_SOURCE_STD_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lib")

# `cmake --install build --prefix ~/.local` installs bin/grav and share/grav/lib.
install(PROGRAMS $<TARGET_FILE:gravc> DESTINATION bin RENAME grav)
install(DIRECTORY lib/ DESTINATION share/grav/lib FILES_MATCHING PATTERN "*.grav" PATTERN "*.md")
