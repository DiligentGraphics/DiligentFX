#ifndef _HN_POST_PROCESS_STRUCTURES_FXH_
#define _HN_POST_PROCESS_STRUCTURES_FXH_

struct PostProcessAttribs
{
    float4 SelectionOutlineColor;
    float4 OccludedSelectionOutlineColor;

    float NonselectionDesaturationFactor;
    float AverageLogLum;
    float ClearDepth;
    float SelectionOutlineWidth;
    
    float SSRScale;
    float SSAOScale;
    float Padding0;
    float Padding1;

    ToneMappingAttribs    ToneMapping;
    CoordinateGridAttribs CoordinateGrid;
};

#endif // _HN_POST_PROCESS_STRUCTURES_FXH_
