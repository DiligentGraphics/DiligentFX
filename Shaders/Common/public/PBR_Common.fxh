#ifndef _PBR_COMMON_FXH_
#define _PBR_COMMON_FXH_

#ifndef FLOAT
#   define FLOAT float
#endif

#ifndef FLOAT2
#   define FLOAT2 float2
#endif

#ifndef FLOAT3
#   define FLOAT3 float3
#endif

#ifndef FLOAT4
#   define FLOAT4 float4
#endif

#ifndef EPSILON
#   define EPSILON 1e-7
#endif

#define _0f FLOAT(0.0)
#define _1f FLOAT(1.0)
#define _2f FLOAT(2.0)

#ifndef PI
#    define PI FLOAT(3.141592653589793)
#endif

// Lambertian diffuse
// see https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
FLOAT3 LambertianDiffuse(FLOAT3 DiffuseColor)
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
#define SCHLICK_REFLECTION(VdotH, Reflectance0, Reflectance90) ((Reflectance0) + ((Reflectance90) - (Reflectance0)) * pow(clamp(_1f - (VdotH), _0f, _1f), FLOAT(5.0)))
FLOAT SchlickReflection(FLOAT VdotH, FLOAT Reflectance0, FLOAT Reflectance90)
{
    return SCHLICK_REFLECTION(VdotH, Reflectance0, Reflectance90);
}
FLOAT3 SchlickReflection(FLOAT VdotH, FLOAT3 Reflectance0, FLOAT3 Reflectance90)
{
    return SCHLICK_REFLECTION(VdotH, Reflectance0, Reflectance90);
}

// Visibility = G2(v,l,a) / (4 * (n,v) * (n,l))
// see https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf
FLOAT SmithGGXVisibilityCorrelated(FLOAT NdotL, FLOAT NdotV, FLOAT AlphaRoughness)
{
    // G1 (masking) is % microfacets visible in 1 direction
    // G2 (shadow-masking) is % microfacets visible in 2 directions
    // If uncorrelated:
    //    G2(NdotL, NdotV) = G1(NdotL) * G1(NdotV)
    //    Less realistic as higher points are more likely visible to both L and V
    //
    // https://ubm-twvideo01.s3.amazonaws.com/o1/vault/gdc2017/Presentations/Hammon_Earl_PBR_Diffuse_Lighting.pdf

    FLOAT a2 = AlphaRoughness * AlphaRoughness;

    FLOAT GGXV = NdotL * sqrt(max(NdotV * NdotV * (_1f - a2) + a2, EPSILON));
    FLOAT GGXL = NdotV * sqrt(max(NdotL * NdotL * (_1f - a2) + a2, EPSILON));

    return FLOAT(0.5) / (GGXV + GGXL);
}

// Smith GGX shadow-masking function G2(v,l,a)
FLOAT SmithGGXShadowMasking(FLOAT NdotL, FLOAT NdotV, FLOAT AlphaRoughness)
{
    return FLOAT(4.0) * NdotL * NdotV * SmithGGXVisibilityCorrelated(NdotL, NdotV, AlphaRoughness);
}

// Smith GGX masking function G1
// [1] "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz - eq. (2)
// https://jcgt.org/published/0007/04/01/
FLOAT SmithGGXMasking(FLOAT NdotV, FLOAT AlphaRoughness)
{
    FLOAT a2 = AlphaRoughness * AlphaRoughness;

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

    FLOAT Denom = NdotV + sqrt(a2 + (_1f - a2) * NdotV * NdotV);
    return _2f * max(NdotV, _0f) / max(Denom, EPSILON);
}


// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games, Equation 3.
FLOAT NormalDistribution_GGX(FLOAT NdotH, FLOAT AlphaRoughness)
{
    // "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz - eq. (1)
    // https://jcgt.org/published/0007/04/01/

    // Make sure we reasonably handle AlphaRoughness == 0
    // (which corresponds to delta function)
    AlphaRoughness = max(AlphaRoughness, FLOAT(1e-3));

    FLOAT a2  = AlphaRoughness * AlphaRoughness;
    FLOAT nh2 = NdotH * NdotH;
    FLOAT f   = nh2 * a2 + (_1f - nh2);
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
FLOAT3 SmithGGXSampleVisibleNormal(FLOAT3 View, // View direction in tangent space
                                   FLOAT  ax,   // X roughness
                                   FLOAT  ay,   // Y roughness
                                   FLOAT  u1,   // Uniform random variable in [0, 1]
                                   FLOAT  u2    // Uniform random variable in [0, 1]
)
{
    // Stretch the view vector so we are sampling as if roughness==1
    FLOAT3 V = normalize(View * FLOAT3(ax, ay, _1f));

#if 1
    // Technique from [1]
    // Note: while [2] claims to provide a better parameterization, it produces
    //       subjectively noisier images, so we will stick with method from [1].

    // Build an orthonormal basis with V, T1, and T2
    FLOAT3 T1 = (V.z < FLOAT(0.999)) ? normalize(cross(V, FLOAT3(0.0, 0.0, 1.0))) : FLOAT3(1.0, 0.0, 0.0);
    FLOAT3 T2 = cross(T1, V);

    // Choose a point on a disk with each half of the disk weighted
    // proportionally to its projection onto direction V
    FLOAT a = _1f / (_1f + V.z);
    FLOAT r = sqrt(u1);
    FLOAT phi = (u2 < a) ? (u2 / a) * PI : PI + (u2 - a) / (_1f - a) * PI;
    FLOAT p1 = r * cos(phi);
    FLOAT p2 = r * sin(phi) * ((u2 < a) ? _1f : V.z);
#else
    // Technique from [2]
    // Note: [1] uses earlier parameterization that cannot be used with view directions located in the lower
    //       hemisphere (View.z < 0). Though this is not a problem for classic microfacet BSDF models,
    //       [2] provides a better approximation that is not subject to this limitation.

    // Build orthonormal basis (with special case if cross product is zero) (Section 4.1)
    FLOAT lensq = dot(V.xy, V.xy);
    FLOAT3 T1 = lensq > _0f ? FLOAT3(-V.y, V.x, _0f) / sqrt(lensq) : FLOAT3(1.0, 0.0, 0.0);
    FLOAT3 T2 = cross(V, T1);

    // Sample the projected area (Section 4.2)
    FLOAT r   = sqrt(u1);
    FLOAT phi = _2f * PI * u2;
    FLOAT p1 = r * cos(phi);
    FLOAT p2 = r * sin(phi);
    FLOAT s  = FLOAT(0.5) * (_1f + V.z);
    p2 = (_1f - s) * sqrt(_1f - p1 * p1) + s * p2;
#endif

    // Calculate the normal in the stretched tangent space
    FLOAT3 N = p1 * T1 + p2 * T2 + sqrt(max(_0f, _1f - p1 * p1 - p2 * p2)) * V;

    // Transform the normal back to the ellipsoid configuration
    return normalize(FLOAT3(ax * N.x, ay * N.y, max(_0f, N.z)));
}

// Returns the probability of sampling direction L for the view direction V and normal N
// using the visible normals distribution.
// [1] "Sampling the GGX Distribution of Visible Normals" (2018) by Eric Heitz
// https://jcgt.org/published/0007/04/01/
FLOAT SmithGGXSampleDirectionPDF(FLOAT3 V, FLOAT3 N, FLOAT3 L, FLOAT AlphaRoughness)
{
    // Micronormal is the halfway vector
    FLOAT3 H = normalize(V + L);

    FLOAT NdotH = dot(H, N);
    FLOAT NdotV = dot(N, V);
    FLOAT NdotL = dot(N, L);
    //FLOAT VdotH = dot(V, H);
    if (NdotH <= _0f || NdotV <= _0f || NdotL <= _0f)
        return _0f;

    // Note that [1] uses notation N for the micronormal, but in our case N is the macronormal,
    // while micronormal is H (aka the halfway vector).
    FLOAT NDF = NormalDistribution_GGX(NdotH, AlphaRoughness); // (1) - D(N)
    FLOAT G1  = SmithGGXMasking(NdotV, AlphaRoughness);        // (2) - G1(V)

    FLOAT VNDF = G1 /* * VdotH */ * NDF / NdotV; // (3) - Dv(N)
    return  VNDF / (FLOAT(4.0) /* * VdotH */); // (17) - VdotH cancels out
}

struct AngularInfo
{
    FLOAT NdotL; // cos angle between normal and light direction
    FLOAT NdotV; // cos angle between normal and view direction
    FLOAT NdotH; // cos angle between normal and half vector
    FLOAT LdotH; // cos angle between light direction and half vector
    FLOAT VdotH; // cos angle between view direction and half vector
};

AngularInfo GetAngularInfo(FLOAT3 PointToLight, FLOAT3 Normal, FLOAT3 View)
{
    FLOAT3 n = normalize(Normal);       // Outward direction of surface point
    FLOAT3 v = normalize(View);         // Direction from surface point to camera
    FLOAT3 l = normalize(PointToLight); // Direction from surface point to light
    FLOAT3 h = normalize(l + v);        // Direction of the vector between l and v

    AngularInfo info;
    info.NdotL = clamp(dot(n, l), _0f, _1f);
    info.NdotV = clamp(dot(n, v), _0f, _1f);
    info.NdotH = clamp(dot(n, h), _0f, _1f);
    info.LdotH = clamp(dot(l, h), _0f, _1f);
    info.VdotH = clamp(dot(v, h), _0f, _1f);

    return info;
}

struct SurfaceReflectanceInfo
{
    FLOAT  PerceptualRoughness;
    FLOAT3 Reflectance0;
    FLOAT3 Reflectance90;
    FLOAT3 DiffuseColor;
};

// BRDF with Lambertian diffuse term and Smith-GGX specular term.
void SmithGGX_BRDF(in FLOAT3                 PointToLight,
                   in FLOAT3                 Normal,
                   in FLOAT3                 View,
                   in SurfaceReflectanceInfo SrfInfo,
                   out FLOAT3                DiffuseContrib,
                   out FLOAT3                SpecContrib,
                   out FLOAT                 NdotL)
{
    AngularInfo angularInfo = GetAngularInfo(PointToLight, Normal, View);

    DiffuseContrib = FLOAT3(0.0, 0.0, 0.0);
    SpecContrib    = FLOAT3(0.0, 0.0, 0.0);
    NdotL          = angularInfo.NdotL;
    // If one of the dot products is larger than zero, no division by zero can happen. Avoids black borders.
    if (angularInfo.NdotL > _0f || angularInfo.NdotV > _0f)
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
        FLOAT  AlphaRoughness = SrfInfo.PerceptualRoughness * SrfInfo.PerceptualRoughness;
        FLOAT  D   = NormalDistribution_GGX(angularInfo.NdotH, AlphaRoughness);
        FLOAT  Vis = SmithGGXVisibilityCorrelated(angularInfo.NdotL, angularInfo.NdotV, AlphaRoughness);
        FLOAT3 F   = SchlickReflection(angularInfo.VdotH, SrfInfo.Reflectance0, SrfInfo.Reflectance90);

        DiffuseContrib = (_1f - F) * LambertianDiffuse(SrfInfo.DiffuseColor);
        SpecContrib    = F * Vis * D;
    }
}

#endif // _PBR_COMMON_FXH_
