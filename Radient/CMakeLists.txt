cmake_minimum_required (VERSION 3.13)

project(Radient CXX)

set(SOURCE
    src/Assets/RadientAssetManagerImpl.cpp
    src/Assets/RadientAssetValidation.cpp
    src/Assets/RadientDrawableMeshConverter.cpp
    src/Assets/RadientMaterialAssetManager.cpp
    src/Assets/RadientMeshPrimitives.cpp
    src/Assets/RadientMeshSource.cpp
    src/Assets/RadientTextureAssetManager.cpp
    src/Assets/RadientTextureSource.cpp
    src/Core/RadientBackendImpl.cpp
    src/Core/RadientEngineImpl.cpp
    src/Core/RadientViewImpl.cpp
    src/Import/RadientGLTFConverter.cpp
    src/Import/RadientSceneImporterImpl.cpp
    src/Math/RadientMath.cpp
    src/Render/Passes/RadientGeometryPass.cpp
    src/Render/Passes/RadientPostProcessPipeline.cpp
    src/Render/Passes/RadientSkyboxPass.cpp
    src/Render/RadientDrawList.cpp
    src/Render/RadientFrameRenderTargets.cpp
    src/Render/RadientLightList.cpp
    src/Render/RadientRenderPipeline.cpp
    src/Render/RadientRendererImpl.cpp
    src/Render/RadientSceneDrawableCache.cpp
    src/Scene/Components/RadientMaterialBindingsStorage.cpp
    src/Scene/Components/RadientMeshComponentStorage.cpp
    src/Scene/RadientSceneImpl.cpp
    src/Scene/RadientSceneState.cpp
    src/Scene/RadientSceneWriterImpl.cpp
)

set(INCLUDE
    include/Assets/RadientAssetCache.hpp
    include/Assets/RadientAssetImpl.hpp
    include/Assets/RadientAssetManagerImpl.hpp
    include/Assets/RadientAssetValidation.hpp
    include/Assets/RadientDrawableMeshConverter.hpp
    include/Assets/RadientMaterialAssetManager.hpp
    include/Assets/RadientMeshSource.hpp
    include/Assets/RadientTextureAssetManager.hpp
    include/Assets/RadientTextureSource.hpp
    include/Core/RadientBackendImpl.hpp
    include/Core/RadientEngineImpl.hpp
    include/Core/RadientViewImpl.hpp
    include/Import/RadientGLTFConverter.hpp
    include/Import/RadientSceneImporterImpl.hpp
    include/Math/RadientMath.hpp
    include/Render/Passes/RadientGeometryPass.hpp
    include/Render/Passes/RadientPostProcessPipeline.hpp
    include/Render/Passes/RadientSkyboxPass.hpp
    include/Render/RadientDrawableMesh.hpp
    include/Render/RadientDrawList.hpp
    include/Render/RadientFrameRenderTargets.hpp
    include/Render/RadientLightList.hpp
    include/Render/RadientRenderPipeline.hpp
    include/Render/RadientRendererImpl.hpp
    include/Render/RadientSceneDrawableCache.hpp
    include/Scene/Components/RadientMaterialBindingsStorage.hpp
    include/Scene/Components/RadientMeshComponentStorage.hpp
    include/Scene/RadientSceneImpl.hpp
    include/Scene/RadientSceneState.hpp
    include/Scene/RadientSceneWriterImpl.hpp
)

set(INTERFACE
    interface/Radient.h
    interface/RadientAssets.h
    interface/RadientBackend.h
    interface/RadientEngine.h
    interface/RadientMath.h
    interface/RadientMeshPrimitives.h
    interface/RadientRenderer.h
    interface/RadientScene.h
    interface/RadientSceneImporter.h
    interface/RadientSceneWriter.h
    interface/RadientTypes.h
    interface/RadientView.h
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
    Diligent-AssetLoader
    Diligent-TextureLoader
    Diligent-Common
    DiligentFX
    EnTT
    absl::flat_hash_map
)

if (MSVC)
    # Disable warning C4324: structure was padded due to alignment specifier
    target_compile_options(Radient PRIVATE /wd4324)
endif()

set_target_properties(Radient PROPERTIES
    FOLDER DiligentFX
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${RADIENT_SOURCE})
