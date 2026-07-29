cmake_minimum_required(VERSION 3.20)

project(edge_iqa VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc videoio highgui)

add_library(edge_iqa_core
    src/metrics.cpp
    src/image_io.cpp
    src/batch_runner.cpp
    src/csv_writer.cpp
    src/distortions.cpp
)

target_include_directories(edge_iqa_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(edge_iqa_core
    PUBLIC
        ${OpenCV_LIBS}
)

target_compile_options(edge_iqa_core
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(iqa_eval
    apps/iqa_eval/main.cpp
)

target_link_libraries(iqa_eval
    PRIVATE
        edge_iqa_core
)

target_compile_options(iqa_eval
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(iqa_generate
    apps/iqa_generate/main.cpp
)

target_link_libraries(iqa_generate
    PRIVATE
        edge_iqa_core
)

target_compile_options(iqa_generate
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)
