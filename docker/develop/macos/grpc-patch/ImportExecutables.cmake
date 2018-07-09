add_executable(grpc_cpp_plugin IMPORTED)
find_program(grpc_CPP_PLUGIN grpc_cpp_plugin)
message(STATUS "grpc_cpp_plugin found status: ${grpc_CPP_PLUGIN}")
set_target_properties(grpc_cpp_plugin PROPERTIES IMPORTED_LOCATION ${grpc_CPP_PLUGIN})