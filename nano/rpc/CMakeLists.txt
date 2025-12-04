add_library(
  rpc
  rpc.hpp
  rpc.cpp
  rpc_connection.hpp
  rpc_connection.cpp
  rpc_handler.hpp
  rpc_handler.cpp
  rpc_request_processor.hpp
  rpc_request_processor.cpp
  rpc_version_handler.hpp
  v3/response_builder.hpp
  v3/response_builder.cpp
  v3/error_codes.hpp
  v3/rpc_v3_handler.hpp
  v3/rpc_v3_handler.cpp)

target_link_libraries(rpc nano_lib Boost::beast Boost::json)
