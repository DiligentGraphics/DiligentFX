#ifndef _HN_POST_PROCESS_STRUCTURES_FXH_
#define _HN_POST_PROCESS_STRUCTURES_FXH_

struct PostProcessAttribs
{
    float4 SelectionOutlineColor;

    float NonselectionDesaturationFactor;
    float AverageLogLum;
    float ClearDepth;
    float SelectionOutlineWidth;

    ToneMappingAttribs ToneMapping;
};

#endif // _HN_POST_PROCESS_STRUCTURES_FXH_
