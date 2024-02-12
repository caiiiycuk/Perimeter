#include "StdAfxRD.h"
#include "xmath.h"
#include "VertexFormat.h"
#include "IRenderDevice.h"
#include "SokolRender.h"
#include "DrawBuffer.h"
#include "Font.h"
#include "MeshTri.h"
#include "ObjMesh.h"

void cSokolRender::SetNoMaterial(eBlendMode blend, float Phase, cTexture* Texture0, cTexture* Texture1,
                                 eColorMode color_mode) {
    if (Texture0) {
        if (blend == ALPHA_NONE && Texture0->IsAlphaTest()) {
            blend = ALPHA_TEST;
        }
        if (blend <= ALPHA_TEST && Texture0->IsAlpha()) {
            blend = ALPHA_BLEND;
        }
    }

    SetColorMode(color_mode);
    SetBlendState(blend);
    SetTexture(0, Texture0, Phase);
    SetTexture(1, Texture1, Phase);
}

// Sprites / 2d textures

void cSokolRender::DrawSprite2(int x1,int y1,int dx,int dy,
                               float u0,float v0,float du0,float dv0,
                               float u1,float v1,float du1,float dv1,
                               cTexture *Tex1,cTexture *Tex2,float lerp_factor,float alpha,float phase,
                               eColorMode mode,eBlendMode blend_mode) {
    if (!Tex1||!Tex2) return;
    VISASSERT(ActiveScene);
    
    if (blend_mode == ALPHA_NONE && alpha < 1.0f) {
        blend_mode = ALPHA_BLEND;
    }
    
    SetTex2Lerp(lerp_factor);
    SetNoMaterial(blend_mode, phase, Tex2, Tex1, mode);
    
    cInterfaceRenderDevice::DrawSprite2(x1, y1, dx, dy, u0, v0, du0, dv0, u1, v1, du1, dv1, Tex1, Tex2, lerp_factor, alpha, phase, mode, blend_mode);
    SetTex2Lerp(-1);
}

// Text render

void cSokolRender::OutText(int x,int y,const char *string,const sColor4f& color,int align,eBlendMode blend_mode) {    
    SetNoMaterial(blend_mode, 0, CurrentFont->GetTexture());

    cInterfaceRenderDevice::OutText(x, y, string, color, align, blend_mode);
}

void cSokolRender::OutText(int x,int y,const char *string,const sColor4f& color,int align,eBlendMode blend_mode,
                         cTexture* pTexture,eColorMode mode,Vect2f uv,Vect2f duv,float phase,float lerp_factor) {
    SetTex2Lerp(lerp_factor);
    SetNoMaterial(blend_mode, phase, pTexture, CurrentFont->GetTexture(), mode);
    
    cInterfaceRenderDevice::OutText(x, y, string, color, align, blend_mode, pTexture, mode, uv, duv, phase, lerp_factor);
    SetTex2Lerp(-1);
}

// Mesh draw

void cSokolRender::BeginDrawMesh(bool obj_mesh, bool use_shadow) {
}

void cSokolRender::EndDrawMesh() {
}

void cSokolRender::SetSimplyMaterialMesh(cObjMesh* mesh, sDataRenderMaterial* data) {
    bool specular=data->Specular.r>=1/256.0f ||
                  data->Specular.g>=1/256.0f ||
                  data->Specular.b>=1/256.0f;
    if (specular) {
        data->mat |= MAT_COLOR_ADD_SPECULAR;
    } else {
        data->mat &= ~static_cast<uint32_t>(MAT_COLOR_ADD_SPECULAR);
    }

    bool bump = data->mat&MAT_BUMP;
    
    cTexture* Texture0 = data->Tex[0];
    eBlendMode blend=ALPHA_NONE;
    if (Texture0) {
        if (Texture0->IsAlphaTest()) {
            blend = ALPHA_TEST;
        } else if (Texture0->IsAlpha() || data->Diffuse.a < 0.99f) {
            blend = ALPHA_BLEND;
        }
    }

    if (data->mat&MAT_LIGHT) {
        SetMaterial(
                SOKOL_MAT_LIGHT,
                data->Diffuse,
                {data->Ambient.r, data->Ambient.g, data->Ambient.b, 0},
                {data->Specular.r, data->Specular.g, data->Specular.b, 0},
                {data->Emissive.r, data->Emissive.g, data->Emissive.b, 0},
                data->Power
        );
    } else {
        SetMaterial(
                SOKOL_MAT_NONE,
                {0, 0, 0, data->Diffuse.a},
                {data->Diffuse.r, data->Diffuse.g, data->Diffuse.b, 0},
                {0, 0, 0, 0},
                {0, 0, 0, 0},
                0
        );
    }

    if (data->mat&MAT_ALPHA_SUBBLEND) {
        blend = ALPHA_SUBBLEND;
    } else if(data->mat&MAT_ALPHA_ADDBLENDALPHA) {
        blend = ALPHA_ADDBLENDALPHA;
    } else if(data->mat&MAT_ALPHA_ADDBLEND) {
        blend = ALPHA_ADDBLEND;
    }

    SetColorMode(COLOR_MOD);
    SetBlendState(blend);
    SetTexture(0, Texture0, data->MaterialAnimPhase);
    SetTexture(1, bump ? nullptr: data->Tex[1], data->MaterialAnimPhase);
}

extern cObjectNodeRoot* _node_debug;
void cSokolRender::DrawNoMaterialMesh(cObjMesh* mesh, sDataRenderMaterial* data) {
    //TODO SetPointLight(mesh->GetRootNode()->GetLight());

    if (_node_debug) {
        if (std::find(_node_debug->GetMeshChild().begin(), _node_debug->GetMeshChild().end(), mesh) != _node_debug->GetMeshChild().end()) {
            printf("Render\n");
        }
    }

    SetWorldMatXf(mesh->GetGlobalMatrix());
    activeTex0Transform = Mat4f::ID;
    if(data->mat&MAT_TEXMATRIX_STAGE1) {
        MatXf &m=data->TexMatrix;
        activeTex0Transform.xx = m.rot()[0][0],	activeTex0Transform.xy = m.rot()[0][1];
        activeTex0Transform.yx = m.rot()[1][0],	activeTex0Transform.yy = m.rot()[1][1];
        activeTex0Transform.zx = m.trans().x,	activeTex0Transform.zy = m.trans().y;
    }

    activeTex1Transform = Mat4f::ID;
    if(data->mat&MAT_RENDER_SPHEREMAP) { // сферический мапинг
        Mat4f mat;
        memset(&mat,0,sizeof(mat));
        activeTex1Transform.xx=activeTex1Transform.yy=activeTex1Transform.wx=activeTex1Transform.wy=0.5f;
    }

    cMeshTri* Tri = mesh->GetTri();
    SubmitDrawBuffer(Tri->db, &Tri->dbr);
}

void cSokolRender::BeginDrawShadow(bool shadow_map) {
    //TODO
}

void cSokolRender::EndDrawShadow() {
    //TODO
}

void cSokolRender::SetSimplyMaterialShadow(cObjMesh* mesh, cTexture* texture) {
    //TODO
}

void cSokolRender::DrawNoMaterialShadow(cObjMesh* mesh) {
    //TODO
}

void cSokolRender::SetMaterialTilemap(cTileMap *TileMap) {
    //TODO
}

void cSokolRender::SetMaterialTilemapShadow() {
    //TODO
}

void cSokolRender::SetTileColor(sColor4f color) {
    if (activeCommandTileColor != color) {
        FinishActiveDrawBuffer();
        activeCommandTileColor = color;
    }
}
