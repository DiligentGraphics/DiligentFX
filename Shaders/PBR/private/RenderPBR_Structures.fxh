#ifndef _RENDER_PBR_STRUCTURES_FXH_
#define _RENDER_PBR_STRUCTURES_FXH_

// #include "BasicStructures.fxh"
// #include "PBR_Structures.fxh"


struct PBRFrameAttribs
{
    CameraAttribs               Camera;
    PBRRendererShaderParameters Renderer;
    PBRLightAttribs             Light;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRFrameAttribs);
#endif


struct PBRPrimitiveAttribs
{
    GLTFNodeShaderTransforms Transforms;
    PBRMaterialShaderInfo    Material;
};
#ifdef CHECK_STRUCT_ALIGNMENT
	CHECK_STRUCT_ALIGNMENT(PBRPrimitiveAttribs);
#endif


#endif // _RENDER_PBR_STRUCTURES_FXH_
