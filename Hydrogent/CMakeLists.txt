cmake_minimum_required (VERSION 3.13)

project(Diligent-Hydrogent CXX)

set(SOURCE
    src/HnMaterial.cpp
    src/HnMaterialNetwork.cpp
    src/HnMesh.cpp
    src/HnRenderDelegate.cpp
    src/HnRendererImpl.cpp
    src/HnTokens.cpp
    src/HnTextureRegistry.cpp
    src/HnTextureUtils.cpp
    src/HnTypeConversions.cpp
)

set(INCLUDE
    include/HnMaterial.hpp
    include/HnMaterialNetwork.hpp
    include/HnMesh.hpp
    include/HnRenderDelegate.hpp
    include/HnRendererImpl.hpp
    include/HnTokens.hpp
    include/HnTextureIdentifier.hpp
    include/HnTextureRegistry.hpp
    include/HnTextureUtils.hpp
    include/HnTypeConversions.hpp
)

set(INTERFACE
    interface/HnRenderer.hpp
)

add_library(Diligent-Hydrogent STATIC README.md ${SOURCE} ${INCLUDE} ${INTERFACE})

set_common_target_properties(Diligent-Hydrogent)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

add_library(USD_LIBS INTERFACE)

if(${CMAKE_GENERATOR} MATCHES "Visual Studio")
    # Boost version depends on Visual Studio version. Since we can't use wildcards in
    # generator expressions, we have to find boost directory for each configuration
    file(GLOB BOOST_INCLUDE_DEBUG "${DILIGENT_USD_PATH}/Debug/include/boost-*")
    file(GLOB BOOST_INCLUDE_RELEASE "${DILIGENT_USD_PATH}/Release/include/boost-*")
    file(GLOB BOOST_INCLUDE_RELWITHDEBINFO "${DILIGENT_USD_PATH}/RelWithDebInfo/include/boost-*")

    target_include_directories(USD_LIBS
    INTERFACE
        ${DILIGENT_USD_PATH}/$<CONFIG>/include
        $<$<CONFIG:DEBUG>:${BOOST_INCLUDE_DEBUG}>
        $<$<CONFIG:RELEASE>:${BOOST_INCLUDE_RELEASE}>
        $<$<CONFIG:RELWITHDEBINFO>:${BOOST_INCLUDE_RELWITHDEBINFO}>
    )

    target_link_directories(USD_LIBS
    INTERFACE
        ${DILIGENT_USD_PATH}/$<CONFIG>/lib
    )
else()
    file(GLOB BOOST_INCLUDE_DEBUG "${DILIGENT_USD_PATH}/Debug/boost-*")
    target_include_directories(USD_LIBS
    INTERFACE
        ${DILIGENT_USD_PATH}/include
        ${BOOST_INCLUDE_DEBUG}
    )

    target_link_directories(USD_LIBS
    INTERFACE
        ${DILIGENT_USD_PATH}/lib
    )
endif()

target_include_directories(USD_LIBS
INTERFACE
    ${Python3_INCLUDE_DIRS}
)

if(MSVC)
    target_compile_definitions(USD_LIBS INTERFACE NOMINMAX)
    target_compile_options(USD_LIBS INTERFACE /wd4244 /wd4305 /wd4127 /wd4267)
endif()

target_link_libraries(USD_LIBS
INTERFACE
    usd_gf
    usd_ar
    usd_sdf
    usd_tf
    usd_usd
    usd_plug
    usd_hd
    usd_usdImaging
    usd_vt
    usd_hio
    usd_sdr
    usd_ndr
)

target_include_directories(Diligent-Hydrogent
PUBLIC
    interface
PRIVATE
    include
)

target_link_directories(Diligent-Hydrogent
PUBLIC 
    ${Python3_LIBRARY_DIRS}
)

# We need to enable RTTI for USD
add_library(EnableRTTI INTERFACE)
target_compile_options(EnableRTTI INTERFACE /GR)

target_link_libraries(Diligent-Hydrogent
PRIVATE
    USD_LIBS
    NO_WERROR
    EnableRTTI # USD requires RTTI
    Diligent-GraphicsEngine
    Diligent-BasicPlatform
    Diligent-GraphicsTools
    Diligent-TextureLoader
    Diligent-Common
    DiligentFX
)

set_target_properties(Diligent-Hydrogent PROPERTIES
    FOLDER DiligentFX
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE} ${INCLUDE} ${INTERFACE})