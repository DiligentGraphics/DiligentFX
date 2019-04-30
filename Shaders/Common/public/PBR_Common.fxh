#ifndef _PBR_COMMON_FXH_
#define _PBR_COMMON_FXH_

#ifndef PI
#   define  PI 3.141592653589793
#endif

// Lambertian diffuse
// see https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
float3 LambertianDiffuse(float3 DiffuseColor)
{
	return DiffuseColor / PI;
}

// The following equation models the Fresnel reflectance term of the spec equation (aka F())
// Implementation of fresnel from "An Inexpensive BRDF Model for Physically based Rendering" by Christophe Schlick
// (https://www.cs.virginia.edu/~jdl/bib/appearance/analytic%20models/schlick94b.pdf), Equation 15
float3 SchlickReflection(float VdotH, float3 reflectance0, float3 reflectance90)
{
	return reflectance0 + (reflectance90 - reflectance0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

// Smith Joint GGX
// see Eric Heitz. 2014. Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs. Journal of Computer Graphics Techniques, 3
// see Real-Time Rendering. Page 331 to 336.
// see https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/geometricshadowing(specularg)
float SmithGGXVisibilityCorrelated(float NdotL, float NdotV, float AlphaRoughness)
{
    float AlphaRoughnessSq = AlphaRoughness * AlphaRoughness;

    float GGXV = NdotL * sqrt(max(NdotV * NdotV * (1.0 - AlphaRoughnessSq) + AlphaRoughnessSq, 1e-7));
    float GGXL = NdotV * sqrt(max(NdotL * NdotL * (1.0 - AlphaRoughnessSq) + AlphaRoughnessSq, 1e-7));

    return 0.5 / (GGXV + GGXL);
}

// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games [1], Equation 3.
float MicrofacetDistribution(float NdotH, float AlphaRoughness)
{
    float AlphaRoughnessSq = AlphaRoughness * AlphaRoughness;
    float f = (NdotH * AlphaRoughnessSq - NdotH) * NdotH + 1.0;
    return AlphaRoughnessSq / (PI * f * f);
}


#endif // _PBR_COMMON_FXH_