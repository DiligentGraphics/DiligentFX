#ifndef _PBR_STRUCTURES_FXH_
#define _PBR_STRUCTURES_FXH_

#ifdef __cplusplus

#   ifndef CHECK_STRUCT_ALIGNMENT
        // Note that defining empty macros causes GL shader compilation error on Mac, because
        // it does not allow standalone semicolons outside of main.
        // On the other hand, adding semicolon at the end of the macro definition causes gcc error.
#       define CHECK_STRUCT_ALIGNMENT(s) static_assert( sizeof(s) % 16 == 0, "sizeof(" #s ") is not multiple of 16" )
#   endif

#ifndef INLINE
#   define INLINE inline
#endif

#else

#ifndef INLINE
#   define INLINE
#endif

#endif

#ifndef PBR_WORKFLOW_METALLIC_ROUGHNESS
#   define PBR_WORKFLOW_METALLIC_ROUGHNESS 0
#endif

#ifndef PBR_WORKFLOW_SPECULAR_GLOSSINESS
#   define PBR_WORKFLOW_SPECULAR_GLOSSINESS 1
#endif


#ifndef PBR_ALPHA_MODE_OPAQUE
#   define PBR_ALPHA_MODE_OPAQUE 0
#endif

#ifndef PBR_ALPHA_MODE_MASK
#   define PBR_ALPHA_MODE_MASK 1
#endif

#ifndef PBR_ALPHA_MODE_BLEND
#   define PBR_ALPHA_MODE_BLEND 2
#endif

#ifndef ENABLE_SHEEN
#   define ENABLE_SHEEN 0
#endif
    
#ifndef ENABLE_ANISOTROPY
#   define ENABLE_ANISOTROPY 0
#endif
    
#ifndef ENABLE_IRIDESCENCE
#   define ENABLE_IRIDESCENCE 0
#endif
    
#ifndef ENABLE_TRANSMISSION
#   define ENABLE_TRANSMISSION 0
#endif
    
#ifndef ENABLE_VOLUME
#   define ENABLE_VOLUME 0
#endif
    
#ifndef PBR_NUM_TEXTURE_ATTRIBUTES
#   define PBR_NUM_TEXTURE_ATTRIBUTES 0
#endif

#ifndef COMPUTE_MOTION_VECTORS
#   define COMPUTE_MOTION_VECTORS 0
#endif

#ifndef USE_JOINTS
#   define USE_JOINTS 0
#endif

#ifndef USE_SKIN_PRE_TRANSFORM
#   define USE_SKIN_PRE_TRANSFORM 0
#endif

// When updating this structure, make sure to update GLTF_PBR_Renderer::WritePBRPrimitiveShaderAttribs,
// and PBR_Renderer::GetPBRPrimitiveAttribsSize.
struct GLTFNodeShaderTransforms
{
	float4x4 NodeMatrix;
#if COMPUTE_MOTION_VECTORS
    float4x4 PrevNodeMatrix;
#endif

	int   JointCount;
    int   FirstJoint; // Index of the first joint in the joints buffer to start from
    float PosBiasX;   // Bias to apply to the position
    float PosBiasY;
    
    float PosBiasZ;   // Scale and bias are used to unpack
    float PosScaleX;  // position (Pos = Pos * PosScale + PosBias)
    float PosScaleY;  // and are applied before the NodeMatrix.
    float PosScaleZ;
    
#if USE_JOINTS && USE_SKIN_PRE_TRANSFORM
    float4x4 SkinPreTransform;
#   if COMPUTE_MOTION_VECTORS
        float4x4 PrevSkinPreTransform;
#   endif
#endif
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(GLTFNodeShaderTransforms);
#endif

struct LoadingAnimationShaderParameters
{
    float Factor;
    float WorldScale;
    float Speed;
    float Padding;
    
    float4 Color0;
    float4 Color1;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(LoadingAnimationShaderParameters);
#endif

struct PBRRendererShaderParameters
{
	float AverageLogLum;
	float MiddleGray;
    float WhitePoint;
	float PrefilteredCubeLastMip; // Prefiltered cube map last mip level

	float4 IBLScale;

    float OcclusionStrength;
    float EmissionScale;
    float PointSize; // OpenGL and Vulkan
    float MipBias;

    int   LightCount;
    float Time;
    int   DebugView;
    float Padding0;
    
    float4 UnshadedColor;
    float4 HighlightColor;

    LoadingAnimationShaderParameters LoadingAnimation;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRRendererShaderParameters);
#endif

struct PBRMaterialBasicAttribs
{
    float4 BaseColorFactor;

    float EmissiveFactorR;
    float EmissiveFactorG;
    float EmissiveFactorB;
    float NormalScale;

    float SpecularFactorR;
    float SpecularFactorG;
    float SpecularFactorB;
    float ClearcoatNormalScale;

    int   Workflow;
    int   AlphaMode;
    float AlphaMaskCutoff;
    float MetallicFactor;

    float RoughnessFactor;
    float OcclusionFactor;
    float ClearcoatFactor;
    float ClearcoatRoughnessFactor;

    // Any user-specific data
    float4 CustomData;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialBasicAttribs);
#endif

struct PBRMaterialSheenAttribs
{
    float ColorFactorR;
    float ColorFactorG;
    float ColorFactorB;
    float RoughnessFactor;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialSheenAttribs);
#endif

struct PBRMaterialAnisotropyAttribs
{
    float Strength;
    float Rotation;
    float Padding0;
    float Padding1;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialAnisotropyAttribs);
#endif

struct PBRMaterialIridescenceAttribs
{
    float Factor;
    float IOR;
    float ThicknessMinimum;
    float ThicknessMaximum;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialIridescenceAttribs);
#endif

struct PBRMaterialTransmissionAttribs
{
    float Factor;
    float Padding0;
    float Padding1;
    float Padding2;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialTransmissionAttribs);
#endif

struct PBRMaterialVolumeAttribs
{
    float AttenuationColorR;
    float AttenuationColorG;
    float AttenuationColorB;
    float ThicknessFactor;

    float AttenuationDistance;
    float Padding0;
    float Padding1;
    float Padding2;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialVolumeAttribs);
#endif

struct PBRMaterialTextureAttribs
{
    uint  PackedProps; // See GLTF::Material::TextureShaderAttribs
    float TextureSlice;
    float UBias;
    float VBias;

    float4 UVScaleAndRotation;
    float4 AtlasUVScaleAndBias;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialTextureAttribs);
#endif

INLINE float UnpackPBRMaterialTextureUVSelector(uint PackedProps)
{
    // See GLTF::Material::TextureShaderAttribs::SetUVSelector
    return float(PackedProps & 7u) - float(1.0);
}

INLINE uint UnpackPBRMaterialTextureWrapUMode(uint PackedProps)
{
    // See GLTF::Material::TextureShaderAttribs::SetWrapUMode
    return ((PackedProps >> 3u) & 7u) + 1u;
}

INLINE uint UnpackPBRMaterialTextureWrapVMode(uint PackedProps)
{
    // See GLTF::Material::TextureShaderAttribs::SetWrapVMode
    return ((PackedProps >> 6u) & 7u) + 1u;
}

struct PBRMaterialShaderInfo
{
    PBRMaterialBasicAttribs Basic;
    
#if ENABLE_SHEEN
    PBRMaterialSheenAttribs Sheen;
#endif
    
#if ENABLE_ANISOTROPY
    PBRMaterialAnisotropyAttribs Anisotropy;
#endif
    
#if ENABLE_IRIDESCENCE
    PBRMaterialIridescenceAttribs Iridescence;
#endif
    
#if ENABLE_TRANSMISSION
    PBRMaterialTransmissionAttribs Transmission;
#endif
    
#if ENABLE_VOLUME
    PBRMaterialVolumeAttribs Volume;
#endif
    
#if PBR_NUM_TEXTURE_ATTRIBUTES > 0
    PBRMaterialTextureAttribs Textures[PBR_NUM_TEXTURE_ATTRIBUTES];
#endif
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRMaterialShaderInfo);
#endif

struct PBRLightAttribs
{
    int   Type; // 1 - directional, 2 - point, 3 - spot
    float PosX; // Point or spot light
    float PosY; // position; unused for
    float PosZ; // directional light.
    
    float DirectionX; // Directional and spot lights.
    float DirectionY;
    float DirectionZ;    
    int   ShadowMapIndex; // -1 if light does not cast shadows

    float IntensityR;    
    float IntensityG;
    float IntensityB;
    float Range4;         // Point and spot light range to the power of 4
    
    float SpotAngleScale; // 1.0 / (cos(InnerConeAngle) - cos(OuterConeAngle))
    float SpotAngleOffset;// -cos(OuterConeAngle) * SpotAngleScale;
    float Padding0;
    float Padding1;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRLightAttribs);
#endif


struct PBRShadowMapInfo
{
    float4x4 WorldToLightProjSpace;
    
    float2 UVScale;
    float2 UVBias;
    
    float    ShadowMapSlice;
    float    Padding0;
    float    Padding1;
    float    Padding2;
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(PBRShadowMapInfo);
#endif

#endif // _PBR_STRUCTURES_FXH_
