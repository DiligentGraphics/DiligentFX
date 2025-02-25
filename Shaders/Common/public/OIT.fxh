uint PackOITLayer(float Depth, float Transmittance)
{
    // Pack depth into the high 24 bits so that packed values
    // can be sorted by depth.
    uint D = uint(clamp(Depth, 0.0, 1.0) * 16777215.0);
    uint T = uint(clamp(Transmittance, 0.0, 1.0) * 255.0);
    return (D << 8u) | T;
}

uint GetOITLayerDepth(uint Layer)
{
    return Layer >> 8u;
}

float GetOITLayerTransmittance(uint Layer)
{
    return float(Layer & 0xFFu) / 255.0;
}

uint GetOITLayerDataOffset(uint2 PixelCoord, uint2 ScreenSize, uint NumLayers)
{
    return (PixelCoord.y * ScreenSize.x + PixelCoord.x) * NumLayers;
}
