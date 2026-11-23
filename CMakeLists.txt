cmake_minimum_required(VERSION 3.21)
include(cmake/Version.cmake)
project(Kadran VERSION ${KADRAN_VERSION_NUMERIC} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(KADRAN_TESTS "Build the tests" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)

set(PXS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/pxs-qt EXCLUDE_FROM_ALL)

# The model: no widgets, so that it is tested alone
add_library(kadran_core STATIC src/Explorer.cpp src/Explorer.h src/KadranServer.cpp src/KadranServer.h src/ShellPage.h src/OscDevice.cpp src/OscDevice.h)
set_target_properties(kadran_core PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(kadran_core PUBLIC src)
target_link_libraries(kadran_core PUBLIC pxs::oscclient pxs::controls pxs::oscquery pxs::tree Qt6::Network)

# The icon: in the window (a resource), in the macOS bundle (.icns) and in the Windows executable (.ico)
set(KADRAN_ICONS src/kadran.qrc)
if(APPLE)
    list(APPEND KADRAN_ICONS packaging/kadran.icns)
    set_source_files_properties(packaging/kadran.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
elseif(WIN32)
    list(APPEND KADRAN_ICONS packaging/kadran.rc)
endif()
add_executable(Kadran WIN32 MACOSX_BUNDLE src/main.cpp ${KADRAN_ICONS})
target_compile_definitions(Kadran PRIVATE KADRAN_VERSION="${KADRAN_VERSION}")
set_target_properties(Kadran PROPERTIES AUTOMOC ON AUTORCC ON MACOSX_BUNDLE_ICON_FILE "kadran.icns" OUTPUT_NAME "${KADRAN_APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "fr.pixelstereo.kadran" MACOSX_BUNDLE_BUNDLE_NAME "Kadran"
    # Our own Info.plist: macOS only lets an app use the local network (Bonjour, multicast) when it says why
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_VERSION "${KADRAN_VERSION}" MACOSX_BUNDLE_SHORT_VERSION_STRING "${KADRAN_VERSION_NUMERIC}")
# A development build is signed "ad hoc" by the linker: its identity changes with every build, and macOS then withholds the local network
# (Bonjour, multicast) from it without a word. With a certificate named here (cmake -DKADRAN_SIGN_IDENTITY="Apple Development: ...", or
# the same variable in the environment) the app keeps one identity and macOS asks for the permission once.
if(APPLE)
    set(KADRAN_SIGN_IDENTITY "$ENV{KADRAN_SIGN_IDENTITY}" CACHE STRING "Code signing identity for the macOS bundle (empty: ad hoc)")
    if(KADRAN_SIGN_IDENTITY)
        add_custom_command(TARGET Kadran POST_BUILD
            COMMAND codesign --force --sign "${KADRAN_SIGN_IDENTITY}" --identifier "fr.pixelstereo.kadran" "$<TARGET_BUNDLE_DIR:Kadran>"
            COMMENT "Signing Kadran with ${KADRAN_SIGN_IDENTITY}" VERBATIM)
    endif()
endif()
# What the packaging scripts read: the name, the version, the executable
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/application.json" CONTENT
  "{\"name\":\"${KADRAN_APP_NAME}\",\"version\":\"${KADRAN_VERSION}\",\"executable\":\"$<TARGET_FILE_NAME:Kadran>\"}\n")
target_link_libraries(Kadran PRIVATE kadran_core pxs::theme Qt6::Widgets)

if(KADRAN_TESTS)
    enable_testing()
    add_executable(kadran_test test/explorer_test.cpp)
    target_link_libraries(kadran_test PRIVATE kadran_core pxs::oscquery pxs::tree)
    add_test(NAME explorer COMMAND kadran_test)
    add_executable(kadran_server_test test/server_test.cpp)
    target_link_libraries(kadran_server_test PRIVATE kadran_core pxs::oscquery pxs::tree)
    add_test(NAME server COMMAND kadran_server_test)
    add_executable(kadran_devices_test test/devices_test.cpp)
    target_link_libraries(kadran_devices_test PRIVATE kadran_core pxs::oscquery pxs::tree)
    add_test(NAME devices COMMAND kadran_devices_test)
    add_executable(kadran_osc_test test/osc_device_test.cpp)
    target_link_libraries(kadran_osc_test PRIVATE kadran_core)
    add_test(NAME osc_device COMMAND kadran_osc_test)
    # A device to try the application on
    add_executable(kadran_fake_device test/fake_device.cpp)
    target_link_libraries(kadran_fake_device PRIVATE pxs::oscquery pxs::tree Qt6::Core)
endif()
