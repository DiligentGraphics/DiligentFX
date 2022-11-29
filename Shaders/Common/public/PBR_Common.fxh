#ifndef _PBR_COMMON_FXH_
#define _PBR_COMMON_FXH_

#ifndef PI
#    define PI 3.141592653589793
#endif

// Lambertian diffuse
// see https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
float3 LambertianDiffuse(float3 DiffuseColor)
{
    return DiffuseColor / PI;
}

// The following equation models the Fresnel reflectance term of the spec equation (aka F())
// Implementation of fresnel term from "An Inexpensive BRDF Model for Physically based Rendering" by Christophe Schlick
// https://en.wikipedia.org/wiki/Schlick%27s_approximation
//
//      Rf(Theta) = Rf(0) + (1 - Rf(0)) * (1 - cos(Theta))^5
//
//
//           '.       |       .'
//             '.     |Theta.'
//               '.   |   .'
//                 '. | .'
//        ___________'.'___________
//                   '|
//                  ' |
//                 '  |
//                '   |
//               ' Phi|
//
// Note that precise relfectance term is given by the following expression:
//
//      Rf(Theta) = 0.5 * (sin^2(Theta - Phi) / sin^2(Theta + Phi) + tan^2(Theta - Phi) / tan^2(Theta + Phi))
//
#define SCHLICK_REFLECTION(VdotH, Reflectance0, Reflectance90) ((Reflectance0) + ((Reflectance90) - (Reflectance0)) * pow(clamp(1.0 - (VdotH), 0.0, 1.0), 5.0))
float SchlickReflection(float VdotH, float Reflectance0, float Reflectance90)
{
    return SCHLICK_REFLECTION(VdotH, Reflectance0, Reflectance90);
}
float3 SchlickReflection(float VdotH, float3 Reflectance0, float3 Reflectance90)
{
    return SCHLICK_REFLECTION(VdotH, Reflectance0, Reflectance90);
}

// Visibility = G2(v,l,a) / (4 * (n,v) * (n,l))
// see https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf
float SmithGGXVisibilityCorrelated(float NdotL, float NdotV, float AlphaRoughness)
{
    // G1 (masking) is % microfacets visible in 1 direction
    // G2 (shadow-masking) is % microfacets visible in 2 directions
    // If uncorrelated:
    //    G2(NdotL, NdotV) = G1(NdotL) * G1(NdotV)
    //    Less realistic as higher points are more likely visible to both L and V
    //
    // https://ubm-twvideo01.s3.amazonaws.com/o1/vault/gdc2017/Presentations/Hammon_Earl_PBR_Diffuse_Lighting.pdf

    float a2 = AlphaRoughness * AlphaRoughness;

    float GGXV = NdotL * sqrt(max(NdotV * NdotV * (1.0 - a2) + a2, 1e-7));
    float GGXL = NdotV * sqrt(max(NdotL * NdotL * (1.0 - a2) + a2, 1e-7));

    return 0.5 / (GGXV + GGXL);
}

// Smith GGX shadow-masking function G2(v,l,a)
float SmithGGXShadowMasking(float NdotL, float NdotV, float AlphaRoughness)
{
    return 4.0 * NdotL * NdotV * SmithGGXVisibilityCorrelated(NdotL, NdotV, AlphaRoughness);
}

// Smith GGX masking function G1
// [1] "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz - eq. (2)
// https://jcgt.org/published/0007/04/01/
float SmithGGXMasking(float NdotV, float AlphaRoughness)
{
    float a2 = AlphaRoughness * AlphaRoughness;

    // In [1], eq. (2) is defined for the tangent-space view direction V:
    //
    //                                        1
    //      G1(V) = -----------------------------------------------------------
    //                                    {      (ax*V.x)^2 + (ay*V.y)^2)  }
    //               1 + 0.5 * ( -1 + sqrt{ 1 + -------------------------- } )
    //                                    {              V.z^2             }
    //
    // Note that [1] uses notation N for the micronormal, but in our case N is the macronormal,
    // while micronormal is H (aka the halfway vector).
    //
    // After multiplying both nominator and denominator by 2*V.z and given that in our
    // case ax = ay = a, we get:
    //
    //                                2 * V.z                                        2 * V.z
    //      G1(V) = ------------------------------------------- =  ----------------------------------------
    //               V.z + sqrt{ V.z^2 + a2 * (V.x^2 + V.y^2) }     V.z + sqrt{ V.z^2 + a2 * (1 - V.z^2) }
    //
    // Since V.z = NdotV, we finally get:

    float Denom = NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
    return 2.0 * max(NdotV, 0.0) / max(Denom, 1e-6);
}


// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games, Equation 3.
float NormalDistribution_GGX(float NdotH, float AlphaRoughness)
{
    // "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz - eq. (1)
    // https://jcgt.org/published/0007/04/01/

    // Make sure we reasonably handle AlphaRoughness == 0
    // (which corresponds to delta function)
    AlphaRoughness = max(AlphaRoughness, 1e-3);

    float a2  = AlphaRoughness * AlphaRoughness;
    float nh2 = NdotH * NdotH;
    float f   = nh2 * a2 + (1.0 - nh2);
    return a2 / (PI * f * f);
}


// Samples a normal from Visible Normal Distribution as described in
// [1] "A Simpler and Exact Sampling Routine for the GGX Distribution of Visible Normals" (2017) by Eric Heitz
//     https://hal.archives-ouvertes.fr/hal-01509746/document
// [2] "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz
//     https://jcgt.org/published/0007/04/01/
//
// Notes:
//      - View direction must be pointing away from the surface.
//      - Returned normal is in tangent space with Z up.
//      - Returned normal should be used to reflect the view direction and obtain
//        the sampling direction.
float3 SmithGGXSampleVisibleNormal(float3 View, // View direction in tangent space
                                   float  ax,   // X roughness
                                   float  ay,   // Y roughness
                                   float  u1,   // Uniform random variable in [0, 1]
                                   float  u2    // Uniform random variable in [0, 1]
)
{
    // Stretch the view vector so we are sampling as if roughness==1
    float3 V = normalize(View * float3(ax, ay, 1.0));

#if 1
    // Technique from [1]
    // Note: while [2] claims to provide a better parameterization, it produces
    //       subjectively noisier images, so we will stick with method from [1].

    // Build an orthonormal basis with V, T1, and T2
    float3 T1 = (V.z < 0.999) ? normalize(cross(V, float3(0.0, 0.0, 1.0))) : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(T1, V);

    // Choose a point on a disk with each half of the disk weighted
    // proportionally to its projection onto direction V
    float a = 1.0 / (1.0 + V.z);
    float r = sqrt(u1);
    float phi = (u2 < a) ? (u2 / a) * PI : PI + (u2 - a) / (1.0 - a) * PI;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi) * ((u2 < a) ? 1.0 : V.z);
#else
    // Technique from [2]
    // Note: [1] uses earlier parameterization that cannot be used with view directions located in the lower
    //       hemisphere (View.z < 0). Though this is not a problem for classic microfacet BSDF models,
    //       [2] provides a better approximation that is not subject to this limitation.

    // Build orthonormal basis (with special case if cross product is zero) (Section 4.1)
    float lensq = dot(V.xy, V.xy);
    float3 T1 = lensq > 0.0 ? float3(-V.y, V.x, 0.0) / sqrt(lensq) : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(V, T1);

    // Sample the projected area (Section 4.2)
    float r   = sqrt(u1);
    float phi = 2.0 * PI * u2;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi);
    float s  = 0.5 * (1.0 + V.z);
    p2 = (1.0 - s) * sqrt(1.0 - p1 * p1) + s * p2;
#endif

    // Calculate the normal in the stretched tangent space
    float3 N = p1 * T1 + p2 * T2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * V;

    // Transform the normal back to the ellipsoid configuration
    return normalize(float3(ax * N.x, ay * N.y, max(0.0, N.z)));
}

// Returns the probability of sampling direction L for the view direction V and normal N
// using the visible normals distribution.
// [1] "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz
// https://jcgt.org/published/0007/04/01/
float SmithGGXSampleDirectionPDF(float3 V, float3 N, float3 L, float AlphaRoughness)
{
    // Micronormal is the halfway vector
    float3 H = normalize(V + L);

    float NdotH = dot(H, N);
    float NdotV = dot(N, V);
    float NdotL = dot(N, L);
    //float VdotH = dot(V, H);
    if (NdotH <= 0.0 || NdotV <= 0.0 || NdotL <= 0.0)
        return 0.0;

    // Note that [1] uses notation N for the micronormal, but in our case N is the macronormal,
    // while micronormal is H (aka the halfway vector).
    float NDF = NormalDistribution_GGX(NdotH, AlphaRoughness); // (1) - D(N)
    float G1  = SmithGGXMasking(NdotV, AlphaRoughness);        // (2) - G1(V)

    float VNDF = G1 /* * VdotH */ * NDF / NdotV; // (3) - Dv(N)
    return  VNDF / (4.0 /* * VdotH */); // (17) - VdotH cancels out
}

struct AngularInfo
{
    float NdotL; // cos angle between normal and light direction
    float NdotV; // cos angle between normal and view direction
    float NdotH; // cos angle between normal and half vector
    float LdotH; // cos angle between light direction and half vector
    float VdotH; // cos angle between view direction and half vector
};

AngularInfo GetAngularInfo(float3 PointToLight, float3 Normal, float3 View)
{
    float3 n = normalize(Normal);       // Outward direction of surface point
    float3 v = normalize(View);         // Direction from surface point to camera
    float3 l = normalize(PointToLight); // Direction from surface point to light
    float3 h = normalize(l + v);        // Direction of the vector between l and v

    AngularInfo info;
    info.NdotL = clamp(dot(n, l), 0.0, 1.0);
    info.NdotV = clamp(dot(n, v), 0.0, 1.0);
    info.NdotH = clamp(dot(n, h), 0.0, 1.0);
    info.LdotH = clamp(dot(l, h), 0.0, 1.0);
    info.VdotH = clamp(dot(v, h), 0.0, 1.0);

    return info;
}

struct SurfaceReflectanceInfo
{
    float  PerceptualRoughness;
    float3 Reflectance0;
    float3 Reflectance90;
    float3 DiffuseColor;
};

// BRDF with Lambertian diffuse term and Smith-GGX specular term.
void SmithGGX_BRDF(in float3                 PointToLight,
                   in float3                 Normal,
                   in float3                 View,
                   in SurfaceReflectanceInfo SrfInfo,
                   out float3                DiffuseContrib,
                   out float3                SpecContrib,
                   out float                 NdotL)
{
    AngularInfo angularInfo = GetAngularInfo(PointToLight, Normal, View);

    DiffuseContrib = float3(0.0, 0.0, 0.0);
    SpecContrib    = float3(0.0, 0.0, 0.0);
    NdotL          = angularInfo.NdotL;
    // If one of the dot products is larger than zero, no division by zero can happen. Avoids black borders.
    if (angularInfo.NdotL > 0.0 || angularInfo.NdotV > 0.0)
    {
        //           D(h,a) * G2(v,l,a) * F(v,h,f0)
        // f(v,l) = -------------------------------- = D(h,a) * Vis(v,l,a) * F(v,h,f0)
        //               4 * (n,v) * (n,l)
        // where
        //
        // Vis(v,l,a) = G2(v,l,a) / (4 * (n,v) * (n,l))

        // It is not a mistake that AlphaRoughness = PerceptualRoughness ^ 2 and that
        // SmithGGXVisibilityCorrelated and NormalDistribution_GGX then use a2 = AlphaRoughness ^ 2.
        // See eq. 3 in https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
        float  AlphaRoughness = SrfInfo.PerceptualRoughness * SrfInfo.PerceptualRoughness;
        float  D   = NormalDistribution_GGX(angularInfo.NdotH, AlphaRoughness);
        float  Vis = SmithGGXVisibilityCorrelated(angularInfo.NdotL, angularInfo.NdotV, AlphaRoughness);
        float3 F   = SchlickReflection(angularInfo.VdotH, SrfInfo.Reflectance0, SrfInfo.Reflectance90);

        DiffuseContrib = (1.0 - F) * LambertianDiffuse(SrfInfo.DiffuseColor);
        SpecContrib    = F * Vis * D;
    }
}

#endif // _PBR_COMMON_FXH_
