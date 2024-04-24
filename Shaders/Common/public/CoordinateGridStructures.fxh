#ifndef _COORDINATE_GRID_STRUCTURES_FXH_
#define _COORDINATE_GRID_STRUCTURES_FXH_

#include "ShaderDefinitions.fxh"

struct CoordinateGridAttribs
{
    float4 XAxisColor      DEFAULT_VALUE(float4(1, 0, 0, 1));
    float4 YAxisColor      DEFAULT_VALUE(float4(0, 1, 0, 1));
    float4 ZAxisColor      DEFAULT_VALUE(float4(0, 0, 1, 1));

    // YZ, XZ, XY
    float4 GridScale       DEFAULT_VALUE(float4(1, 1, 1, 0));     
     
    // YZ, XZ, XY
    float4 GridSubdivision DEFAULT_VALUE(float4(5, 5, 5, 0));
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(CoordinateGridAttribs);
#endif

#endif //_COORDINATE_GRID_STRUCTURES_FXH_
