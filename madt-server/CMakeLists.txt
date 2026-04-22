find_package(Qt5 REQUIRED COMPONENTS Widgets)
if(MADT_USE_WEBENGINEVIEW)
    find_package(Qt5 REQUIRED COMPONENTS WebEngineWidgets)
else()
    find_package(Qt5 REQUIRED COMPONENTS WebKitWidgets)
endif()

find_program(MADT_HOST_MOC moc REQUIRED)
find_program(MADT_HOST_RCC rcc REQUIRED)
find_library(UUID_LIBRARY uuid REQUIRED)

add_executable(madt-server
    main.cpp
    gui.cpp
    Browser.cpp
    madt-server.cpp
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
    Qt5::Widgets
    ${UUID_LIBRARY}
)
if(MADT_USE_WEBENGINEVIEW)
    target_link_libraries(madt-server PRIVATE Qt5::WebEngineWidgets)
else()
    target_link_libraries(madt-server PRIVATE Qt5::WebKitWidgets)
endif()
set_target_properties(madt-server PROPERTIES AUTOMOC_EXECUTABLE "${MADT_HOST_MOC}")
set_target_properties(madt-server PROPERTIES AUTORCC_EXECUTABLE "${MADT_HOST_RCC}")
set_target_properties(madt-server PROPERTIES AUTORCC ON)

install(TARGETS madt-server DESTINATION bin)
