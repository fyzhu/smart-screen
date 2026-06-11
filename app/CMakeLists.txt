cmake_minimum_required(VERSION 3.15)

project(demo)

add_subdirectory(res)

aux_source_directory(. SOURCES)
aux_source_directory(ui SOURCES)
aux_source_directory(ui/data SOURCES)
aux_source_directory(ui/utils SOURCES)
aux_source_directory(ui/utils/clock SOURCES)
aux_source_directory(ui/utils/file SOURCES)

add_executable(demo ${SOURCES})

target_include_directories(demo PRIVATE
                        ./
                        ui/
                        ui/data/
                        ui/utils/
                        ui/utils/clock/
                        ui/utils/file/
                        res/
                        res/color/
                        res/font/
                        res/music/
                        res/image/
                        ${CMAKE_SOURCE_DIR}/lvgl
                        ${CMAKE_SOURCE_DIR}/lvgl/src
                        ${CMAKE_SOURCE_DIR}/lvgl/demos
                        ${CMAKE_SOURCE_DIR}/component/hal
                        ${CMAKE_SOURCE_DIR}/component/wifi
                        ${CMAKE_SOURCE_DIR}/component/net
                        ${CMAKE_SOURCE_DIR}/component/osal
                        ${CMAKE_SOURCE_DIR}/component/font
                        ${CMAKE_SOURCE_DIR}/component/player
                        ${CMAKE_SOURCE_DIR}/component/usb_hid
                        ${CMAKE_SOURCE_DIR}/component/net/inc
)

target_link_libraries(demo PRIVATE
                        lvgl_demos
                        lvgl
                        lvgl_porting
                        wifi
                        http
                        player
                        font
                        usb_hid
                        )


if(SIMULATOR_LINUX)
    add_compile_definitions(SIMULATOR_LINUX)
    set(PROJECT_RES_URL ${PROJECT_BINARY_DIR}/res/)
else()
    set(PROJECT_RES_URL /usr/res/)
endif()
target_compile_definitions(demo PUBLIC PROJECT_RES_URL="${PROJECT_RES_URL}")
message("PROJECT_RES_URL is ${PROJECT_RES_URL}")


