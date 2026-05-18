find_package(OpenSSL REQUIRED)
if(MADT_USE_WEBENGINEVIEW)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
    find_package(Qt6 REQUIRED COMPONENTS WebEngineWidgets)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets Network)
    find_package(Qt5 REQUIRED COMPONENTS WebKitWidgets)
endif()

find_program(MADT_HOST_MOC moc REQUIRED)
find_program(MADT_HOST_RCC rcc REQUIRED)
find_library(UUID_LIBRARY uuid REQUIRED)

add_executable(madt-server
    AvahiService.cpp
    main.cpp
    gui.cpp
    Browser.cpp
    IconLoader.cpp
    madt-server.cpp
    runtime-config.cpp
    resources/resources.qrc
)
target_compile_definitions(madt-server PRIVATE MODULE_NAME="madt")
target_compile_definitions(madt-server PRIVATE MADT_SOFTWARE_VERSION="${MADT_SOFTWARE_VERSION}")
if(MADT_USE_WEBENGINEVIEW)
    target_compile_definitions(madt-server PRIVATE USE_WEBENGINEVIEW=1)
endif()
target_include_directories(madt-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(madt-server PRIVATE
    wrapper_log
    wrapper_process
    wrapper_wdog
    nlohmann_json::nlohmann_json
    OpenSSL::Crypto
    #Qt6::Network
    #Qt6::Widgets
    ${UUID_LIBRARY}
)
if(MADT_USE_AVAHI)
    find_package( avahi-client REQUIRED)

    target_compile_definitions(madt-server PRIVATE MADT_USE_AVAHI=1)
    target_link_libraries(madt-server PRIVATE avahi-client::avahi)
endif()
if(MADT_USE_WEBENGINEVIEW)
    target_link_libraries(madt-server PRIVATE Qt6::WebEngineWidgets)
else()
    target_link_libraries(madt-server PRIVATE Qt5::WebKitWidgets)
endif()
set_target_properties(madt-server PROPERTIES AUTOMOC_EXECUTABLE "${MADT_HOST_MOC}")
set_target_properties(madt-server PROPERTIES AUTORCC_EXECUTABLE "${MADT_HOST_RCC}")
set_target_properties(madt-server PROPERTIES AUTORCC ON)

install(TARGETS madt-server DESTINATION bin)
