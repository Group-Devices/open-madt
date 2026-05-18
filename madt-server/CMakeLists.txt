find_package(OpenSSL REQUIRED)
if(MADT_USE_WEBENGINEVIEW)
    find_package(Qt6 REQUIRED COMPONENTS CoreTools Widgets Network WebEngineWidgets)
    set(MADT_QT_WIDGETS_TARGET Qt6::Widgets)
    set(MADT_QT_NETWORK_TARGET Qt6::Network)
    set(MADT_QT_WEB_TARGET Qt6::WebEngineWidgets)
    get_target_property(MADT_HOST_MOC Qt6::moc IMPORTED_LOCATION)
    get_target_property(MADT_HOST_RCC Qt6::rcc IMPORTED_LOCATION)
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network WebKitWidgets)
    set(MADT_QT_WIDGETS_TARGET Qt5::Widgets)
    set(MADT_QT_NETWORK_TARGET Qt5::Network)
    set(MADT_QT_WEB_TARGET Qt5::WebKitWidgets)
    set(MADT_HOST_MOC "${Qt5Core_MOC_EXECUTABLE}")
    set(MADT_HOST_RCC "${Qt5Core_RCC_EXECUTABLE}")
endif()

if(NOT MADT_HOST_MOC)
    message(FATAL_ERROR "Could not resolve MADT host moc from the active Qt package")
endif()
if(NOT MADT_HOST_RCC)
    message(FATAL_ERROR "Could not resolve MADT host rcc from the active Qt package")
endif()
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
    ${MADT_QT_NETWORK_TARGET}
    ${MADT_QT_WIDGETS_TARGET}
    ${UUID_LIBRARY}
)
if(MADT_USE_AVAHI)
    find_package( avahi-client REQUIRED)

    target_compile_definitions(madt-server PRIVATE MADT_USE_AVAHI=1)
    target_link_libraries(madt-server PRIVATE avahi-client::avahi)
endif()
if(MADT_USE_WEBENGINEVIEW)
    target_link_libraries(madt-server PRIVATE ${MADT_QT_WEB_TARGET})
else()
    target_link_libraries(madt-server PRIVATE ${MADT_QT_WEB_TARGET})
endif()
set_target_properties(madt-server PROPERTIES AUTOMOC_EXECUTABLE "${MADT_HOST_MOC}")
set_target_properties(madt-server PROPERTIES AUTORCC_EXECUTABLE "${MADT_HOST_RCC}")
set_target_properties(madt-server PROPERTIES AUTORCC ON)

install(TARGETS madt-server DESTINATION bin)
