#include "CKTextureFont.h"
#include "CKFontManager.h"

static const CKDWORD TEXT_SCISSOR = 4096;

CKTextureFont::CKTextureFont(CKFontManager *fm, CKContext *ctx, CKSTRING name)
{
    // Font Visual Properties
    m_Leading.Set(0.0f, 0.0f);
    m_Scale.Set(1.0f, 1.0f);
    m_ShadowOffset.Set(4.0f, 4.0f);
    m_ShadowScale.Set(1.0f, 1.0f);
    m_ItalicOffset = 0.0f;
    m_StartColor = RGBAITOCOLOR(255, 255, 255, 255);
    m_EndColor = RGBAITOCOLOR(0, 0, 0, 255);
    m_ShadowColor = RGBAITOCOLOR(0, 0, 0, 128);
    m_Material = 0;
    m_Properties = 0;
    m_FontTexture = 0;
    m_FirstCharacter = 0;
    m_ParagraphIndentation = Vx2DVector(0.0f);
    m_SpacingProperties = 0;
    m_CaretMaterial = NULL;
    m_CaretSize = 0.0f;
    m_SpacePercentage = 0.3f;
    m_SystemFontName = NULL;
    m_Offset.Set(0.0f, 0.0f);
    m_Margins.SetCorners(0.0f, 0.0f, 0.0f, 0.0f);
    m_TextExtents.SetCorners(0.0f, 0.0f, 0.0f, 0.0f);
    m_ClippingRect.SetCorners(0.0f, 0.0f, 0.0f, 0.0f);
    m_ScreenExtents.Set(0.0f, 0.0f);
    m_LineCount = 0;
    m_SpaceSize = 0.0f;
    m_HLeading = 0.0f;
    m_LineWidth = 0.0f;

    m_FontName = CKStrdup(name);
    m_Context = ctx;
    m_FontManager = fm;
}

CKTextureFont::~CKTextureFont()
{
    CKDeletePointer(m_FontName);
    CKDeletePointer(m_SystemFontName);
}

////////////////////////////////////////////////////////////////////////
// NEW FUNCTIONS

// Font Access
char *CKTextureFont::GetFontName()
{
    return m_FontName;
}

CKBOOL CKTextureFont::IsFontSimilar(CKTexture *fontTexture, Vx2DVector &charNumber, CKBOOL fixed)
{
    if (!fontTexture)
        return FALSE;
    if (fixed)
    {
        if (!(m_SpacingProperties & FIXED))
            return FALSE;
    }
    else
    {
        if (m_SpacingProperties & FIXED)
            return FALSE;
    }
    if (m_CharNumber.x != charNumber.x || m_CharNumber.y != charNumber.y)
        return FALSE;
    if (m_FontTexture != CKOBJID(fontTexture))
        return FALSE;

    return TRUE;
}

void CKTextureFont::CreateCKFont(CKTexture *fontTexture, VxRect &textZone, Vx2DVector &charNumber, CKBOOL fixed, int firstCharacter, float iSpaceSize)
{
    if (!fontTexture)
        return;

    m_CharNumber = charNumber;
    m_SpacingProperties = (fixed) ? FIXED : 0;
    m_FirstCharacter = firstCharacter;

    // save the font texture id
    m_FontTexture = CKOBJID(fontTexture);

    float twidth = (float)fontTexture->GetWidth();
    float theight = (float)fontTexture->GetHeight();

    m_ScreenExtents.Set(twidth, theight);

    if ((int)textZone.GetWidth() == 0 || (int)textZone.GetHeight() == 0)
    {
        textZone.SetDimension(0, 0, twidth, theight);
    }

    m_FontZone = textZone;
    m_SpacePercentage = iSpaceSize;
    if (m_SpacePercentage < 0.0f)
        m_SpacePercentage = 0.0f;

    // the Creation
    CreateFromTexture();
}

void CKTextureFont::CreateFromTexture()
{
    if (m_SpacingProperties & CREATED)
        return;

    CKTexture *fontTexture = (CKTexture *)m_Context->GetObject(m_FontTexture);
    if (!fontTexture)
        return;

    int iWidth = fontTexture->GetWidth();
    int iHeight = fontTexture->GetHeight();

    float twidth = (float)iWidth;
    float theight = (float)iHeight;

    m_ScreenExtents.Set(twidth, theight);

    if (m_CharNumber.x <= 0.0f || m_CharNumber.y <= 0.0f)
        return;

    if (m_FirstCharacter < 0)
        m_FirstCharacter = 0;
    if (m_FirstCharacter > 255)
        return;

    int charsX = (int)m_CharNumber.x;
    if ((float)charsX < m_CharNumber.x)
        ++charsX;
    int charsY = (int)m_CharNumber.y;
    if ((float)charsY < m_CharNumber.y)
        ++charsY;
    if (charsX <= 0 || charsY <= 0)
        return;

    float ustep = m_FontZone.GetWidth() / (twidth * (float)charsX);
    float vstep = m_FontZone.GetHeight() / (theight * (float)charsY);

    Vx2DVector v2 = m_FontZone.GetTopLeft();
    const float ustart = v2.x / twidth;
    const float vstart = v2.y / theight;

    // Initialisation of the characters
    for (int k = 0; k < 256; ++k)
    {
        m_FontCoordinates[k].ustart = ustart;
        m_FontCoordinates[k].vstart = vstart;
        m_FontCoordinates[k].uwidth = ustep;
        m_FontCoordinates[k].uprewidth = 0;
        m_FontCoordinates[k].upostwidth = 0;
        m_FontCoordinates[k].vwidth = vstep;
    }

    if (m_SpacingProperties & FIXED) // The font must be fixed
    {
        float u = ustart;
        float v = vstart;
        int c = m_FirstCharacter;
        // fill the uvs with the characters
        for (int i = 0; i < charsY && c < 256; ++i)
        {
            for (int j = 0; j < charsX && c < 256; ++j)
            {
                m_FontCoordinates[c].ustart = u;
                m_FontCoordinates[c].vstart = v;

                m_FontCoordinates[c].uwidth = ustep;
                m_FontCoordinates[c].vwidth = vstep;

                u += ustep;
                c++;
            }
            u = ustart;
            v += vstep;
        }
    }
    else // The font must be proportional
    {
        float u = ustart;
        float v = vstart;
        int c = m_FirstCharacter;
        CKDWORD transColor = 0;
        CKBOOL alpha = TRUE;
        if (fontTexture->IsTransparent())
        {
            alpha = FALSE;
            transColor = fontTexture->GetTransparentColor();
        }

        float width = (float)iWidth;
        float height = (float)iHeight;

        float upixel = 1.0f / width;
        float vpixel = 1.0f / height;

        int xpixel = (int)v2.x;
        int ypixel = (int)v2.y;

        int xwidth = (int)(width * ustep);
        int ywidth = (int)(height * vstep);
        const int rowStart = xpixel;

#ifndef FONTMANAGER_NOSYSFONT
        // Give the priority to the user defined font texture
        // If we give the same name to a texture than a system font, we use the texture
        // with self calculated dimensions

        // Mark the font as to be saved
        m_SpacingProperties |= SPACINGTOBESAVED;

        CKBOOL spacingDone = FALSE;
        // Select the font
        CKTexture *texture = (CKTexture *)m_Context->GetObject(m_FontTexture);
        CKSTRING textureName = (texture) ? texture->GetName() : NULL;
        if (textureName && m_FontManager->SelectFont(textureName))
        {
            if (m_FontManager->IsTrueTypeFont())
            {
                FONT_ABC fontABC[256];
                if (m_FontManager->GetCharABCWidths(0, 255, fontABC))
                {
                    for (int i = 0; i < charsY && c < 256; i++)
                    {
                        for (int j = 0; j < charsX && c < 256; j++)
                        {
                            m_FontCoordinates[c].ustart = u + (float)fontABC[c].abcA * upixel;
                            m_FontCoordinates[c].vstart = v;
                            m_FontCoordinates[c].uprewidth = (float)fontABC[c].abcA * upixel;
                            m_FontCoordinates[c].uwidth = (float)fontABC[c].abcB * upixel;
                            m_FontCoordinates[c].upostwidth = (float)fontABC[c].abcC * upixel;
                            m_FontCoordinates[c].vwidth = vstep;

                            u += ustep;
                            c++;
                        }
                        u = ustart;
                        v += vstep;
                    }
                    spacingDone = TRUE;
                }
            }
            else
            {
                int widths[256];
                if (m_FontManager->GetCharWidths(0, 255, widths))
                {
                    for (int i = 0; i < charsY && c < 256; i++)
                    {
                        for (int j = 0; j < charsX && c < 256; j++)
                        {
                            m_FontCoordinates[c].ustart = u;
                            m_FontCoordinates[c].vstart = v;
                            m_FontCoordinates[c].uprewidth = 0;
                            m_FontCoordinates[c].uwidth = widths[c] * upixel;
                            m_FontCoordinates[c].upostwidth = 0;
                            m_FontCoordinates[c].vwidth = vstep;

                            u += ustep;
                            c++;
                        }
                        u = ustart;
                        v += vstep;
                    }
                    spacingDone = TRUE;
                }
            }
        }
        if (!spacingDone)
#endif
            {
                VxImageDescEx texDesc;
                fontTexture->GetSystemTextureDesc(texDesc);
                CKDWORD *pixelMap = NULL;
                int rowStride = 0;
                if (texDesc.BitsPerPixel == 32)
                {
                    pixelMap = (CKDWORD *)fontTexture->LockSurfacePtr();
                    if (pixelMap)
                    {
                        if (texDesc.BytesPerLine)
                            rowStride = (texDesc.BytesPerLine >> 2);
                        else
                            rowStride = texDesc.Width;
                    }
                }
                if (pixelMap && rowStride <= 0)
                {
                    fontTexture->ReleaseSurfacePtr();
                    pixelMap = NULL;
                }

                // fill the uvs with the characters
                for (int i = 0; i < charsY && c < 256; ++i)
                {
                    for (int j = 0; j < charsX && c < 256; ++j)
                    {
                        m_FontCoordinates[c].ustart = u;
                        m_FontCoordinates[c].vstart = v;

                        m_FontCoordinates[c].uwidth = ustep;
                        m_FontCoordinates[c].vwidth = vstep;

                        // We now try to narrow the character

                        // left
                        int k;
                        for (k = 0; k < xwidth; k++)
                        {
                            CKDWORD color;
                            int y;
                            if (alpha)
                            {
                                for (y = 0; y < ywidth; y++)
                                {
                                    if (pixelMap)
                                        color = pixelMap[xpixel + k + rowStride * (ypixel + y)];
                                    else
                                        color = fontTexture->GetPixel(xpixel + k, ypixel + y);
                                    if (ColorGetAlpha(color))
                                        break;
                                }
                            }
                            else
                            {
                                for (y = 0; y < ywidth; y++)
                                {
                                    if (pixelMap)
                                        color = pixelMap[xpixel + k + rowStride * (ypixel + y)];
                                    else
                                        color = fontTexture->GetPixel(xpixel + k, ypixel + y);
                                    if (color != transColor)
                                        break;
                                }
                            }
                            if (y < ywidth)
                                break;
                        }

                        if (k == xwidth) // the whole character is empty
                        {
                            m_FontCoordinates[c].uwidth *= m_SpacePercentage; // Changed from 0.5 to 0.3 coz was too big
                            u += ustep;
                            xpixel += xwidth;

                            c++;
                            // We go on to the next character
                            continue;
                        }
                        else
                        {
                            m_FontCoordinates[c].ustart += k * upixel;
                            m_FontCoordinates[c].uwidth -= k * upixel;
                        }

                        // right
                        for (k = 0; k < xwidth; k++)
                        {
                            int y;
                            const int baseIndex = xpixel + xwidth - 1 - k;
                            for (y = 0; y < ywidth; y++)
                            {
                                CKDWORD color;
                                if (pixelMap)
                                    color = pixelMap[baseIndex + rowStride * (ypixel + y)];
                                else
                                    color = fontTexture->GetPixel(baseIndex, ypixel + y);
                                if (alpha)
                                {
                                    if (ColorGetAlpha(color))
                                        break;
                                }
                                else
                                {
                                    if (color != transColor)
                                        break;
                                }
                            }
                            if (y < ywidth)
                                break;
                        }

                        m_FontCoordinates[c].uwidth -= k * upixel;
                        if (m_FontCoordinates[c].uwidth < 0.0f)
                            m_FontCoordinates[c].uwidth = 0.0f;

                        u += ustep;
                        xpixel += xwidth;

                        c++;
                    }

                    u = ustart;
                    xpixel = rowStart;

                    v += vstep;
                    ypixel += ywidth;
                }

                if (pixelMap)
                    fontTexture->ReleaseSurfacePtr();
            }
    }

    if (m_FirstCharacter)
    {
        m_FontCoordinates[0].vwidth = vstep;
    }

    // The font is now officially created
    m_SpacingProperties |= CREATED;
}

/////////////////////////
// The leading must be in relative of the texture too (for example leading = leadinginpixels/texturewidth or leadinginpixels/devicewidth)
// Warning : Is /texturewidth working ok ?
////////////////////////////
float CKTextureFont::GetStringWidth(CKSTRING string)
{
    if (!string)
        return 0.0f;

    float scale = m_Scale.x * m_ScreenExtents.x;
    if (scale == 0.0f)
        return 0.0f;
    float leading = m_Leading.x / scale;
    float italic = m_ItalicOffset / scale;

    float sw = 0.0f;
    CKBOOL hasChar = FALSE;
    const CharacterTextureCoordinates *coords = m_FontCoordinates;
    const char *ptr = string;
    while ((*ptr > 0) && *ptr != '\n')
    {
        const CharacterTextureCoordinates &ctc = coords[(unsigned char)*ptr];
        sw += (ctc.uprewidth + ctc.uwidth + ctc.upostwidth + leading);
        ++ptr;
        hasChar = TRUE;
    }

    if (hasChar)
    {
        // No space after last character of the line
        sw -= leading;
        // Italic offset at the end
        sw += italic;
    }

    return sw * scale;
}

int CKTextureFont::GetTextExtents(float &width, float &height)
{
    int linecount = m_FontManager->GetLineCount();

    float vspace = m_FontCoordinates[0].vwidth * (m_Scale.y * m_ScreenExtents.y) + m_Leading.y;
    for (int i = 0; i < linecount; ++i)
    {
        // current line
        LineData *data = m_FontManager->GetLine(i);

        if (data->stringwidth > width)
        {
            width = data->stringwidth;
        }

        // Paragraph Indentation
        if ((data->len < 0) && (i != 0))
        {
            height += m_ParagraphIndentation.y * m_FontCoordinates[0].vwidth * (m_Scale.y * m_ScreenExtents.y);
        }

        // We add the Y space
        height += vspace;
    }

    return linecount;
}

void DrawFillRectangle(CKRenderContext *dev, CKMaterial *mat, VxRect &rect, CKBOOL lighted, CKBOOL transform)
{
    if (!mat)
        return;

    VxDrawPrimitiveData *data;
    if (transform && lighted)
        data = dev->GetDrawPrimitiveStructure(CKRST_DP_TR_CL_VNT, 4);
    else if (transform)
        data = dev->GetDrawPrimitiveStructure(CKRST_DP_TR_CL_VCT, 4);
    else if (lighted)
        data = dev->GetDrawPrimitiveStructure((CKRST_DPFLAGS)(CKRST_DP_TR_CL_VNT & ~CKRST_DP_TRANSFORM), 4);
    else
        data = dev->GetDrawPrimitiveStructure(CKRST_DP_CL_VCT, 4);

    CKWORD *indices = dev->GetDrawPrimitiveIndices(4);

#if CKVERSION == 0x13022002 || CKVERSION == 0x05082002
    VxUV *uvs = (VxUV *)data->TexCoordPtr;
    VxVector4 *positions = (VxVector4 *)data->PositionPtr;
    VxVector *normals = (VxVector *)data->NormalPtr;
    CKDWORD *colors = (CKDWORD *)data->ColorPtr;
#else
    VxUV *uvs = (VxUV *)data->TexCoord.Ptr;
    VxVector4 *positions = (VxVector4 *)data->Positions.Ptr;
    VxVector *normals = (VxVector *)data->Normals.Ptr;
    CKDWORD *colors = (CKDWORD *)data->Colors.Ptr;
#endif

    mat->SetAsCurrent(dev);
    dev->SetState(VXRENDERSTATE_ZWRITEENABLE, FALSE);

    if (lighted)
    {
        /////////////////
        // Normals

#if CKVERSION == 0x13022002 || CKVERSION == 0x05082002
        const unsigned int normalStride = data->NormalStride;
#else
        const unsigned int normalStride = data->Normals.Stride;
#endif

        // Normal 0
        normals->x = 0;
        normals->y = 0;
        normals->z = 1.0f;
        normals = (VxVector *)((CKBYTE *)normals + normalStride);
        // Normal 1
        normals->x = 0;
        normals->y = 0;
        normals->z = 1.0f;
        normals = (VxVector *)((CKBYTE *)normals + normalStride);
        // Normal 2
        normals->x = 0;
        normals->y = 0;
        normals->z = 1.0f;
        normals = (VxVector *)((CKBYTE *)normals + normalStride);
        // Normal 3
        normals->x = 0;
        normals->y = 0;
        normals->z = 1.0f;
        normals = (VxVector *)((CKBYTE *)normals + normalStride);
    }
    else
    {
        /////////////////
        // Colors

#if CKVERSION == 0x13022002 || CKVERSION == 0x05082002
        const unsigned int colorStride = data->ColorStride;
#else
        const unsigned int colorStride = data->Colors.Stride;
#endif

        VxColor vxcol = mat->GetDiffuse();
        CKDWORD col = RGBAFTOCOLOR(&vxcol);
        // Vertex 0
        *colors = col;
        colors = (CKDWORD *)((CKBYTE *)colors + colorStride);
        // Vertex 1
        *colors = col;
        colors = (CKDWORD *)((CKBYTE *)colors + colorStride);
        // Vertex 2
        *colors = col;
        colors = (CKDWORD *)((CKBYTE *)colors + colorStride);
        // Vertex 3
        *colors = col;
        colors = (CKDWORD *)((CKBYTE *)colors + colorStride);
    }

    /////////////////
    // UVs

#if CKVERSION == 0x13022002 || CKVERSION == 0x05082002
    const unsigned int texCoordStride = data->TexCoordStride;
#else
    const unsigned int texCoordStride = data->TexCoord.Stride;
#endif

    // Vertex 0
    uvs->u = 0.0f;
    uvs->v = 0.0f;
    uvs = (VxUV *)((CKBYTE *)uvs + texCoordStride);
    // Vertex 1
    uvs->u = 1.0f;
    uvs->v = 0.0f;
    uvs = (VxUV *)((CKBYTE *)uvs + texCoordStride);
    // Vertex 2
    uvs->u = 1.0f;
    uvs->v = 1.0f;
    uvs = (VxUV *)((CKBYTE *)uvs + texCoordStride);
    // Vertex 3
    uvs->u = 0.0f;
    uvs->v = 1.0f;
    uvs = (VxUV *)((CKBYTE *)uvs + texCoordStride);

    /////////////////
    // Positions

#if CKVERSION == 0x13022002 || CKVERSION == 0x05082002
    const unsigned int positionStride = data->PositionStride;
#else
    const unsigned int positionStride = data->Positions.Stride;
#endif

    // Vertex 0
    positions->x = rect.left;
    positions->y = rect.top;
    positions->z = 0.0f;
    positions->w = 1.0f;
    positions = (VxVector4 *)((CKBYTE *)positions + positionStride);
    // Vertex 1
    positions->x = rect.right;
    positions->y = rect.top;
    positions->z = 0.0f;
    positions->w = 1.0f;
    positions = (VxVector4 *)((CKBYTE *)positions + positionStride);
    // Vertex 2
    positions->x = rect.right;
    positions->y = rect.bottom;
    positions->z = 0.0f;
    positions->w = 1.0f;
    positions = (VxVector4 *)((CKBYTE *)positions + positionStride);
    // Vertex 3
    positions->x = rect.left;
    positions->y = rect.bottom;
    positions->z = 0.0f;
    positions->w = 1.0f;
    positions = (VxVector4 *)((CKBYTE *)positions + positionStride);

    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    indices[3] = 3;

    // the drawing itself
    dev->DrawPrimitive(VX_TRIANGLEFAN, indices, 4, data);
}

int CKTextureFont::BuildStringGeometry(CKRenderContext *dev, CKSTRING string, int slen, VxVector position, VxRect &textZone, CKDWORD textoptions, CompiledTextData *ctdata, TextDrawEmitter *emitter, CKBOOL drawNow)
{
    if (!slen)
        return 0;
    int len = slen;
    const CKBOOL is3d = (textoptions & TEXT_3D) != 0;
    const CKBOOL doClip = (textoptions & TEXT_CLIP) != 0;
    const CKBOOL scissorClip = (textoptions & TEXT_SCISSOR) != 0;
    const CKBOOL showCaret = (textoptions & TEXT_SHOWCARET) != 0;
    const CharacterTextureCoordinates *coords = m_FontCoordinates;

    // We check if we are in Text Compilation
    if (!(textoptions & TEXT_COMPILED))
        ctdata = NULL;

    // Screen Clipping
    VxRect textzone = textZone;
    if (textoptions & TEXT_SCREENCLIP)
    {
        if (!textzone.Clip(m_ClippingRect))
            return 0;
    }

    ///
    // Creation of the DrawPrim Data
    VxDrawPrimitiveData *data = NULL;
    CKWORD *IndicesPtr = NULL;
    if (ctdata)
    {
        if (is3d) // 3D
        {
            if (m_Properties & FONT_LIGHTING) // Lighting
            {
                data = ctdata->GetStructure(CKRST_DP_TR_CL_VNT, len * 4);
            }
            else
            {
                data = ctdata->GetStructure(CKRST_DP_TR_CL_VCT, len * 4);
            }
        }
        else // 2D
        {
            data = ctdata->GetStructure(CKRST_DP_CL_VCST, len * 4);
        }

        // Indices
        IndicesPtr = ctdata->GetIndices(len * 6);
    }
    else
    {
        // Draw Prim Data
        if (is3d) // 3D
        {
            if (m_Properties & FONT_LIGHTING) // Lighting
            {
                data = dev->GetDrawPrimitiveStructure(CKRST_DP_TR_CL_VNT, len * 4);
            }
            else
            {
                data = dev->GetDrawPrimitiveStructure(CKRST_DP_TR_CL_VCT, len * 4);
            }
        }
        else // 2D
        {
            data = dev->GetDrawPrimitiveStructure(CKRST_DP_CL_VCST, len * 4);
        }

        // Indices
        IndicesPtr = dev->GetDrawPrimitiveIndices(len * 6);
    }

    VxVector pos = position;
    const float width = m_Scale.x * m_ScreenExtents.x;
    const float height = m_Scale.y * m_ScreenExtents.y;
    float italic = m_ItalicOffset;
    const float invWidth = (doClip && !scissorClip) ? (1.0f / width) : 0.0f;

    CKTexture *tex = GetFontTexture();
    if (!tex)
    {
        return 0;
    }

    // Multiplication by scale removed because big fonts were cropped !
    float texelwidth = /*m_Scale.x */ 0.25f / tex->GetWidth();
    float texelheight = /*m_Scale.y */ 0.25f / tex->GetHeight();

    CKWORD *indices = IndicesPtr;
    const CKDWORD dataFlags = data->Flags;
#if CKVERSION == 0x13022002 || CKVERSION == 0x05082002
    XPtrStrided<VxVector4> positions(data->PositionPtr, data->PositionStride);
    XPtrStrided<VxUV> uvs(data->TexCoordPtr, data->TexCoordStride);
#else
    XPtrStrided<VxVector4> positions(data->Positions.Ptr, data->Positions.Stride);
    XPtrStrided<VxUV> uvs(data->TexCoord.Ptr, data->TexCoord.Stride);
#endif

    int index = 0;

    int oldlen = len;
    // We reset the counter
    len = 0;

    // int conversion for proper rendering
    if (!is3d)
    {
        pos.x = (float)(int)(pos.x + 0.5f);
        pos.y = (float)(int)(pos.y + 0.5f);

        if (!(textoptions & TEXT_JUSTIFIED))
        {
            m_HLeading = (float)(int)(m_HLeading + 0.5f);
        }
    }

    float lineheight = coords[0].vwidth * height;

    CKBOOL seeCaret = FALSE;
    // Clipping Y
    float starty = pos.y;
    float endy = pos.y + lineheight;
    float cutup = 0.0f;
    float cutdown = 1.0f;

    if (doClip)
    {
        // Bottom of the zone
        if (pos.y > textzone.bottom)
            return 0;
        // Top of the zone
        if (endy < textzone.top)
            return 0;
        // Right of the zone
        if (pos.x > textzone.right)
            return 0;
        // Left of the zone
        if (pos.x + m_LineWidth < textzone.left)
            return 0;

        if (!scissorClip)
        {
            // Cut on top
            if (starty < textzone.top)
            {
                cutup = (textzone.top - starty) / lineheight;
                starty = textzone.top;
            }
            // Cut on bottom
            if (endy > textzone.bottom)
            {
                cutdown = 1.0f - (endy - textzone.bottom) / lineheight;
                endy = textzone.bottom;
            }
        }
    }

    // Temporary Variables
    float startx = -1.0f;
    float endx = -1.0f;
    float startu = -1.0f;
    float endu = -1.0f;

    while (slen--)
    {
        // Skipping irrelevant character
        if (*string == ' ') // Here comes a space or a null character
        {
            if (seeCaret)
            {
                if (showCaret)
                {
                    if (drawNow)
                    {
                        DrawCaret(dev, pos.x, starty, m_SpaceSize, endy - starty, dataFlags);
                        SetRenderStates(dev, textoptions);
                    }
                    else if (emitter && emitter->EmitCaret)
                    {
                        emitter->EmitCaret(emitter->user, pos.x, starty, m_SpaceSize, endy - starty, dataFlags);
                    }
                }
                seeCaret = FALSE;
            }
            pos.x += m_SpaceSize;

            ++string;
            continue;
        }

        if (*string == '\b') // Caret character
        {
            if (!slen) // end of string, we have to draw the caret now
            {
                if (showCaret)
                {
                    if (drawNow)
                    {
                        DrawCaret(dev, pos.x, starty, m_SpaceSize, endy - starty, dataFlags);
                        SetRenderStates(dev, textoptions);
                    }
                    else if (emitter && emitter->EmitCaret)
                    {
                        emitter->EmitCaret(emitter->user, pos.x, starty, m_SpaceSize, endy - starty, dataFlags);
                    }
                }
            }
            seeCaret = TRUE;

            ++string;
            continue;
        }

        const CharacterTextureCoordinates *ctc = &coords[(unsigned char)*string];

        // If the character is empty, we skip it
        if (ctc->vwidth == 0.0f)
        {
            string++;
            continue;
        }

        /////////////////
        // Positions

        startx = pos.x + ctc->uprewidth * width;
        endx = startx + width * ctc->uwidth;
        startu = ctc->ustart;
        endu = ctc->ustart + ctc->uwidth;

        ////////////////////
        // Clipping

        if (doClip)
        {
            if (startx > textzone.right)
                break; // letter totally Right of the zone : we can stop writing

            if (endx < textzone.left) // letter totally Left of the zone : skip tio the next one
            {
                pos.x = endx + m_HLeading + ctc->upostwidth * width;
                ++string;
                continue;
            }

            if (!scissorClip)
            {
                if (pos.x < textzone.left) // Letter partially left of the zone
                {
                    startu += (textzone.left - startx) * invWidth;
                    startx = textzone.left;
                }

                if (endx > textzone.right) // Letter partially right of the zone
                {
                    endu -= (endx - textzone.right) * invWidth;
                    endx = textzone.right;
                }
            }
        }

        // Vertices 0 1 2 3
        if (!is3d)
        {
            positions->Set((int)(startx + italic + 0.5f) - 0.25f, starty, 0.01f, 1.0f);
            ++positions;
            positions->Set((int)(endx + italic + 0.5f) - 0.25f, starty, 0.01f, 1.0f);
            ++positions;
            positions->Set((int)(endx + 0.5f) - 0.25f, endy, 0.01f, 1.0f);
            ++positions;
            positions->Set((int)(startx + 0.5f) - 0.25f, endy, 0.01f, 1.0f);
            ++positions;
        }
        else
        {
            positions->Set(startx + italic, starty, 0.01f, 1.0f);
            ++positions;
            positions->Set(endx + italic, starty, 0.01f, 1.0f);
            ++positions;
            positions->Set(endx, endy, 0.01f, 1.0f);
            ++positions;
            positions->Set(startx, endy, 0.01f, 1.0f);
            ++positions;
        }

        if (seeCaret)
        {
            if (showCaret)
            {
                if (drawNow)
                {
                    DrawCaret(dev, startx, starty, endx - startx, endy - starty, dataFlags);
                    SetRenderStates(dev, textoptions);
                }
                else if (emitter && emitter->EmitCaret)
                {
                    emitter->EmitCaret(emitter->user, startx, starty, endx - startx, endy - starty, dataFlags);
                }
            }
            seeCaret = FALSE;
        }

        // advance to next character
        pos.x = endx + ctc->upostwidth * width + m_HLeading;

        /////////////////
        // UVs

        float topv = ctc->vstart + cutup * ctc->vwidth;
        float botv = ctc->vstart + cutdown * ctc->vwidth;

        startu += texelwidth;
        topv += texelheight;

        // Vertex 0 1 2 3
        uvs->u = startu;
        uvs->v = topv;
        ++uvs;
        uvs->u = endu;
        uvs->v = topv;
        ++uvs;
        uvs->u = endu;
        uvs->v = botv;
        ++uvs;
        uvs->u = startu;
        uvs->v = botv;
        ++uvs;

        /////////////////////
        // Indices

        indices[0] = index;
        indices[1] = index + 1;
        indices[2] = index + 2;
        indices[3] = index;
        indices[4] = index + 2;
        indices[5] = index + 3;
        indices += 6;
        index += 4;

        // String advance
        ++string;
        // One more face couple
        ++len;
    }

    if (!len)
        return 0;

    ///
    // Colors & Normals

    if (m_Properties & FONT_LIGHTING)
    {
        /////////////////
        // Normals

        VxVector norm(0, 0, 1.0f);
#if CKVERSION == 0x13022002 || CKVERSION == 0x05082002
        VxFillStructure(len * 4, data->NormalPtr, data->NormalStride, sizeof(VxVector), &norm);
#else
        VxFillStructure(len * 4, data->Normals.Ptr, data->Normals.Stride, sizeof(VxVector), &norm);
#endif
    }
    else
    {
        /////////////////
        // Colors

        CKDWORD cols[4];
        if (m_Properties & FONT_GRADIENT)
        {
            CKDWORD scolor = m_StartColor;
            CKDWORD ecolor = m_EndColor;
            if (cutup != 0.0f || cutdown != 0.0f)
            {
                cutdown = 1.0f - cutdown;

                VxColor scol(scolor);
                VxColor ecol(ecolor);
                VxColor delta(ecol.r - scol.r, ecol.g - scol.g, ecol.b - scol.b, ecol.a - scol.a);
                scol.r += (delta.r) * cutup;
                scol.g += (delta.g) * cutup;
                scol.b += (delta.b) * cutup;
                scol.a += (delta.a) * cutup;
                ecol.r -= (delta.r) * cutdown;
                ecol.g -= (delta.g) * cutdown;
                ecol.b -= (delta.b) * cutdown;
                ecol.a -= (delta.a) * cutdown;
                scolor = RGBAFTOCOLOR(&scol);
                ecolor = RGBAFTOCOLOR(&ecol);
            }

            cols[0] = scolor;
            cols[1] = scolor;
            cols[2] = ecolor;
            cols[3] = ecolor;
        }
        else
        {
            cols[0] = m_StartColor;
            cols[1] = m_StartColor;
            cols[2] = m_StartColor;
            cols[3] = m_StartColor;
        }

        // WARNING : this works only because all the colors are contiguous...
#if CKVERSION == 0x13022002 || CKVERSION == 0x05082002
        VxFillStructure(len, data->ColorPtr, 4 * data->ColorStride, 4 * sizeof(CKDWORD), &cols);
#else
        VxFillStructure(len, data->Colors.Ptr, 4 * data->Colors.Stride, 4 * sizeof(CKDWORD), &cols);
#endif
    }

    if (ctdata)
        ctdata->PatchIndices(oldlen * 6, len * 6);

    if (drawNow)
    {
        // the drawing itself
        dev->DrawPrimitive(VX_TRIANGLELIST, IndicesPtr, len * 6, data);
    }

    return len * 6;
}

void CKTextureFont::DrawString(CKRenderContext *dev, CKSTRING string, int slen, VxVector position, VxRect &textZone, CKDWORD textoptions, CompiledTextData *ctdata)
{
    BuildStringGeometry(dev, string, slen, position, textZone, textoptions, ctdata, NULL, TRUE);
}

int CKTextureFont::AppendStringGeometry(CKRenderContext *dev, CKSTRING string, int slen, VxVector position, VxRect &textZone, CKDWORD textoptions, CompiledTextData *ctdata, TextDrawEmitter *emitter)
{
    if (!ctdata)
        return 0;

    CKDWORD options = textoptions | TEXT_COMPILED;
    return BuildStringGeometry(dev, string, slen, position, textZone, options, ctdata, emitter, FALSE);
}

void CKTextureFont::DrawCaret(CKRenderContext *context, float posx, float posy, float dimx, float dimy, CKDWORD flags)
{
    VxDrawPrimitiveData data = {};
    data.VertexCount = 4;
    if (m_CaretMaterial)
        m_CaretMaterial->SetAsCurrent(context);
    VxVector4 positions[4];
    float uvs[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    positions[0].Set(posx, posy + dimy, 0.0f, 1.0f);
    positions[1].Set(posx, posy + dimy * (1.0f - m_CaretSize), 0.0f, 1.0f);
    positions[2].Set(posx + dimx, posy + dimy * (1.0f - m_CaretSize), 0.0f, 1.0f);
    positions[3].Set(posx + dimx, posy + dimy, 0.0f, 1.0f);
    VxColor diffuse(1.0f, 1.0f, 1.0f, 0.5f);
    if (m_CaretMaterial)
        diffuse = m_CaretMaterial->GetDiffuse();
    CKDWORD col = RGBAFTOCOLOR(&diffuse);
    CKDWORD colors[4] = {col, col, col, col};
    data.Flags = flags;
#if CKVERSION == 0x13022002 || CKVERSION == 0x05082002
    data.PositionPtr = &positions;
    data.PositionStride = sizeof(VxVector4);
    data.TexCoordPtr = &uvs;
    data.TexCoordStride = 2 * sizeof(float);
    data.ColorPtr = &colors;
    data.ColorStride = sizeof(CKDWORD);
    data.NormalPtr = NULL;
    data.SpecularColorPtr = NULL;
#else
    data.Positions.Ptr = &positions;
    data.Positions.Stride = sizeof(VxVector4);
    data.TexCoord.Ptr = &uvs;
    data.TexCoord.Stride = 2 * sizeof(float);
    data.Colors.Ptr = &colors;
    data.Colors.Stride = sizeof(CKDWORD);
    data.Normals.Ptr = NULL;
    data.SpecularColors.Ptr = NULL;
#endif
    context->DrawPrimitive(VX_TRIANGLEFAN, NULL, 4, &data);
}

void CKTextureFont::DrawStringShadowed(CKRenderContext *dev, CKSTRING string, int slen, VxVector position, VxRect &textZone, CKDWORD textoptions, CompiledTextData *ctdata)
{
    if (!slen)
        return;

    const CKBOOL drawShadow = (ColorGetAlpha(m_ShadowColor) != 0);
    if (drawShadow)
    {
        VxVector shadowoffset(m_ShadowOffset.x, m_ShadowOffset.y, 0);
        VxVector shadowpos = position + shadowoffset;

        CKDWORD oldproperties = m_Properties;
        m_Properties &= ~FONT_GRADIENT;

        CKDWORD oldstartcolor = m_StartColor;
        m_StartColor = m_ShadowColor;

        Vx2DVector oldscale = m_Scale;
        m_Scale = m_ShadowScale;

        // We draw the shadow
        DrawString(dev, string, slen, shadowpos, textZone, textoptions, ctdata);

        // We restore the normal states
        m_StartColor = oldstartcolor;
        m_Properties = oldproperties;
        m_Scale = oldscale;
    }

    // We draw the real string
    DrawString(dev, string, slen, position, textZone, textoptions, ctdata);
}

void CKTextureFont::SetRenderStates(CKRenderContext *dev, int options)
{
    ///
    // STATES

    // Render in solid
    dev->SetState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);

    dev->SetState(VXRENDERSTATE_SPECULARENABLE, FALSE);

    if (m_Properties & FONT_LIGHTING)
    {
        CKMaterial *mat = (CKMaterial *)m_Context->GetObject(m_Material);
        if (mat)
            mat->SetAsCurrent(dev);
    }
    else
    {
        dev->SetState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
        dev->SetState(VXRENDERSTATE_WRAP0, 0);
        dev->SetState(VXRENDERSTATE_SRCBLEND, VXBLEND_SRCALPHA);
        dev->SetState(VXRENDERSTATE_DESTBLEND, VXBLEND_INVSRCALPHA);
    }

    CKTexture *fonttexture = (CKTexture *)m_Context->GetObject(m_FontTexture);
    dev->SetTexture(fonttexture);

    dev->SetState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
    dev->SetState(VXRENDERSTATE_ZWRITEENABLE, FALSE);

    if (options & TEXT_3D)
    {
        dev->SetState(VXRENDERSTATE_ZENABLE, TRUE);
    }
    else
    {
        dev->SetState(VXRENDERSTATE_ZENABLE, FALSE);
    }

    if (m_Properties & FONT_GRADIENT)
    {
        dev->SetState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);
    }
    else
    {
        dev->SetState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
    }

    dev->SetTextureStageState(CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);
    dev->SetTextureStageState(CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);

    dev->SetTextureStageState(CKRST_TSS_STAGEBLEND, 0, 1);
    dev->SetTextureStageState(CKRST_TSS_TEXTURETRANSFORMFLAGS, 0);
    dev->SetTextureStageState(CKRST_TSS_TEXCOORDINDEX, 0);

    if (m_Properties & FONT_DISABLEFILTER)
    {
        dev->SetTextureStageState(CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
        dev->SetTextureStageState(CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    }
    else
    {
        if (fonttexture && ((fonttexture->GetMipmapCount() > 1) || (fonttexture->GetMipmapCount() == -1)))
            dev->SetTextureStageState(CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEARMIPLINEAR);
        else
            dev->SetTextureStageState(CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEAR);
        dev->SetTextureStageState(CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_LINEAR);
    }
}

void CKTextureFont::DrawCKText(CKRenderContext *dev, CKBeObject *obj, CKSTRING string, int align, VxRect &textZone, CKMaterial *mat, int options, CKBOOL reallyDraw)
{
    CK_ID objID = obj->GetID();
    const CKBOOL optCompiled = (options & TEXT_COMPILED) != 0;
    const CKBOOL optBackground = (options & TEXT_BACKGROUND) != 0;
    const CKBOOL opt3d = (options & TEXT_3D) != 0;
    const CKBOOL optScreen = (options & TEXT_SCREEN) != 0;
    const CKBOOL optJustified = (options & TEXT_JUSTIFIED) != 0;
    const CKBOOL optWordwrap = (options & TEXT_WORDWRAP) != 0;
    const CKBOOL optResizeVert = (options & TEXT_RESIZE_VERT) != 0;
    const CKBOOL optResizeHori = (options & TEXT_RESIZE_HORI) != 0;
    const CKBOOL alignTop = (align & VALIGN_TOP) != 0;
    const CKBOOL alignBottom = (align & VALIGN_BOTTOM) != 0;
    const CKBOOL alignLeft = (align & HALIGN_LEFT) != 0;
    const CKBOOL alignRight = (align & HALIGN_RIGHT) != 0;
    const CKBOOL doWrap = optJustified || optWordwrap;
    CKFontManager *fontMgr = m_FontManager;
    const CharacterTextureCoordinates *coords = m_FontCoordinates;

    CompiledTextData *ctdata = NULL;
    if (optCompiled) // The user asked for text compilation
    {
        if (reallyDraw && (ctdata = fontMgr->GetCompiledText(objID)))
        {
            if (optBackground)
            {
                // Rendering of the rectangle
                DrawFillRectangle(dev, mat, ctdata->m_DrawZone, m_Properties & FONT_LIGHTING, opt3d);
            }

            // set the render states
            SetRenderStates(dev, options);

            // Do the rendering
            ctdata->Render(dev);

            return;
        }
        else
        {
            // we have to add the compiled data
            ctdata = fontMgr->AddCompiledText(objID);
        }
    }
    else
    {
        // we clear the data
        fontMgr->RemoveCompiledText(objID);
    }

    VxRect drawzone = textZone;
    textZone.left += m_Margins.left;
    textZone.right -= m_Margins.right;
    textZone.top += m_Margins.top;
    textZone.bottom -= m_Margins.bottom;

    // If the textZone is smaller than a character maxwidth, we do not write anything
    float inv32f = opt3d ? (1.0f / 32.0f) : 1.0f;

    ///
    // We choose the relative coordinates (screen(same size,peu importe la resolution) or textures(looks better, original fontsize)....)
    if (optScreen)
    {
        VxRect ui;
        if (!opt3d && dev->Get2dRoot(FALSE)) {
            // Follow the same UI canvas as the entity text zone. The engine
            // keeps this full-size normally, or centered/aspect-preserving for
            // a responsive view; the independent 3D viewport may be wider.
            dev->Get2dRoot(FALSE)->GetRect(ui);
            m_ScreenExtents.Set(ui.GetWidth(), ui.GetHeight());
        }
        else
            m_ScreenExtents.Set(dev->GetWidth() * inv32f, dev->GetHeight() * inv32f);
    }
    else
    {
        CKTexture *fonttexture = GetFontTexture();
        if (fonttexture)
            m_ScreenExtents.Set(fonttexture->GetWidth() * inv32f, fonttexture->GetHeight() * inv32f);
    }

    if (doWrap)
    {
        if (textZone.GetWidth() < inv32f * m_FontZone.GetWidth() / (m_Scale.x * m_ScreenExtents.x * m_CharNumber.x))
            return;
    }

    // Dimensions variables
    float width = m_Scale.x * m_ScreenExtents.x;
    float height = m_Scale.y * m_ScreenExtents.y;
    float spaceadvance = coords[' '].uprewidth + coords[' '].uwidth + coords[' '].upostwidth;
    float hlead = m_Leading.x;
    if (fontMgr->m_VersionUpdate)
    {
        hlead = 0.1f * width * spaceadvance * m_Leading.x;
    }
    float spacesize = width * spaceadvance + hlead;
    float zonewidth = textZone.GetWidth();
    float paragraphIndentX = m_ParagraphIndentation.x * coords[0].uwidth * width;
    float paragraphIndentY = m_ParagraphIndentation.y * coords[0].vwidth * height;
    float verticalspace = coords[0].vwidth * height + m_Leading.y;

    // We clear the line array
    fontMgr->ClearLines();

    // Line Data Variables
    LineData ldata;

    // Wrapping variables
    const char *lastword = "\n";
    int lastlen = 0;
    float lastwidth = 0.0f;
    CKBOOL lastcharacterwasspace = TRUE;
    CKBOOL paragraphstart = TRUE;
    int linecount = 0;
    float textwidth = 0.0f;
    float textheight = 0.0f;

    // String cutting, LineData Array filling
    while (*string)
    {
        ldata.string = string;
        ldata.len = 0;
        ldata.nbspace = 0;
        if (paragraphstart)
        {
            ldata.stringwidth = paragraphIndentX;
        }
        else
            ldata.stringwidth = 0.0f;

        // we iterate line by line
        while (*string && *string != '\n')
        {
            // We encounter a space
            if (*string == ' ')
            {
                if (!lastcharacterwasspace) // we already encounter a space
                    lastcharacterwasspace = TRUE;

                lastlen = ldata.len;
                lastword = string + 1;
                lastwidth = ldata.stringwidth;

                ldata.nbspace++;
                ldata.stringwidth += spacesize;
            }
            else // we encounter a character
            {
                if (lastcharacterwasspace) // Is it a word start ?
                {
                    lastcharacterwasspace = FALSE;
                    lastlen = ldata.len - 1;
                    lastword = string;
                    lastwidth = ldata.stringwidth - spacesize;
                }
                const CharacterTextureCoordinates *tctc = &coords[(unsigned char)*string];
                ldata.stringwidth += (tctc->uprewidth + tctc->uwidth + tctc->upostwidth) * width + hlead;
            }

            // If wordwrap, whe have to see if the text fit in the line
            if (doWrap)
            {
                if (ldata.stringwidth > zonewidth) // it does not : we break the line
                {
                    if (lastlen > 0) // the word was not on the beginning of the line
                    {
                        ldata.len = lastlen;
                        ldata.stringwidth = lastwidth;
                        // We drop the last character before this word
                        ldata.nbspace--;
                        string = lastword;
                    }
                    else // first word of the line : we have to break it
                    {
                        if (*string != ' ') // the character length was already added : we subtract it
                        {
                            const CharacterTextureCoordinates *tctc = &coords[(unsigned char)*string];
                            ldata.stringwidth -= (tctc->uprewidth + tctc->uwidth + tctc->upostwidth) * width + hlead;
                            if (lastword == string)
                                string++;
                            lastword = string;
                        }
                    }
                    lastcharacterwasspace = TRUE;
                    break;
                }
            }

            ldata.len++;
            string++;
        }

        if (paragraphstart)
        {
            ldata.len = -ldata.len;
            paragraphstart = FALSE;
        }

        if (*string == '\n')
        {
            paragraphstart = TRUE;
            // Patch for infinite loop
            if (*lastword == '\n')
                string++;
        }
        // end of a line (or text)
        if (ldata.stringwidth > textwidth)
            textwidth = ldata.stringwidth;
        if ((ldata.len < 0) && (linecount != 0))
            textheight += paragraphIndentY;
        textheight += verticalspace;
        linecount++;

        m_FontManager->AddLine(ldata);

        if (*string && string != lastword && *lastword != '\n')
            string++;
    }

    // now we have to draw the strings
    VxVector pos(textZone.left, textZone.top, 0.0f);

    m_LineCount = linecount;

    ///
    // Vertical Alignment Calculation
    if (alignTop) // Top
    {
        pos.y = textZone.top;
    }
    else if (alignBottom) // Bottom
    {
        pos.y = textZone.bottom - textheight;
    }
    else // Center
    {
        pos.y = (textZone.bottom + textZone.top) * 0.5f - textheight * 0.5f;
    }

    ///
    // Horizontal Alignment Calculation
    if (alignLeft) // Top
    {
        pos.x = textZone.left;
    }
    else if (alignRight) // Bottom
    {
        pos.x = textZone.right - textwidth;
    }
    else // Center
    {
        pos.x = (textZone.right + textZone.left) * 0.5f - textwidth * 0.5f;
    }

    m_TextExtents.SetDimension(pos.x, pos.y, textwidth, textheight);

    VxRect backgroundRect = m_TextExtents;
    if (optJustified)
    {
        backgroundRect.left = textZone.left;
        backgroundRect.right = textZone.right;
    }

    ///
    // Resize
    if (obj) // Entity to resize ?
    {
        if (optResizeVert || optResizeHori)
        {
            drawzone.top = pos.y;
            if (optJustified) // Only vertically
            {
                drawzone.bottom = drawzone.top + textheight;
            }
            else
            {
                if (optResizeHori)
                    drawzone.right = drawzone.left + textwidth;
                if (optResizeVert)
                    drawzone.bottom = drawzone.top + textheight;
            }
            if (opt3d)
            {
                if (CKIsChildClassOf(obj, CKCID_SPRITE3D))
                {
                    CKSprite3D *spr = (CKSprite3D *)obj;
                    Vx2DVector v2d(textwidth, textheight);
                    spr->SetSize(v2d);
                    // spr->SetOffset(Vx2DVector(drawzone.left,-drawzone.top));
                }
                else
                {
                    VxVector v;
                    ((CK3dEntity *)obj)->GetScale(&v, FALSE);

                    if (optResizeHori)
                        v.x = (drawzone.right - drawzone.left) / 2 + EPSILON;
                    if (optResizeVert || optJustified)
                        v.y = (drawzone.bottom - drawzone.top) / 2 + EPSILON;

                    ((CK3dEntity *)obj)->SetScale(&v, TRUE, FALSE);
                }
            }
            else
            {
                Vx2DVector v;
                ((CK2dEntity *)obj)->GetSize(v);
                if (optResizeHori)
                    v.x = drawzone.right - drawzone.left;
                if (optResizeVert || optJustified)
                    v.y = drawzone.bottom - drawzone.top;
                ((CK2dEntity *)obj)->SetSize(v);
            }
        }
    }

    ///
    // Scroll Vertical
    if (alignTop || alignBottom) // Top|Bottom
    {
        pos.y += m_Offset.y;
    }

    if (reallyDraw)
    {
        // we store the zone of the rectangle to draw
        if (ctdata)
            ctdata->m_DrawZone = backgroundRect;
        ///
        // Background
        if (optBackground)
        {
            DrawFillRectangle(dev, mat, backgroundRect, m_Properties & FONT_LIGHTING, opt3d);
        }

        // States
        SetRenderStates(dev, options);
    }

    // The actual Drawing
    for (int i = 0; i < linecount; ++i)
    {
        // current line
        LineData *data = fontMgr->GetLine(i);
        int drawLen = data->len;
        const CKBOOL isParagraph = (drawLen < 0);
        if (isParagraph)
            drawLen = -drawLen;

        m_SpaceSize = spacesize;
        m_HLeading = hlead;
        // We update the line width
        m_LineWidth = data->stringwidth;

        ///
        // Horizontal Alignment Calculation and spacing
        if (optJustified)
        {
            if (data->nbspace) // there is space, so we work on them
            {
                float delta = data->stringwidth - zonewidth;
                if (delta > 0)
                {
                    m_SpaceSize = spacesize - delta / data->nbspace;
                }
                else
                {
                    m_SpaceSize = spacesize + -delta / data->nbspace;
                }
            }
            else // no space, we adjust leading
            {
                float delta = zonewidth - data->stringwidth;
                if (drawLen > 1)
                {
                    if (delta > 0)
                    {
                        m_HLeading = hlead + delta / (drawLen - 1);
                    }
                    else
                    {
                        m_HLeading = hlead - -delta / (drawLen - 1);
                    }
                }
            }
            // the text has to be on the extreme left side
            pos.x = textZone.left;
        }
        else // The text is not justified, so we have to calculate where it should be horizontally
        {
            if (alignLeft) // Left
            {
                pos.x = textZone.left + m_Offset.x;
            }
            else if (alignRight) // Right
            {
                pos.x = m_Offset.x + textZone.right - data->stringwidth;
            }
            else // Center
            {
                pos.x = (textZone.right + textZone.left) * 0.5f - data->stringwidth * 0.5f;
            }
        }

        if (isParagraph)
        {
            if (alignLeft)
            {
                pos.x += paragraphIndentX;
            }
            if (i != 0)
                pos.y += paragraphIndentY;
        }

        // WARNING quand derniere ligne ou quand ligne qui contient un \n, pas de leading ni de space modifie...
        // peut etre mettre les deux a 0 lors de la creation des lines datas... et retrouver ensuite la vraie longueur
        // while(*string && *string != '\n') len++;
        if (reallyDraw)
        {
            ///
            // Shadow
            if (m_Properties & FONT_SHADOW)
            {
                DrawStringShadowed(dev, data->string, drawLen, pos, textZone, options, ctdata);
            }
            else
            {
                DrawString(dev, data->string, drawLen, pos, textZone, options, ctdata);
            }
        }

        pos.y += verticalspace;
    }
}

void CKTextureFont::DrawStringEx(CKRenderContext *iRC, const char *iString, const VxRect &iTextZone, int iOptions)
{
    if (!iString)
        return;

    VxRect textzone = iTextZone;
    textzone.left += m_Margins.left;
    textzone.right -= m_Margins.right;
    textzone.top += m_Margins.top;
    textzone.bottom -= m_Margins.bottom;

    CKTexture *fonttexture = GetFontTexture();
    if (fonttexture)
        m_ScreenExtents.Set(fonttexture->GetWidth(), fonttexture->GetHeight());

    // Dimensions variables
    float width = m_Scale.x * m_ScreenExtents.x;
    float hlead = m_Leading.x;
    if (m_FontManager->m_VersionUpdate)
    {
        hlead = 0.1f * width * (m_FontCoordinates[' '].uprewidth + m_FontCoordinates[' '].uwidth + m_FontCoordinates[' '].upostwidth) * m_Leading.x;
    }
    float spacesize = width * (m_FontCoordinates[' '].uprewidth + m_FontCoordinates[' '].uwidth + m_FontCoordinates[' '].upostwidth) + hlead;

    VxVector pos;
    pos.x = textzone.left;
    pos.y = textzone.top;

    // States
    SetRenderStates(iRC, iOptions);

    m_SpaceSize = spacesize;
    m_HLeading = hlead;
    // We update the line width
    pos.x = textzone.left + m_Offset.x;

    DrawString(iRC, iString, (int)strlen(iString), pos, textzone, iOptions);
}
