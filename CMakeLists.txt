cmake_minimum_required(VERSION 3.5)
project(ajaxremote C CXX)

set(CMAKE_AUTOMOC ON)

find_package(Qt5Core REQUIRED)
find_package(Qt5Gui REQUIRED)
find_package(Qt5Network REQUIRED)
find_package(Qt5Qml REQUIRED)
find_package(Qt5Quick REQUIRED)

set(PROJECT_NAME "ajaxremote")
set(FULL_PROJECT_NAME "ajaxremote.cloudsite")
set(AJAXREMOTE_VERSION "0.1.0")
set(DATA_DIR /)
set(DESKTOP_FILE_NAME ${PROJECT_NAME}.desktop)

configure_file(manifest.json.in ${CMAKE_CURRENT_BINARY_DIR}/manifest.json @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/manifest.json DESTINATION ${CMAKE_INSTALL_PREFIX})
install(FILES ${PROJECT_NAME}.apparmor DESTINATION ${DATA_DIR})
install(FILES assets/logo.svg DESTINATION assets)

qt5_add_resources(QT_RESOURCES qml/qml.qrc assets/assets.qrc)

add_executable(${PROJECT_NAME}
    main.cpp
    src/AjaxApiClient.cpp
    src/AjaxApiClient.h
    src/LocalSecretStore.cpp
    src/LocalSecretStore.h
    src/QrLoginClient.cpp
    src/QrLoginClient.h
    ${QT_RESOURCES}
)
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS_RELEASE -s)
target_compile_definitions(${PROJECT_NAME} PRIVATE AJAXREMOTE_VERSION="${AJAXREMOTE_VERSION}")
target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Gui Qt5::Network Qt5::Qml Qt5::Quick)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})

configure_file(${DESKTOP_FILE_NAME}.in ${CMAKE_CURRENT_BINARY_DIR}/${DESKTOP_FILE_NAME})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${DESKTOP_FILE_NAME} DESTINATION ${DATA_DIR})

file(GLOB_RECURSE PROJECT_SRC_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    qml/*.qml
    qml/*.js
    src/*.cpp
    src/*.h
    assets/*
    third_party/*
    *.json
    *.json.in
    *.apparmor
    *.desktop.in
    *.yaml
    *.md
    docs/*.md
    po/*.po
    po/*.pot
    main.cpp
)

file(GLOB_RECURSE I18N_SRC_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    qml/*.qml
    qml/*.js
)

add_subdirectory(po)

add_custom_target(${PROJECT_NAME}_FILES ALL SOURCES ${PROJECT_SRC_FILES})
