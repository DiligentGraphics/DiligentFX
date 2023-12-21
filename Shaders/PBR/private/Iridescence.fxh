#ifndef _IRIDESCENCE_FXH_
#define _IRIDESCENCE_FXH_

// Assume air interface for top
// Note: We don't handle the case fresnel0 == 1
float3 Fresnel0ToIor(float3 Fresnel0) 
{
    float3 sqrtF0 = sqrt(Fresnel0);
    return (float3(1.0, 1.0, 1.0) + sqrtF0) / (float3(1.0, 1.0, 1.0) - sqrtF0);
}

float  Sqr(float  v) { return v * v; }
float3 Sqr(float3 v) { return v * v; }

// Conversion F0/IOR
float3 IorToFresnel0(float3 TransmittedIor, float IncidentIor)
{
    return Sqr((TransmittedIor - float3(IncidentIor, IncidentIor, IncidentIor)) / 
               (TransmittedIor + float3(IncidentIor, IncidentIor, IncidentIor))
              );
}

// ior is a value between 1.0 and 3.0. 1.0 is air interface
float IorToFresnel0(float TransmittedIor, float IncidentIor) 
{
    return Sqr((TransmittedIor - IncidentIor) / (TransmittedIor + IncidentIor));
}

// Fresnel equations for dielectric/dielectric interfaces.
// Ref: https://belcour.github.io/blog/research/publication/2017/05/01/brdf-thin-film.html
// Evaluation XYZ sensitivity curves in Fourier space
float3 EvalSensitivity(float OPD, float3 shift)
{
    float phase = 2.0 * PI * OPD * 1.0e-9; // OPD in nm, phase in rad
    float3 val = float3(5.4856e-13, 4.4201e-13, 5.2481e-13);
    float3 pos = float3(1.6810e+06, 1.7953e+06, 2.2084e+06);
    float3 var = float3(4.3278e+09, 9.3046e+09, 6.6121e+09);

    float3 xyz = val * sqrt(2.0 * PI * var) * cos(pos * phase + shift) * exp(-Sqr(phase) * var);
    xyz.x += 9.7470e-14 * sqrt(2.0 * PI * 4.5282e+09) * cos(2.2399e+06 * phase + shift[0]) * exp(-4.5282e+09 * Sqr(phase));
    xyz /= 1.0685e-7;

    float3 srgb = float3(
         3.2404542 * xyz.x - 1.5371385 * xyz.y - 0.4985314 * xyz.z,
        -0.9692660 * xyz.x + 1.8760108 * xyz.y + 0.0415560 * xyz.z,
         0.0556434 * xyz.x - 0.2040259 * xyz.y + 1.0572252 * xyz.z
    );
    return srgb;
}

float3 EvalIridescence(float outsideIOR, float eta2, float cosTheta1, float thinFilmThickness, float3 baseF0)
{
    float3 I;

    // Force iridescenceIor -> outsideIOR when thinFilmThickness -> 0.0
    float iridescenceIor = lerp(outsideIOR, eta2, smoothstep(0.0, 0.03, thinFilmThickness));
    // Evaluate the cosTheta on the base layer (Snell law)
    float sinTheta2Sq = Sqr(outsideIOR / iridescenceIor) * (1.0 - Sqr(cosTheta1));

    // Handle TIR:
    float cosTheta2Sq = 1.0 - sinTheta2Sq;
    if (cosTheta2Sq < 0.0)
    {
        return float3(1.0, 1.0, 1.0);
    }

    float cosTheta2 = sqrt(cosTheta2Sq);

    // First interface
    float R0 = IorToFresnel0(iridescenceIor, outsideIOR);
    float R12 = SchlickReflection(cosTheta1, R0, 1.0);
    float R21 = R12;
    float T121 = 1.0 - R12;
    float phi12 = 0.0;
    if (iridescenceIor < outsideIOR) phi12 = PI;
    float phi21 = PI - phi12;

    // Second interface
    float3 baseIOR = Fresnel0ToIor(clamp(baseF0, 0.0, 0.9999)); // guard against 1.0
    float3 R1 = IorToFresnel0(baseIOR, iridescenceIor);
    float3 R23 = SchlickReflection(cosTheta2, R1, float3(1.0, 1.0, 1.0));
    float3 phi23 = float3(0.0, 0.0, 0.0);
    if (baseIOR[0] < iridescenceIor) phi23[0] = PI;
    if (baseIOR[1] < iridescenceIor) phi23[1] = PI;
    if (baseIOR[2] < iridescenceIor) phi23[2] = PI;

    // Phase shift
    float OPD = 2.0 * iridescenceIor * thinFilmThickness * cosTheta2;
    float3 phi = float3(phi21, phi21, phi21) + phi23;

    // Compound terms
    float3 R123 = clamp(R12 * R23, 1e-5, 0.9999);
    float3 r123 = sqrt(R123);
    float3 Rs = Sqr(T121) * R23 / (float3(1.0, 1.0, 1.0) - R123);

    // Reflectance term for m = 0 (DC term amplitude)
    float3 C0 = R12 + Rs;
    I = C0;

    // Reflectance term for m > 0 (pairs of diracs)
    float3 Cm = Rs - float3(T121, T121, T121);
    for (int m = 1; m <= 2; ++m)
    {
        Cm *= r123;
        float3 Sm = 2.0 * EvalSensitivity(float(m) * OPD, float(m) * phi);
        I += Cm * Sm;
    }

    // Since out of gamut colors might be produced, negative color values are clamped to 0.
    return max(I, float3(0.0, 0.0, 0.0));
}

#endif // _IRIDESCENCE_FXH_
