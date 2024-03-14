#ifndef _BOUND_BOX_STRUCTURES_FXH_
#define _BOUND_BOX_STRUCTURES_FXH_

struct BoundBoxAttribs
{
    float4x4 Transform;
    float4   Color;
    
    float PatternLength;
    uint  PatternMask;
    float Padding0;
    float Padding1;
};

struct BoundBoxVSOutput
{
    float4 Pos              : SV_Position;
    float4 ClipPos          : CLIP_POS;
    float4 PrevClipPos      : PREV_CLIP_POS;
    float4 EdgeStartClipPos : EDGE_START_CLIP_POS;
};

#endif // _BOUND_BOX_STRUCTURES_FXH_
