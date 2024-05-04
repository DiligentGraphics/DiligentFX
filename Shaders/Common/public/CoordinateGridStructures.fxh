#ifndef _COORDINATE_GRID_STRUCTURES_FXH_
#define _COORDINATE_GRID_STRUCTURES_FXH_

#include "ShaderDefinitions.fxh"

struct CoordinateGridAttribs
{
    float4 XAxisColor       DEFAULT_VALUE(float4(1, 0, 0, 1));
    float4 YAxisColor       DEFAULT_VALUE(float4(0, 1, 0, 1));
    float4 ZAxisColor       DEFAULT_VALUE(float4(0, 0, 1, 1));

    float XAxisWidth        DEFAULT_VALUE(2); // in pixels
    float YAxisWidth        DEFAULT_VALUE(2); // in pixels
    float ZAxisWidth        DEFAULT_VALUE(2); // in pixels
    float Padding0          DEFAULT_VALUE(0);

    float4 GridMajorColor   DEFAULT_VALUE(float4(0.4f, 0.4f, 0.4f, 1));
    float4 GridMinorColor   DEFAULT_VALUE(float4(0.1f, 0.1f, 0.1f, 1));
    float4 GridScale        DEFAULT_VALUE(float4(1, 1, 1, 0));    // YZ, XZ, XY   
    float4 GridSubdivision  DEFAULT_VALUE(float4(10, 10, 10, 0)); // YZ, XZ, XY

    float  GridLineWidth    DEFAULT_VALUE(2); // in pixels
    float  GridMinCellWidth DEFAULT_VALUE(4); // in pixels
    float  GridMinCellSize  DEFAULT_VALUE(0.0001f);
    float  Padding1         DEFAULT_VALUE(0);
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(CoordinateGridAttribs);
#endif

#endif //_COORDINATE_GRID_STRUCTURES_FXH_
