project(http)

aux_source_directory(./ SOURCE)

include_directories(http
    PRIVATE
    ./
    ${CMAKE_SOURCE_DIR}/component/osal
    ${CMAKE_SOURCE_DIR}/lvgl
)

add_library(http STATIC ${SOURCE})

if(SIMULATOR_LINUX)
    find_package(OpenSSL QUIET)
    if(OPENSSL_FOUND)
        target_compile_definitions(http PRIVATE HTTP_HAS_OPENSSL=1)
        target_link_libraries(http PUBLIC
                    cjson
                    curl
                    OpenSSL::SSL
                    OpenSSL::Crypto
                    nghttp2
                    dl
                    osal
                    )
    else()
        target_compile_definitions(http PRIVATE HTTP_HAS_OPENSSL=0)
        target_link_libraries(http PUBLIC
                    cjson
                    curl
                    nghttp2
                    dl
                    osal
                    )
    endif()
else()
    target_compile_definitions(http PRIVATE HTTP_HAS_OPENSSL=1)
    target_link_libraries(http PUBLIC
                cjson
                curl
                ssl
                crypto
                nghttp2
                dl
                z
                osal
                )
endif()