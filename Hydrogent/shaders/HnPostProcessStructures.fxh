#ifndef _HN_POST_PROCESS_STRUCTURES_FXH_
#define _HN_POST_PROCESS_STRUCTURES_FXH_

struct PostProcessAttribs
{
    float4 SelectionOutlineColor;

    float NonselectionDesaturationFactor;
    float AverageLogLum;
    float ClearDepth;
    float SelectionOutlineWidth;
    
    float SSRScale;
    float Padding0;
    float Padding1;
    float Padding2;

    ToneMappingAttribs ToneMapping;
};

#endif // _HN_POST_PROCESS_STRUCTURES_FXH_
