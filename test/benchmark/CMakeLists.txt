# directory contains benchmarks

# default benchmark path is build/benchmark_bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmark_bin)


add_executable(benchmark_example
    benchmark_example.cpp
    )
target_link_libraries(benchmark_example
    benchmark
    shared_model_stateless_validation
    )

add_executable(proto_creation_benchmark
        proto_creation_benchmark.cpp
        )

target_include_directories(proto_creation_benchmark PUBLIC
        ${PROJECT_SOURCE_DIR}/test)

target_link_libraries(proto_creation_benchmark
        benchmark
        shared_model_proto_backend
        shared_model_proto_builders
        )
