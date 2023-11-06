#ifndef _HN_CLOSEST_SELECTED_LOCATION_FXH_
#define _HN_CLOSEST_SELECTED_LOCATION_FXH_

struct ClosestSelectedLocationConstants
{
    float ClearDepth;
    float SampleRange;
    float Padding1;
    float Padding2;
};

#ifndef __cplusplus
float2 EncodeClosestSelectedLocation(float2 Location, bool IsValid)
{
    if (IsValid)
    {
        Location.y = Location.y * 0.5 + 0.5;
        return Location;
    }
    else
    {
        return float2(0.0, 0.0);
    }
}

void DecodeClosestSelectedLocation(in  float2 EncodedLocation, 
                                   out float2 Location,
                                   out bool   IsValid)
{
    if (EncodedLocation.y <= 0.25)
    {
        IsValid  = false;
        Location = float2(0.0, 0.0);
    }
    else
    {
        Location   = EncodedLocation;
        Location.y = Location.y * 2.0 - 1.0;
        IsValid    = true;
    }
}
#endif

#endif // _HN_CLOSEST_SELECTED_LOCATION_FXH_
