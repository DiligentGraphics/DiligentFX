cmake_minimum_required (VERSION 3.13)

project(Diligent-Hydrogent CXX)

# We need to bring in the OpenUSD package explicitly so that USDViewer can
# link with the right version of python and boost::python
if(PLATFORM_LINUX)
    set(pxr_DIR ${DILIGENT_USD_PATH})
    find_package(pxr)
endif()

set(SOURCE
    src/HnMaterial.cpp
    src/HnMaterialNetwork.cpp
    src/HnMesh.cpp
    src/HnBuffer.cpp
    src/HnDrawItem.cpp
    src/HnCamera.cpp
    src/HnLight.cpp
    src/HnRenderBuffer.cpp
    src/HnRenderDelegate.cpp
    src/HnRenderPass.cpp
    src/HnShaderSourceFactory.cpp
    src/HnRenderPassState.cpp
    src/HnRenderParam.cpp
    src/HnTokens.cpp
    src/HnTextureRegistry.cpp
    src/HnTextureUtils.cpp
    src/HnTypeConversions.cpp
    src/Tasks/HnTask.cpp
    src/Tasks/HnCopySelectionDepthTask.cpp
    src/Tasks/HnBeginFrameTask.cpp
    src/Tasks/HnRenderRprimsTask.cpp
    src/Tasks/HnRenderEnvMapTask.cpp
    src/Tasks/HnRenderAxesTask.cpp
    src/Tasks/HnPostProcessTask.cpp
    src/Tasks/HnSetupSelectionDepthTask.cpp
    src/Tasks/HnProcessSelectionTask.cpp
    src/Tasks/HnReadRprimIdTask.cpp
    src/Tasks/HnTaskManager.cpp
)

set(INCLUDE
    include/HnDrawItem.hpp
    include/HnRenderParam.hpp
    include/HnShaderSourceFactory.hpp
    include/HnTypeConversions.hpp
    include/HnTextureUtils.hpp
    include/HnTextureIdentifier.hpp
)

set(INTERFACE
    interface/GfTypeConversions.hpp
    interface/HnMaterial.hpp
    interface/HnMaterialNetwork.hpp
    interface/HnMesh.hpp
    interface/HnBuffer.hpp
    interface/HnCamera.hpp
    interface/HnLight.hpp
    interface/HnRenderBuffer.hpp
    interface/HnRenderDelegate.hpp
    interface/HnRenderPass.hpp
    interface/HnRenderPassState.hpp
    interface/HnTokens.hpp
    interface/HnTypes.hpp
    interface/HnTextureRegistry.hpp
    interface/Tasks/HnTask.hpp
    interface/Tasks/HnCopySelectionDepthTask.hpp
    interface/Tasks/HnBeginFrameTask.hpp
    interface/Tasks/HnRenderRprimsTask.hpp
    interface/Tasks/HnRenderEnvMapTask.hpp
    interface/Tasks/HnRenderAxesTask.hpp
    interface/Tasks/HnPostProcessTask.hpp
    interface/Tasks/HnSetupSelectionDepthTask.hpp
    interface/Tasks/HnProcessSelectionTask.hpp
    interface/Tasks/HnReadRprimIdTask.hpp
    interface/Tasks/HnTaskManager.hpp
)

file(GLOB_RECURSE SHADERS LIST_DIRECTORIES false shaders/*.*)

# Convert shaders to headers and generate master header with the list of all files
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders_inc)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})
set(SHADERS_LIST_FILE ${SHADER_OUTPUT_DIR}/shaders_list.h)
convert_shaders_to_headers("${SHADERS}" ${SHADER_OUTPUT_DIR} ${SHADERS_LIST_FILE} SHADERS_INC_LIST)

set(HYDROGENT_SOURCE ${SOURCE} ${INCLUDE} ${INTERFACE} ${SHADERS} readme.md)
add_library(Diligent-Hydrogent STATIC ${HYDROGENT_SOURCE} ${SHADERS_LIST_FILE} ${SHADERS_INC_LIST})

set_common_target_properties(Diligent-Hydrogent)

add_library(USD-Libraries INTERFACE)

if(pxr_FOUND)
    # PXR is already loaded, so we can use its variables.
    target_include_directories(USD-Libraries INTERFACE ${PXR_INCLUDE_DIRS})
elseif(${CMAKE_GENERATOR} MATCHES "Visual Studio")
    # Boost version depends on Visual Studio version. Since we can't use wildcards in
    # generator expressions, we have to find boost directory for each configuration
    file(GLOB BOOST_INCLUDE_DEBUG "${DILIGENT_USD_PATH}/Debug/include/boost-*")
    file(GLOB BOOST_INCLUDE_RELEASE "${DILIGENT_USD_PATH}/Release/include/boost-*")
    file(GLOB BOOST_INCLUDE_RELWITHDEBINFO "${DILIGENT_USD_PATH}/RelWithDebInfo/include/boost-*")

    target_include_directories(USD-Libraries
    INTERFACE
        ${DILIGENT_USD_PATH}/$<CONFIG>/include
        $<$<CONFIG:DEBUG>:${BOOST_INCLUDE_DEBUG}>
        $<$<CONFIG:RELEASE>:${BOOST_INCLUDE_RELEASE}>
        $<$<CONFIG:RELWITHDEBINFO>:${BOOST_INCLUDE_RELWITHDEBINFO}>
    )

    target_link_directories(USD-Libraries
    INTERFACE
        ${DILIGENT_USD_PATH}/$<CONFIG>/lib
    )
else()
    file(GLOB BOOST_INCLUDE_DEBUG "${DILIGENT_USD_PATH}/Debug/boost-*")
    target_include_directories(USD-Libraries
    INTERFACE
        ${DILIGENT_USD_PATH}/include
        ${BOOST_INCLUDE_DEBUG}
    )

    target_link_directories(USD-Libraries
    INTERFACE
        ${DILIGENT_USD_PATH}/lib
    )
endif()

if(MSVC)
    target_compile_definitions(USD-Libraries INTERFACE NOMINMAX)
    target_compile_options(USD-Libraries INTERFACE /wd4244 /wd4305 /wd4127 /wd4267 /wd4003)
    target_compile_options(Diligent-Hydrogent PRIVATE /permissive-)
else()
    target_compile_options(USD-Libraries INTERFACE -Wno-deprecated-builtins -Wno-deprecated-declarations)
endif()

set(USD_LIBS
    gf
    ar
    sdf
    tf
    usd
    plug
    hd
    usdImaging
    vt
    hio
    sdr
    ndr
    trace
    cameraUtil
)

if (NOT pxr_FOUND)
    # Add usd_ prefix to all USD libraries
    set(_USD_LIBS "")
    foreach(lib ${USD_LIBS})
        list(APPEND _USD_LIBS "usd_${lib}")
    endforeach()
    set(USD_LIBS ${_USD_LIBS})

    # Add python library
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

    target_include_directories(USD-Libraries INTERFACE ${Python3_INCLUDE_DIRS})
    target_link_directories(USD-Libraries INTERFACE ${Python3_LIBRARY_DIRS})
endif()

target_link_libraries(USD-Libraries INTERFACE ${USD_LIBS})

if(MSVC)
    # USD requires RTTI
    target_compile_options(USD-Libraries INTERFACE /GR)
endif()

target_include_directories(Diligent-Hydrogent
PUBLIC
    interface
PRIVATE
    include
    ${SHADER_OUTPUT_DIR}
)

target_link_libraries(Diligent-Hydrogent
PRIVATE
    USD-Libraries
    NO_WERROR
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

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${HYDROGENT_SOURCE})
source_group("generated" FILES
    ${SHADERS_LIST_FILE}
    ${SHADERS_INC_LIST}
)
