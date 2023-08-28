cmake_minimum_required (VERSION 3.13)

project(Diligent-Hydrogent CXX)

set(SOURCE
    src/Material.cpp
    src/Mesh.cpp
    src/RenderDelegate.cpp
    src/Renderer.cpp
)

set(INCLUDE
    include/Material.hpp
    include/Mesh.hpp
    include/RenderDelegate.hpp
    include/Renderer.hpp
)

add_library(Diligent-Hydrogent STATIC README.md ${SOURCE} ${INCLUDE})
target_include_directories(Diligent-Hydrogent PUBLIC include)

set_common_target_properties(Diligent-Hydrogent)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

if(${CMAKE_GENERATOR} MATCHES "Visual Studio")
    # Boost version depends on Visual Studio version. Since we can't use wildcards in
    # generator expressions, we have to find boost directory for each configuration
    file(GLOB BOOST_INCLUDE_DEBUG "${DILIGENT_USD_PATH}/Debug/include/boost-*")
    file(GLOB BOOST_INCLUDE_RELEASE "${DILIGENT_USD_PATH}/Release/include/boost-*")
    file(GLOB BOOST_INCLUDE_RELWITHDEBINFO "${DILIGENT_USD_PATH}/RelWithDebInfo/include/boost-*")

    target_include_directories(Diligent-Hydrogent
    PRIVATE
        ${DILIGENT_USD_PATH}/$<CONFIG>/include
        $<$<CONFIG:DEBUG>:${BOOST_INCLUDE_DEBUG}>
        $<$<CONFIG:RELEASE>:${BOOST_INCLUDE_RELEASE}>
        $<$<CONFIG:RELWITHDEBINFO>:${BOOST_INCLUDE_RELWITHDEBINFO}>
    )

    target_link_directories(Diligent-Hydrogent
    PUBLIC
        ${DILIGENT_USD_PATH}/$<CONFIG>/lib
    )
else()
    file(GLOB BOOST_INCLUDE_DEBUG "${DILIGENT_USD_PATH}/Debug/boost-*")
    target_include_directories(Diligent-Hydrogent
    PRIVATE
        ${DILIGENT_USD_PATH}/include
        ${BOOST_INCLUDE_DEBUG}
    )

    target_link_directories(Diligent-Hydrogent
    PUBLIC
        ${DILIGENT_USD_PATH}/lib
    )
endif()

target_include_directories(Diligent-Hydrogent
PRIVATE
    include
    ${Python3_INCLUDE_DIRS}
)

target_link_directories(Diligent-Hydrogent
PUBLIC 
    ${Python3_LIBRARY_DIRS}
)

# We need to enable RTTI for USD
add_library(EnableRTTI INTERFACE)
target_compile_options(EnableRTTI INTERFACE /GR)

target_link_libraries(Diligent-Hydrogent
PUBLIC
    usd_sdf
    usd_tf
    usd_usd
    usd_plug
    usd_hd
    usd_usdImaging
    usd_vt
PRIVATE
    NO_WERROR
    EnableRTTI # USD requires RTTI
    Diligent-GraphicsEngine
    Diligent-BasicPlatform
    Diligent-GraphicsTools
)

if(PLATFORM_WIN32 OR PLATFORM_UNIVERSAL_WINDOWS)
    target_compile_definitions(Diligent-Hydrogent PRIVATE NOMINMAX)
endif()

if(MSVC)
    target_compile_options(Diligent-Hydrogent PRIVATE /wd4244 /wd4305 /wd4127 /wd4267)
endif()

set_target_properties(Diligent-Hydrogent PROPERTIES
    FOLDER DiligentFX
)
