cmake_minimum_required (VERSION 3.13)

project(Diligent-Hydrogent CXX)

include(FetchContent)

message("Fetching entt repository...")
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt
    GIT_TAG        v3.13.1
)
FetchContent_MakeAvailable(entt)
set_directory_root_folder(${entt_SOURCE_DIR} "DiligentFX/ThirdParty/entt")

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
    src/HnShadowMapManager.cpp
    src/HnRenderPassState.cpp
    src/HnFrameRenderTargets.cpp
    src/HnRenderParam.cpp
    src/HnTokens.cpp
    src/HnTextureRegistry.cpp
    src/HnTextureUtils.cpp
    src/HnTypeConversions.cpp
    src/Tasks/HnTask.cpp
    src/Tasks/HnCopySelectionDepthTask.cpp
    src/Tasks/HnBeginFrameTask.cpp
    src/Tasks/HnRenderShadowsTask.cpp
    src/Tasks/HnBeginMainPassTask.cpp
    src/Tasks/HnRenderRprimsTask.cpp
    src/Tasks/HnRenderEnvMapTask.cpp
    src/Tasks/HnRenderBoundBoxTask.cpp
    src/Tasks/HnPostProcessTask.cpp
    src/Tasks/HnProcessSelectionTask.cpp
    src/Tasks/HnReadRprimIdTask.cpp
    src/Tasks/HnTaskManager.cpp
)

set(INCLUDE
    include/HnDrawItem.hpp
    include/HnRenderParam.hpp
    include/HnShaderSourceFactory.hpp
    include/HnShadowMapManager.hpp
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
    interface/HnFrameRenderTargets.hpp
    interface/HnTokens.hpp
    interface/HnTypes.hpp
    interface/HnTextureRegistry.hpp
    interface/Tasks/HnTask.hpp
    interface/Tasks/HnCopySelectionDepthTask.hpp
    interface/Tasks/HnBeginFrameTask.hpp
    interface/Tasks/HnBeginMainPassTask.hpp
    interface/Tasks/HnRenderShadowsTask.hpp
    interface/Tasks/HnRenderRprimsTask.hpp
    interface/Tasks/HnRenderEnvMapTask.hpp
    interface/Tasks/HnRenderBoundBoxTask.hpp
    interface/Tasks/HnPostProcessTask.hpp
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

if(${CMAKE_GENERATOR} MATCHES "Visual Studio" OR
   ${CMAKE_GENERATOR} MATCHES "Xcode")
    # Since we can't use wildcards in generator expressions, we have to
    # find boost directory for each configuration
    file(GLOB BOOST_INCLUDE_DEBUG "${DILIGENT_USD_PATH}/Debug/include/boost*")
    file(GLOB BOOST_INCLUDE_RELEASE "${DILIGENT_USD_PATH}/Release/include/boost*")
    file(GLOB BOOST_INCLUDE_RELWITHDEBINFO "${DILIGENT_USD_PATH}/RelWithDebInfo/include/boost*")

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
    # Load the pxr package, which will bring all USD libraries
    set(pxr_DIR ${DILIGENT_USD_PATH})
    find_package(pxr)
    target_include_directories(USD-Libraries INTERFACE ${PXR_INCLUDE_DIRS})
endif()

if(MSVC)
    target_compile_definitions(USD-Libraries INTERFACE NOMINMAX)
    # Silence compiler warnings and enable RTTI
    target_compile_options(USD-Libraries INTERFACE /wd4244 /wd4305 /wd4127 /wd4267 /wd4003 /GR)
    target_compile_options(Diligent-Hydrogent PRIVATE /permissive-)
else()
    target_compile_definitions(USD-Libraries INTERFACE _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION=1)
    target_compile_options(USD-Libraries INTERFACE
        -Wno-deprecated-declarations
        -Wno-conversion
        -Wno-deprecated-copy
        -Wno-missing-field-initializers)

    # Set default visibility or there will be issues with VtType
    set_target_properties(Diligent-Hydrogent PROPERTIES CXX_VISIBILITY_PRESET default)
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
    usdGeom
    work
    usdLux
)

if (NOT pxr_FOUND)
    # Add usd_ prefix to all USD libraries
    set(_USD_LIBS "")
    foreach(lib ${USD_LIBS})
        list(APPEND _USD_LIBS "usd_${lib}")
    endforeach()
    set(USD_LIBS ${_USD_LIBS})

    if (PLATFORM_MACOS)
        file(GLOB BOOST_PYTHON_LIB_DEBUG "${DILIGENT_USD_PATH}/Debug/lib/*boost_python*")
        file(GLOB BOOST_PYTHON_LIB_RELEASE "${DILIGENT_USD_PATH}/Release/lib/*boost_python*")
        file(GLOB BOOST_PYTHON_LIB_RELWITHDEBINFO "${DILIGENT_USD_PATH}/RelWithDebInfo/lib/*boost_python*")

        target_link_libraries(USD-Libraries
        INTERFACE
            $<$<CONFIG:DEBUG>:${BOOST_PYTHON_LIB_DEBUG}>
            $<$<CONFIG:RELEASE>:${BOOST_PYTHON_LIB_RELEASE}>
            $<$<CONFIG:RELWITHDEBINFO>:${BOOST_PYTHON_LIB_RELWITHDEBINFO}>
        )
    endif()

    # Add python library
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
    target_link_libraries(USD-Libraries INTERFACE Python3::Python)
endif()

target_link_libraries(USD-Libraries INTERFACE ${USD_LIBS})

target_include_directories(Diligent-Hydrogent
PUBLIC
    interface
PRIVATE
    include
    ${SHADER_OUTPUT_DIR}
)

target_link_libraries(Diligent-Hydrogent
PRIVATE
    Diligent-BuildSettings 
    NO_WERROR
    Diligent-GraphicsEngine
    Diligent-BasicPlatform
    Diligent-GraphicsTools
    Diligent-TextureLoader
    Diligent-Common
    DiligentFX
PUBLIC
    EnTT
    USD-Libraries
)

set_target_properties(Diligent-Hydrogent PROPERTIES
    FOLDER DiligentFX
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${HYDROGENT_SOURCE})
source_group("generated" FILES
    ${SHADERS_LIST_FILE}
    ${SHADERS_INC_LIST}
)
