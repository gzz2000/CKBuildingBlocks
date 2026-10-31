#ifndef TEXTUREPROCESSING_H
#define TEXTUREPROCESSING_H

//////////////////////////////////////
// Texture Processing functions

// Convolutions Matrix
// Gaussian Blur Matrix
extern int CMAntialias[3][3];
// Gaussian Blur Matrix
extern int CMGaussianBlur[3][3];
// Gaussian Blur Matrix
extern int CMUniformBlur[3][3];
// Gaussian Blur Matrix
extern int CMDirectionnal[3][3];
// Emboss Matrix
extern int CMEmboss[3][3];
// Passe haut Matrix
extern int CMHighPass[3][3];
// Passe bas Matrix
extern int CMLowPass[3][3];

// Decal  Matrices
extern int CMDecalLeft[3][3];
extern int CMDecalRight[3][3];
extern int CMDecalTop[3][3];
extern int CMDecalBottom[3][3];
extern int CMDecalNone[3][3];
extern int CMDecalBR[3][3];
extern int CMDecalBL[3][3];
extern int CMDecalTL[3][3];
extern int CMDecalTR[3][3];

void ConvolveTexture3x3(CKTexture *texture, int kernel[3][3], int scale);

void ConvolvePaddedPixels3x3(const CKDWORD *paddedSource,
                             CKDWORD *destination,
                             int width,
                             int height,
                             const short *kernelData);
void ConvolveTemporalPixels3x3(const CKDWORD *currentFrame,
                               CKDWORD *previousFrame,
                               int width,
                               int height,
                               const short *kernelData,
                               int damping);
void ApplyWaterRipple(CKDWORD *currentFrame,
                      CKDWORD *previousFrame,
                      int width,
                      int height,
                      CKDWORD borderColor,
                      int damping);
void SwapPixelBuffers(CKDWORD *first, CKDWORD *second, CKDWORD pixelCount);
void BlendPixelBuffers(CKDWORD *output,
                       const CKDWORD *first,
                       const CKDWORD *second,
                       int pixelCount,
                       float blendFactor);
void BlendPixelBuffersScalar(CKDWORD *output,
                             const CKDWORD *first,
                             const CKDWORD *second,
                             int pixelCount,
                             float blendFactor);
#endif // TEXTUREPROCESSING_H
