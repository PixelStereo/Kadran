cmake_minimum_required(VERSION 3.21)
project(Kadran VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(KADRAN_TESTS "Build the tests" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)

set(PXS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/pxs EXCLUDE_FROM_ALL)

# The model: no widgets, so that it is tested alone
add_library(kadran_core STATIC src/Explorer.cpp src/Explorer.h src/KadranServer.cpp src/KadranServer.h src/ShellPage.h)
set_target_properties(kadran_core PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(kadran_core PUBLIC src)
target_link_libraries(kadran_core PUBLIC pxs::oscclient pxs::controls Qt6::Network)

add_executable(Kadran MACOSX_BUNDLE src/main.cpp)
set_target_properties(Kadran PROPERTIES AUTOMOC ON MACOSX_BUNDLE_GUI_IDENTIFIER "fr.pixelstereo.kadran"
    MACOSX_BUNDLE_BUNDLE_NAME "Kadran")
target_link_libraries(Kadran PRIVATE kadran_core pxs::theme Qt6::Widgets)

if(KADRAN_TESTS)
    enable_testing()
    add_executable(kadran_test test/explorer_test.cpp)
    target_link_libraries(kadran_test PRIVATE kadran_core pxs::oscquery pxs::tree)
    add_test(NAME explorer COMMAND kadran_test)
    # A device to try the application on
    add_executable(kadran_fake_device test/fake_device.cpp)
    target_link_libraries(kadran_fake_device PRIVATE pxs::oscquery pxs::tree Qt6::Core)
endif()
