cmake_minimum_required(VERSION 3.28)
project(T3NativeControls CXX)

find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)

add_library(t3-native-controls STATIC DpopKeys.cpp T3NativeControls.cpp)
set_target_properties(t3-native-controls PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(t3-native-controls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(t3-native-controls PUBLIC jsi reactnative PRIVATE OpenSSL::Crypto)
