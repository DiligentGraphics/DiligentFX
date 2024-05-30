#ifndef _COORDINATE_GRID_STRUCTURES_FXH_
#define _COORDINATE_GRID_STRUCTURES_FXH_

#include "ShaderDefinitions.fxh"

struct CoordinateGridAttribs
{
    float4 XAxisColor       DEFAULT_VALUE(float4(1, 0, 0, 1));
    float4 YAxisColor       DEFAULT_VALUE(float4(0, 1, 0, 1));
    float4 ZAxisColor       DEFAULT_VALUE(float4(0, 0, 1, 1));

    float XAxisWidth        DEFAULT_VALUE(3); // in pixels
    float YAxisWidth        DEFAULT_VALUE(3); // in pixels
    float ZAxisWidth        DEFAULT_VALUE(3); // in pixels
    float Padding0          DEFAULT_VALUE(0);

    float4 GridMajorColor   DEFAULT_VALUE(float4(0.4f, 0.4f, 0.4f, 1));
    float4 GridMinorColor   DEFAULT_VALUE(float4(0.1f, 0.1f, 0.1f, 1));
    float4 GridMinCellSize  DEFAULT_VALUE(float4(0.1f, 0.1f, 0.1f, 0.1f)); // YZ, XZ, XY   
    float4 GridSubdivision  DEFAULT_VALUE(float4(10, 10, 10, 0));          // YZ, XZ, XY
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(CoordinateGridAttribs);
#endif

#endif //_COORDINATE_GRID_STRUCTURES_FXH_
