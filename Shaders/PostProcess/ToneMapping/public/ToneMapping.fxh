#include "ToneMappingStructures.fxh"
#include "SRGBUtilities.fxh"

#ifndef RGB_TO_LUMINANCE
#   define RGB_TO_LUMINANCE float3(0.212671, 0.715160, 0.072169)
#endif

float3 Uncharted2Tonemap(float3 x)
{
    // http://www.gdcvault.com/play/1012459/Uncharted_2__HDR_Lighting
    // http://filmicgames.com/archives/75 - the coefficients are from here
    float A = 0.15; // Shoulder Strength
    float B = 0.50; // Linear Strength
    float C = 0.10; // Linear Angle
    float D = 0.20; // Toe Strength
    float E = 0.02; // Toe Numerator
    float F = 0.30; // Toe Denominator
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F; // E/F = Toe Angle
}

float3 AgXDefaultContrastApprox(float3 x) 
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
  
    return + 15.5     * x4 * x2
           - 40.14    * x4 * x
           + 31.96    * x4
           - 6.868    * x2 * x
           + 0.4298   * x2
           + 0.1191   * x
           - 0.00232;
}

float3 AgX(float3 Color) 
{
    float3x3 AgXTransform = MatrixFromRows(
        float3(0.842479062253094,  0.0784335999999992, 0.0792237451477643),
        float3(0.0423282422610123, 0.878468636469772,  0.0791661274605434),
        float3(0.0423756549057051, 0.0784336,          0.879142973793104));
    // LOG2_MIN      = -10.0
    // LOG2_MAX      =  +6.5
    // MIDDLE_GRAY   =  0.18
    const float MinEv = -12.47393f; // log2(pow( 2, LOG2_MIN) * MIDDLE_GRAY)
    const float MaxEv = 4.026069f;  // log2(pow( 2, LOG2_MAX) * MIDDLE_GRAY)

    // Input transform (inset)
    Color = mul(AgXTransform, Color);
  
    // Log2 space encoding
    Color = clamp(log2(Color), MinEv, MaxEv);
    Color = (Color - MinEv) / (MaxEv - MinEv);
  
    // Apply sigmoid function approximation
    return AgXDefaultContrastApprox(Color);
}

float3 AgXEotf(float3 Color) 
{
    float3x3 InvAgXTransform = MatrixFromRows(
        float3(+1.19687900512017,   -0.0980208811401368, -0.0990297440797205),
        float3(-0.0528968517574562, +1.15190312990417,   -0.0989611768448433),
        float3(-0.0529716355144438, -0.0980434501171241, +1.15107367264116));
    
    // Inverse input transform (outset)
    Color = mul(InvAgXTransform, Color);
  
    // sRGB IEC 61966-2-1 2.2 Exponent Reference EOTF Display
    // NOTE: We're linearizing the output here. Comment/adjust when
    // *not* using a sRGB render target
    return SRGBToLinear(Color);
}

float3 AgXPunchyLook(float3 Color, float Saturation) 
{
    float Lum = dot(Color, RGB_TO_LUMINANCE);
  
    float3 Offset = float3(0.0, 0.0, 0.0);
    float3 Slope  = float3(1.0, 1.0, 1.0);
    float3 Power  = float3(1.35, 1.35, 1.35);
  
    // ASC CDL
    Color = pow(Color * Slope + Offset, Power);
    return Lum + Saturation * (Color - Lum);
}

float3 ToneMap(in float3 f3Color, ToneMappingAttribs Attribs, float fAveLogLum)
{
    //const float middleGray = 1.03 - 2 / (2 + log10(fAveLogLum+1));
    float middleGray = Attribs.fMiddleGray;
    // Compute scale factor such that average luminance maps to middle gray
    float fLumScale = middleGray / fAveLogLum;

    f3Color = max(f3Color, float3(0.0, 0.0, 0.0));
    float fInitialPixelLum = max(dot(RGB_TO_LUMINANCE, f3Color), 1e-10);
    float fScaledPixelLum = fInitialPixelLum * fLumScale;
    float3 f3ScaledColor = f3Color * fLumScale;

    float whitePoint = Attribs.fWhitePoint;

#   if TONE_MAPPING_MODE == TONE_MAPPING_MODE_EXP
    {
        float  fToneMappedLum = 1.0 - exp( -fScaledPixelLum );
        return fToneMappedLum * pow(f3Color / fInitialPixelLum, Attribs.fLuminanceSaturation * float3(1.0, 1.0, 1.0));
    }
#   elif TONE_MAPPING_MODE == TONE_MAPPING_MODE_REINHARD || TONE_MAPPING_MODE == TONE_MAPPING_MODE_REINHARD_MOD
    {
        // http://www.cs.utah.edu/~reinhard/cdrom/tonemap.pdf
        // http://imdoingitwrong.wordpress.com/2010/08/19/why-reinhard-desaturates-my-blacks-3/
        // http://content.gpwiki.org/index.php/D3DBook:High-Dynamic_Range_Rendering

        float L_xy = fScaledPixelLum;
        float fToneMappedLum;
#       if TONE_MAPPING_MODE == TONE_MAPPING_MODE_REINHARD
        {
            fToneMappedLum = L_xy / (1.0 + L_xy);
        }
#       else
        {
	        fToneMappedLum = L_xy * (1.0 + L_xy / (whitePoint*whitePoint)) / (1.0 + L_xy);
        }
#       endif
	    return fToneMappedLum * pow(f3Color / fInitialPixelLum, Attribs.fLuminanceSaturation * float3(1.0, 1.0, 1.0));
    }
#elif TONE_MAPPING_MODE == TONE_MAPPING_MODE_UNCHARTED2
    {
        // http://filmicgames.com/archives/75
        float ExposureBias = 2.0;
        float3 curr = Uncharted2Tonemap(ExposureBias*f3ScaledColor);
        float3 whiteScale = float3(1.0, 1.0, 1.0) / Uncharted2Tonemap(float3(whitePoint, whitePoint, whitePoint));
        return curr*whiteScale;
    }
#elif TONE_MAPPING_MODE == TONE_MAPPING_FILMIC_ALU
    {
        // http://www.gdcvault.com/play/1012459/Uncharted_2__HDR_Lighting
        float3 f3ToneMappedColor = max(f3ScaledColor - float3(0.004, 0.004, 0.004), float3(0.0, 0.0, 0.0));
        f3ToneMappedColor = (f3ToneMappedColor * (6.2 * f3ToneMappedColor + float3(0.5, 0.5, 0.5))) / 
                            (f3ToneMappedColor * (6.2 * f3ToneMappedColor + float3(1.7, 1.7, 1.7))+ float3(0.06, 0.06, 0.06));
        // result has 1/2.2 gamma baked in
        return pow(f3ToneMappedColor, float3(2.2, 2.2, 2.2));
    }
#elif TONE_MAPPING_MODE == TONE_MAPPING_LOGARITHMIC
    {
        // http://www.mpi-inf.mpg.de/resources/tmo/logmap/logmap.pdf
        float fToneMappedLum = log10(1.0 + fScaledPixelLum) / log10(1.0 + whitePoint);
	    return fToneMappedLum * pow(f3Color / fInitialPixelLum, Attribs.fLuminanceSaturation * float3(1.0, 1.0, 1.0));
    }
#elif TONE_MAPPING_MODE == TONE_MAPPING_ADAPTIVE_LOG
    {
        // http://www.mpi-inf.mpg.de/resources/tmo/logmap/logmap.pdf
        float Bias = 0.85;
        float fToneMappedLum = 
            1.0 / log10(1.0 + whitePoint) *
            log(1.0 + fScaledPixelLum) / log( 2.0 + 8.0 * pow( fScaledPixelLum / whitePoint, log(Bias) / log(0.5)) );
	    return fToneMappedLum * pow(f3Color / fInitialPixelLum, Attribs.fLuminanceSaturation * float3(1.0, 1.0, 1.0));
    }
#elif TONE_MAPPING_MODE == TONE_MAPPING_AGX
    {
        // https://iolite-engine.com/blog_posts/minimal_agx_implementation
        float3 f3ToneMappedColor  = AgX(f3ScaledColor);
        f3ToneMappedColor = AgXEotf(f3ToneMappedColor);
        return f3ToneMappedColor;
    }
#elif TONE_MAPPING_MODE == TONE_MAPPING_AGX_PUNCHY
    {
        // https://iolite-engine.com/blog_posts/minimal_agx_implementation
        // The constant was chosen experimentally to preserve the brightness when changing the tone mapper
        float3 f3ToneMappedColor = AgX(3.0 * f3ScaledColor);
        f3ToneMappedColor = AgXPunchyLook(f3ToneMappedColor, 1.4);
        f3ToneMappedColor = AgXEotf(f3ToneMappedColor);
        return f3ToneMappedColor;
    }
#else
    {
        return f3Color;
    }
#endif
}
