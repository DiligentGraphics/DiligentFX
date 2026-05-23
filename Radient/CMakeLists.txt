cmake_minimum_required (VERSION 3.13)

project(Radient CXX)

set(SOURCE
    src/RadientAssetManagerImpl.cpp
    src/RadientBackendImpl.cpp
    src/RadientEngineImpl.cpp
    src/RadientMaterialBindingsStorage.cpp
    src/RadientMeshComponentStorage.cpp
    src/RadientRendererImpl.cpp
    src/RadientSceneImpl.cpp
    src/RadientSceneState.cpp
    src/RadientSceneWriterImpl.cpp
)

set(INCLUDE
    include/RadientAssetManagerImpl.hpp
    include/RadientBackendImpl.hpp
    include/RadientEngineImpl.hpp
    include/RadientMaterialBindingsStorage.hpp
    include/RadientMath.hpp
    include/RadientMeshComponentStorage.hpp
    include/RadientRendererImpl.hpp
    include/RadientSceneImpl.hpp
    include/RadientSceneState.hpp
    include/RadientSceneWriterImpl.hpp
)

set(INTERFACE
    interface/Radient.h
    interface/RadientAssets.h
    interface/RadientBackend.h
    interface/RadientEngine.h
    interface/RadientMath.h
    interface/RadientRenderer.h
    interface/RadientScene.h
    interface/RadientSceneWriter.h
    interface/RadientTypes.h
)

set(RADIENT_SOURCE ${SOURCE} ${INCLUDE} ${INTERFACE} readme.md)
add_library(Radient STATIC ${RADIENT_SOURCE})

set_common_target_properties(Radient)

target_include_directories(Radient
PUBLIC
    interface
PRIVATE
    include
)

target_link_libraries(Radient
PRIVATE
    Diligent-BuildSettings 
    Diligent-GraphicsEngine
    Diligent-BasicPlatform
    Diligent-GraphicsTools
    Diligent-Common
    EnTT
)

if (MSVC)
    # Disable warning C4324: structure was padded due to alignment specifier
    target_compile_options(Radient PRIVATE /wd4324)
endif()

set_target_properties(Radient PROPERTIES
    FOLDER DiligentFX
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${RADIENT_SOURCE})
