#include <vector>
#include "xmath.h"
#include "Umath.h"
#include "StdAfxRD.h"
#include "VertexFormat.h"
#include "sokol_gfx.h"
#include "SokolResources.h"
#include "IRenderDevice.h"
#include "SokolRender.h"
#include "SokolRenderPipeline.h"
#include "xerrhand.h"
#include "SokolShaders.h"
#include "Texture.h"
#include "DrawBuffer.h"
#include "RenderTracker.h"

int cSokolRender::BeginScene() {
    RenderSubmitEvent(RenderEvent::BEGIN_SCENE, ActiveScene ? "ActiveScene" : "");
    MTG();
    if (ActiveScene) {
        xassert(0);
        return 1;
    }
    ActiveScene = true;

    return cInterfaceRenderDevice::BeginScene();
}

int cSokolRender::EndScene() {
    RenderSubmitEvent(RenderEvent::END_SCENE, ActiveScene ? "" : "NotActiveScene");
    MTG();
    if (!ActiveScene) {
        xassert(0);
        return 1;
    }

    //Make sure there is nothing left to send as command
    FinishCommand();
    
    ActiveScene = false;

    //Begin pass
    sg_pass_action pass_action = {};
    pass_action.colors[0].action = SG_ACTION_CLEAR;
    pass_action.colors[0].value = fill_color;
    pass_action.depth.action = SG_ACTION_CLEAR;
    pass_action.depth.value = 1.0f;
    pass_action.stencil.action = SG_ACTION_CLEAR;
    pass_action.stencil.value = 0;
    sg_begin_default_pass(&pass_action, ScreenSize.x, ScreenSize.y);

    //Iterate each command
    for (auto& command : commands) {
#ifdef PERIMETER_RENDER_TRACKER_COMMANDS
        RenderSubmitEvent(RenderEvent::PROCESS_COMMAND, "", command);
#endif
        //Nothing to draw
        if (3 > command->vertices) {
            xassert(0);
            continue;
        }

        //Apply viewport/clip
        auto& clipPos = command->clipPos;
        auto& clipSize = command->clipSize;
        sg_apply_scissor_rect(clipPos.x, clipPos.y, clipSize.x, clipSize.y, true);
        
        //Get pipeline
        const SokolPipeline* pipeline = pipelines.count(command->pipeline_id) ? pipelines[command->pipeline_id] : nullptr;
        if (pipeline == nullptr) {
            //Not implemented vertex format
            xxassert(0, "cSokolRender::EndScene missing pipeline for " + std::to_string(command->pipeline_id));
            continue;
        }
#if defined(PERIMETER_DEBUG) && 0
        printf("id: 0x%X fmt: 0x%X vtx: %d idx: %d\n",
               command->pipeline_id, pipeline->vertex_fmt,
               command->vertices, command->indices
        );
        printf("tex0 0x%X 0x%X tex1 0x%X 0x%X\n",
               command->textures[0], command->textures[0]?command->textures[0]->image.id:0,
               command->textures[1], command->textures[1]?command->textures[1]->image.id:0
        );
#endif
        shader_funcs* shader_funcs = pipeline->shader_funcs;
        
        //Check amount is correct
        switch (pipeline->type) {
            case PIPELINE_TYPE_TRIANGLE:
                xassert(command->indices % 3 == 0);
                break;
#ifdef PERIMETER_DEBUG
            case PIPELINE_TYPE_LINE_STRIP:
#endif
            case PIPELINE_TYPE_TRIANGLESTRIP:
                break;
            case PIPELINE_TYPE_TERRAIN:
            case PIPELINE_TYPE_MAX:
                xassert(0);
                break;
        }

        //Apply pipeline
        if (sg_query_pipeline_state(pipeline->pipeline) != SG_RESOURCESTATE_VALID) {
            xxassert(0, "cSokolRender::EndScene not valid state");
            continue;
        }
        sg_apply_pipeline(pipeline->pipeline);
        
        //Apply bindings
        sg_bindings bindings = {};
        
        //Bind vertex and index buffer, ensure they are updated
        if (!command->vertex_buffer) {
            xxassert(0, "cSokolRender::EndScene missing vertex_buffer");
            continue;
        }
        if (sg_query_buffer_state(command->vertex_buffer->buffer) != SG_RESOURCESTATE_VALID) {
            xxassert(0, "cSokolRender::EndScene vertex_buffer not valid state");
            continue;
        }
        bindings.vertex_buffers[0] = command->vertex_buffer->buffer;
        xassert(command->indices);
        if (!command->index_buffer) {
            xxassert(0, "cSokolRender::EndScene missing index_buffer");
            continue;
        }
        if (sg_query_buffer_state(command->index_buffer->buffer) != SG_RESOURCESTATE_VALID) {
            xxassert(0, "cSokolRender::EndScene index_buffer not valid state");
            continue;
        }
        bindings.index_buffer = command->index_buffer->buffer;
        
        //Bind images for samplers
        int slot_tex0 = shader_funcs->image_slot(SG_SHADERSTAGE_FS, "un_tex0");
        if (0 <= slot_tex0) {
            SokolTexture2D* tex = emptyTexture;
            if (pipeline->vertex_fmt & VERTEX_FMT_TEX1) {
                tex = command->sokol_textures[0];
            }
            if (tex) {
                if (tex->dirty) tex->update();
                if (sg_query_image_state(tex->image) != SG_RESOURCESTATE_VALID) {
                    xxassert(0, "cSokolRender::EndScene tex0 not valid state");
                    continue;
                }
                bindings.fs_images[slot_tex0] = tex->image;
            } else {
                xxassert(0, "cSokolRender::EndScene tex0 missing");
                continue;
            }
        }
        int slot_tex1 = shader_funcs->image_slot(SG_SHADERSTAGE_FS, "un_tex1");
        if (0 <= slot_tex1) {
            SokolTexture2D* tex = emptyTexture;
            if (pipeline->vertex_fmt & VERTEX_FMT_TEX2) {
                tex = command->sokol_textures[1];
            }
            if (tex) {
                if (tex->dirty) tex->update();
                if (sg_query_image_state(tex->image) != SG_RESOURCESTATE_VALID) {
                    xxassert(0, "cSokolRender::EndScene tex1 not valid state");
                    continue;
                }
                bindings.fs_images[slot_tex1] = tex->image;
            } else {
                xxassert(0, "cSokolRender::EndScene tex1 missing");
                continue;
            }
        }
        sg_apply_bindings(&bindings);
        
        //Apply VS uniforms
        int vs_params_slot = shader_funcs->uniformblock_slot(SG_SHADERSTAGE_VS, "color_texture_vs_params");
        if (0 <= vs_params_slot) {
            color_texture_vs_params_t vs_params;
            if (command->vs_mvp) {
                vs_params.un_mvp = *command->vs_mvp;
            } else {
                xxassert(0, "cSokolRender::EndScene missing mvp");
                continue;
            }
            sg_apply_uniforms(SG_SHADERSTAGE_VS, vs_params_slot, SG_RANGE_REF(vs_params));
            vs_params_slot = -1;
        } else {
            vs_params_slot = shader_funcs->uniformblock_slot(SG_SHADERSTAGE_VS, "normal_texture_vs_params");
        }
        if (0 <= vs_params_slot) {
            normal_texture_vs_params_t vs_params;
            if (command->vs_mvp) {
                vs_params.un_mvp = *command->vs_mvp;
            } else {
                xxassert(0, "cSokolRender::EndScene missing mvp");
                continue;
            }
            sg_apply_uniforms(SG_SHADERSTAGE_VS, vs_params_slot, SG_RANGE_REF(vs_params));
            vs_params_slot = -1;
        }

        //Apply FS uniforms, currently only for shader with 2 texures
        int fs_params_slot = -1;
        if (pipeline->vertex_fmt & VERTEX_FMT_TEX2) {
            fs_params_slot = shader_funcs->uniformblock_slot(SG_SHADERSTAGE_FS, "color_texture_fs_params");
            if (0 <= fs_params_slot) {
                color_texture_fs_params_t fs_params;
                fs_params.un_color_mode = static_cast<int>(command->fs_color_mode);
                fs_params.un_tex2_lerp = command->fs_tex2_lerp;
                switch (command->fs_alpha_test) {
                    default:
                    case ALPHATEST_NONE:
                        fs_params.un_alpha_test = -1.0f;
                        break;
                    case ALPHATEST_GT_0:
                        fs_params.un_alpha_test = 0.0f;
                        break;
                    case ALPHATEST_GT_1:
                        fs_params.un_alpha_test = 1.0f;
                        break;
                    case ALPHATEST_GT_254:
                        fs_params.un_alpha_test = 254.0f;
                        break;
                }
                sg_apply_uniforms(SG_SHADERSTAGE_FS, fs_params_slot, SG_RANGE_REF(fs_params));
                fs_params_slot = -1;
            }
        }

        //Draw
        sg_draw(static_cast<int>(command->base_elements), static_cast<int>(command->indices), 1);
    }

    //End pass
    sg_end_pass();

    return cInterfaceRenderDevice::EndScene();
}

int cSokolRender::Fill(int r, int g, int b, int a) {
    RenderSubmitEvent(RenderEvent::FILL);
    if (ActiveScene) {
        xassert(0);
        EndScene();
    }

#ifdef PERIMETER_DEBUG
    if (r == 0 && g == 0 && b == 0) {
        r = 64;
        g = b = 32;
    }
#endif

    fill_color.r = static_cast<float>(r) / 255.f;
    fill_color.g = static_cast<float>(g) / 255.f;
    fill_color.b = static_cast<float>(b) / 255.f;
    fill_color.a = static_cast<float>(a) / 255.f;

    return 0;
}

int cSokolRender::Flush(bool wnd) {
    RenderSubmitEvent(RenderEvent::FLUSH_SCENE);
    if (!sdl_window) {
        xassert(0);
        return -1;
    }
    if (ActiveScene) {
        xassert(0);
        EndScene();
    }

    sg_commit();

    SDL_GL_SwapWindow(sdl_window);

    ClearCommands();

    xassert(!activeDrawBuffer || !activeDrawBuffer->written_vertices);

    return 0;
}

SokolBuffer* CreateSokolBuffer(MemoryResource* resource, size_t len, bool dynamic, sg_buffer_type type) {
    xassert(!resource->locked);
    xassert(len <= resource->data_len);
    sg_buffer_desc desc = {};
    desc.size = len;
    desc.type = type;
    desc.usage = dynamic ? SG_USAGE_STREAM : SG_USAGE_IMMUTABLE;
    if (type == SG_BUFFERTYPE_VERTEXBUFFER) {
        desc.label = "CreateVertexBuffer";
    } else if (type == SG_BUFFERTYPE_INDEXBUFFER) {
        desc.label = "CreateIndexBuffer";
    } else {
        desc.label = "CreateSokolBuffer";
    }
    if (desc.usage == SG_USAGE_IMMUTABLE) {
        xassert(resource->data);
        xassert(!resource->burned);
        desc.data = {resource->data, len};
        resource->burned = true;
        resource->dirty = false;
    }

    SokolBuffer* buffer = new SokolBuffer(&desc);
    
    return buffer;
}

void cSokolRender::FinishCommand() {
    if (!activeDrawBuffer || !activeDrawBuffer->written_vertices) {
#ifdef PERIMETER_RENDER_TRACKER_COMMANDS
        RenderSubmitEvent(RenderEvent::FINISH_COMMAND, "No/Empty");
#endif
        return;
    }
    
    PIPELINE_TYPE pipelineType = activePipelineType;
#ifdef PERIMETER_DEBUG
    if (WireframeMode) pipelineType = PIPELINE_TYPE_LINE_STRIP;
#endif
    
    pipeline_id_t pipeline_id = GetPipelineID(
            pipelineType,
            activeDrawBuffer->vb.fmt,
            activePipelineMode
    );
    if (pipelines.count(pipeline_id) == 0) {
        RegisterPipeline(pipeline_id);
    }

#ifdef PERIMETER_RENDER_TRACKER_COMMANDS
    std::string label = "Pipeline: " + std::to_string(pipeline_id);
    RenderSubmitEvent(RenderEvent::FINISH_COMMAND, label.c_str());
#endif

#ifdef PERIMETER_DEBUG
    xassert(!activeDrawBuffer->IsLocked());
    if (activeDrawBuffer->vb.fmt & VERTEX_FMT_TEX1) {
        if (activeCommand.sokol_textures[0] != emptyTexture) {
            xassert(activeCommand.sokol_textures[0]);
            xassert(activeCommand.texture_handles[0]);
        }
        
    }
    if (activeDrawBuffer->vb.fmt & VERTEX_FMT_TEX2) {
        xassert(activeCommand.sokol_textures[1]);
        xassert(activeCommand.texture_handles[1]);
    }
#endif
    
    if (0 == activeCommand.vertices && 0 == activeCommand.indices) {
        activeCommand.base_elements = 0;
        activeCommand.vertices = activeDrawBuffer->written_vertices;
        activeCommand.indices = activeDrawBuffer->written_indices;
    }

    //Update buffers
    if (activeDrawBuffer->vb.data) {
        VertexBuffer* vb  = &activeDrawBuffer->vb;
        size_t len = activeDrawBuffer->written_vertices * activeDrawBuffer->vb.VertexSize;
        if (!vb->sg) {
            vb->sg = CreateSokolBuffer(vb, len, vb->dynamic, SG_BUFFERTYPE_VERTEXBUFFER);
        }
        if (vb->dynamic) {
            vb->sg->update(vb, len);
        }
    }
    if (activeDrawBuffer->ib.data) {
        IndexBuffer* ib  = &activeDrawBuffer->ib;
        size_t len = activeDrawBuffer->written_indices * sizeof(indices_t);
        if (!ib->sg) {
            ib->sg = CreateSokolBuffer(ib, len, ib->dynamic, SG_BUFFERTYPE_INDEXBUFFER);
        }
        if (ib->dynamic) {
            ib->sg->update(ib, len);
        }
    }
    
    //Create command to be send
    SokolCommand* cmd = new SokolCommand();
    cmd->pipeline_id = pipeline_id;
    for (int i = 0; i < PERIMETER_SOKOL_TEXTURES; ++i) {
        cmd->SetTexture(i, activeCommand.texture_handles[i], activeCommand.sokol_textures[i]);
    }
    cmd->fs_color_mode = activeCommand.fs_color_mode;
    cmd->fs_tex2_lerp = activeCommand.fs_tex2_lerp;
    cmd->fs_alpha_test = activeCommand.fs_alpha_test;
    cmd->base_elements = activeCommand.base_elements;
    cmd->vertices = activeCommand.vertices;
    cmd->indices = activeCommand.indices;
    cmd->clipPos = activeCommand.clipPos;
    cmd->clipSize = activeCommand.clipSize;
    
    //We copy the MVP
    cmd->owned_mvp = true;
    if (isOrthographicProjSet) {
        cmd->vs_mvp = new Mat4f(orthoVP);
    } else {
        cmd->vs_mvp = new Mat4f(activeCommandW * activeCommandVP);
    }
    
    //Transfer buffers to command
    cmd->owned_vertex_buffer = activeDrawBuffer->vb.dynamic;
    cmd->owned_index_buffer = activeDrawBuffer->ib.dynamic;
    cmd->vertex_buffer = activeDrawBuffer->vb.sg;
    cmd->index_buffer = activeDrawBuffer->ib.sg;
    if (cmd->owned_vertex_buffer) {
        activeDrawBuffer->vb.sg = nullptr;
    }
    if (cmd->owned_index_buffer) {
        activeDrawBuffer->ib.sg = nullptr;
    }
    activeDrawBuffer->PostDraw();
    activeDrawBuffer = nullptr;
    activeCommand.base_elements = 0;
    activeCommand.vertices = 0;
    activeCommand.indices = 0;
    
    //Submit command
    commands.emplace_back(cmd);

#ifdef PERIMETER_RENDER_TRACKER_COMMANDS
    label = "Submit - Pipeline: " + std::to_string(pipeline_id)
            + " ColM: " + std::to_string(activeCommand.fs_color_mode)
            + " OwVB: " + std::to_string(cmd->owned_vertex_buffer)
            + " OwIB: " + std::to_string(cmd->owned_index_buffer)
            + " Vtxs: " + std::to_string(cmd->vertices)
            + " Idxs: " + std::to_string(cmd->indices)
            + " Tex0: " + std::to_string(reinterpret_cast<size_t>(cmd->sokol_textures[0]))
            + " Tex1: " + std::to_string(reinterpret_cast<size_t>(cmd->sokol_textures[1]));
    RenderSubmitEvent(RenderEvent::FINISH_COMMAND, label.c_str(), cmd);
#endif
}

void cSokolRender::SetActiveDrawBuffer(DrawBuffer* db) {
    if (activeDrawBuffer && activeDrawBuffer != db) {
        //Submit previous buffer first
        if (activeDrawBuffer->IsLocked()) {
            activeDrawBuffer->Unlock();
        }
        FinishCommand();
    }
    cInterfaceRenderDevice::SetActiveDrawBuffer(db);
    if (activeDrawBuffer) {
        activePipelineType = getPipelineType(activeDrawBuffer->primitive);
    }
    activeCommand.base_elements = 0;
    activeCommand.vertices = 0;
    activeCommand.indices = 0;
}

void cSokolRender::SubmitDrawBuffer(DrawBuffer* db, DrawBufferRange* range) {
#ifdef PERIMETER_RENDER_TRACKER_DRAW_BUFFER_STATE
    RenderSubmitEvent(RenderEvent::SUBMIT_DRAW_BUFFER, "", db);
#endif
    if (activeDrawBuffer != nullptr && activeDrawBuffer != db) {
        //We need to submit internal render buffer first
        FinishCommand();
    }
    activePipelineType = getPipelineType(db->primitive);
    activeDrawBuffer = db;
    if (activeDrawBuffer) {
        activeCommand.vertices = activeDrawBuffer->written_vertices;
        if (range) {
            activeCommand.base_elements = range->offset;
            activeCommand.indices = range->len;
        } else {
            activeCommand.base_elements = 0;
            activeCommand.indices = activeDrawBuffer->written_indices;
        }
    }
    FinishCommand();
}

void cSokolRender::SetVPMatrix(const Mat4f* matrix) {
#ifdef PERIMETER_RENDER_TRACKER_MATRIX
    RenderSubmitEvent(RenderEvent::SET_VIEWPROJ_MATRIX);
#endif
    if (!matrix) matrix = &Mat4f::ID;
    if (isOrthographicProjSet || !activeCommandVP.eq(*matrix, 0)) {
        FinishCommand();
    }
    isOrthographicProjSet = false;
    activeCommandVP = *matrix;
}

void cSokolRender::SetWorldMat4f(const Mat4f* matrix) {
#ifdef PERIMETER_RENDER_TRACKER_MATRIX
    RenderSubmitEvent(RenderEvent::SET_WORLD_MATRIX);
#endif
    if (!matrix) matrix = &Mat4f::ID;
    if (isOrthographicProjSet || !activeCommandW.eq(*matrix, 0)) {
        FinishCommand();
    }
    isOrthographicProjSet = false;
    activeCommandW = *matrix;
}

void cSokolRender::SetTex2Lerp(float lerp) {
    if (activeCommand.fs_tex2_lerp != lerp) {
        FinishCommand();
    }
    activeCommand.fs_tex2_lerp = lerp;
}

void cSokolRender::UseOrthographicProjection() {
#ifdef PERIMETER_RENDER_TRACKER_MATRIX
    RenderSubmitEvent(RenderEvent::SET_VIEWPROJ_MATRIX, "Orthographic");
#endif
    if (!isOrthographicProjSet) {
        FinishCommand();
    }
    isOrthographicProjSet = true;
}

void cSokolRender::SetColorMode(eColorMode color_mode) {
    if (activeCommand.fs_color_mode != color_mode) {
        FinishCommand();
        activeCommand.fs_color_mode = color_mode;
    }
}

void cSokolRender::SetBlendState(eBlendMode blend) {
    if (activePipelineMode.blend != blend) {
        FinishCommand();
        activePipelineMode.blend = blend;
    }
}

void cSokolRender::SetTextures(float Phase, cTexture* Texture0, cTexture* Texture1) {
    SokolTexture2D* tex0 = emptyTexture;
    SokolTexture2D* tex1 = nullptr;

    if (Texture0) {
        float nAllFrame = static_cast<float>(Texture0->GetNumberFrame());
        int nFrame = 1 < nAllFrame ? static_cast<int>(0.999f * Phase * nAllFrame) : 0;
        tex0 = Texture0->GetFrameImage(nFrame).sg;
    }

    if (Texture1) {
        float nAllFrame = static_cast<float>(Texture1->GetNumberFrame());
        int nFrame = 1 < nAllFrame ? static_cast<int>(0.999f * Phase * nAllFrame) : 0;
        tex1 = Texture1->GetFrameImage(nFrame).sg;
    }

    if (activeCommand.sokol_textures[0] != tex0
     || activeCommand.sokol_textures[1] != tex1) {
        FinishCommand();
        activeCommand.SetTexture(0, Texture0, tex0);
        activeCommand.SetTexture(1, Texture1, tex1);
    }
}

void cSokolRender::SetDrawNode(cCamera *pDrawNode)
{
    if (DrawNode==pDrawNode) return;
    cInterfaceRenderDevice::SetDrawNode(pDrawNode);
    /* TODO
    if (DrawNode->GetRenderTarget()) {
        LPDIRECT3DSURFACE9 pZBuffer=DrawNode->GetZBuffer();
        SetRenderTarget(DrawNode->GetRenderTarget(),pZBuffer);
        uint32_t color=0;
        if(pDrawNode->GetAttribute(ATTRCAMERA_SHADOW))
        {
            if(Option_ShadowType==SHADOW_MAP_SELF &&
               (dtAdvance->GetID()==DT_RADEON8500 ||
                dtAdvance->GetID()==DT_RADEON9700
               ))
            {
                color=D3DCOLOR_RGBA(0,0,0,255);
                kShadow=0.25f;
            }else
            if(CurrentMod4==D3DTOP_MODULATE4X)
            {
                color=D3DCOLOR_RGBA(63,63,63,255);
                kShadow=0.25f;
            }
            else if(CurrentMod4==D3DTOP_MODULATE2X)
            {
                color=D3DCOLOR_RGBA(127,127,127,255);
                kShadow=0.5f;
            }else
            {
                color=D3DCOLOR_RGBA(255,255,255,255);
                kShadow=1.f;
            }
        }else
        {
//			color=D3DCOLOR_RGBA(63,63,63,255);
//			kShadow=0.25f;
            color=D3DCOLOR_RGBA(255,255,255,255);
            kShadow=1.3f;
        }

        if(!pDrawNode->GetAttribute(ATTRCAMERA_NOCLEARTARGET))
        {
            if(!pZBuffer)
            {
                RDCALL(lpD3DDevice->Clear(0,NULL,D3DCLEAR_TARGET,color,1,0));
            }else
            {
                RDCALL(lpD3DDevice->Clear(0,NULL,D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, color,
                                          pDrawNode->GetAttribute(ATTRCAMERA_ZINVERT)?0:1, 0));
            }
        }
    }
    else
    {
        RestoreRenderTarget();
    }
    */

    SetDrawTransform(pDrawNode);
}

void cSokolRender::SetDrawTransform(class cCamera *pDrawNode)
{
#ifdef PERIMETER_RENDER_TRACKER_MATRIX
    RenderSubmitEvent(RenderEvent::SET_VIEWPROJ_MATRIX);
#endif
    FinishCommand();
    SetClipRect(
        pDrawNode->vp.X,
        pDrawNode->vp.Y,
        pDrawNode->vp.X + pDrawNode->vp.Width,
        pDrawNode->vp.Y + pDrawNode->vp.Height
    );
    SetVPMatrix(&pDrawNode->matViewProj);
    activePipelineMode.cull = pDrawNode->GetAttribute(ATTRCAMERA_REFLECTION) == 0 ? CULL_CW : CULL_CCW;
}

uint32_t cSokolRender::GetRenderState(eRenderStateOption option) {
    switch(option) {
        case RS_WIREFRAME:
            return WireframeMode;
        case RS_ZWRITEENABLE:
            return activePipelineMode.depth_write;
        case RS_CULLMODE:
            return activePipelineMode.cull;
        case RS_ALPHA_TEST_MODE:
            return activeCommand.fs_alpha_test;
        case RS_BILINEAR:
            return 1;
    }
    return 0;
}

int cSokolRender::SetRenderState(eRenderStateOption option, uint32_t value) {
    VISASSERT(ActiveScene);
    switch(option) {
        case RS_ZWRITEENABLE: {
            if (value) {
                SetRenderState(RS_CULLMODE, CameraCullMode);
            } else {
                SetRenderState(RS_CULLMODE, CULL_NONE);
            }
            bool state = value != 0;
            if (state != activePipelineMode.depth_write) {
                FinishCommand();
                activePipelineMode.depth_write = state;
            }
            break;
        }
        case RS_ZFUNC: {
            eCMPFUNC depth_cmp = static_cast<eCMPFUNC>(value);
            if (depth_cmp != activePipelineMode.depth_cmp) {
                FinishCommand();
                activePipelineMode.depth_cmp = depth_cmp;
            }
            break;
        }
        case RS_WIREFRAME: {
            bool state = value != 0;
            if (state != WireframeMode) {
                FinishCommand();
                WireframeMode = state;
            }
            break;
        }
        case RS_CULLMODE: {
            eCullMode cull = static_cast<eCullMode>(value);
            if (cull >= CULL_CAMERA) cull = CameraCullMode;
            if (cull != activePipelineMode.cull) {
                FinishCommand();
                activePipelineMode.cull = cull;
            }
            break;
        }
        case RS_ALPHA_TEST_MODE:
            activeCommand.fs_alpha_test = static_cast<eAlphaTestMode>(value);
            break;
        case RS_BILINEAR:
            //Useless as we can't change globally
            break;
    }
    return 0;
}


void cSokolRender::SetGlobalFog(const sColor4f &color,const Vect2f &v) {
    //TODO implement this
}

void cSokolRender::SetGlobalLight(Vect3f *vLight, sColor4f *Ambient, sColor4f *Diffuse, sColor4f *Specular) {
    //TODO implement this
}

void cSokolRender::ClearZBuffer() {
}
