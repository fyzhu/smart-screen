
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 新版本仅需修改build.sh的编译器路径即可，这里无需添加

set(CMAKE_C_COMPILER ${TOOLCHAIN_PATH}/arm-openwrt-linux-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PATH}/arm-openwrt-linux-g++)

get_filename_component(TOOLCHAIN_ROOT "${TOOLCHAIN_PATH}/.." ABSOLUTE)
set(CMAKE_SYSROOT "${TOOLCHAIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_ROOT}" "${CMAKE_CURRENT_LIST_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

add_compile_options(--sysroot=${TOOLCHAIN_ROOT})
add_link_options(--sysroot=${TOOLCHAIN_ROOT})
add_link_options(-L${CMAKE_CURRENT_LIST_DIR}/lib)
add_link_options(-L${CMAKE_SOURCE_DIR}/wifi/libs/)
add_link_options(-L${CMAKE_SOURCE_DIR}/net/libs/)
add_link_options(-Wl,-rpath,/lib:/usr/lib:/lib64:/usr/lib64)
add_link_options(-Wl,-rpath-link,${CMAKE_CURRENT_LIST_DIR}/lib)

add_link_options(-lpthread -lfreetype -lrt -ldl -znow -zrelro -luapi -lm -lz -lbz2 -O0 -rdynamic -g -funwind-tables -ffunction-sections)
add_link_options(-fPIC -Wl,-gc-sections)
add_compile_options(-I${CMAKE_CURRENT_LIST_DIR}/src/porting)
add_compile_options(-I${CMAKE_SOURCE_DIR})
add_compile_options(-I${CMAKE_SOURCE_DIR}/lvgl/demos)
add_compile_options(-I${CMAKE_CURRENT_LIST_DIR}/include)
add_compile_options(-I${CMAKE_CURRENT_LIST_DIR}/include/freetype)
add_compile_options(-idirafter${CMAKE_CURRENT_LIST_DIR}/include)
add_compile_options(-idirafter${CMAKE_CURRENT_LIST_DIR}/include/freetype)

add_compile_options(-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard -O0 -ldl -rdynamic -g -funwind-tables -ffunction-sections)