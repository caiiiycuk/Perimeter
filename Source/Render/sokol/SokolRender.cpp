#include "StdAfxRD.h"
#include "xmath.h"
#include "Umath.h"
#include <sokol_gfx.h>
#include <sokol_log.h>
#include "SokolResources.h"
#include "IRenderDevice.h"
#include "SokolRender.h"
#include "SokolRenderPipeline.h"
#include "xerrhand.h"
#include "DrawBuffer.h"
#include "SokolShaders.h"
#include "RenderTracker.h"
#include <SDL_hints.h>

#define SOKOL_IMGUI_NO_SOKOL_APP
#include <util/sokol_imgui.h>
#include <util/sokol_gfx_imgui.h>

#ifdef SOKOL_GL
#include <SDL_opengl.h>
#endif

cSokolRender::cSokolRender() = default;

cSokolRender::~cSokolRender() {
    Done();
};

eRenderDeviceSelection cSokolRender::GetRenderSelection() const {
    return DEVICE_SOKOL;
}

uint32_t cSokolRender::GetWindowCreationFlags() const {
    uint32_t flags = cInterfaceRenderDevice::GetWindowCreationFlags();
#ifdef SOKOL_GL
    flags |= SDL_WINDOW_OPENGL;
#endif
#ifdef SOKOL_METAL
    flags |= SDL_WINDOW_METAL;
#endif
    return flags;
}

extern sg_imgui_t* imgui = new sg_imgui_t{};

int cSokolRender::Init(int xScr, int yScr, int mode, SDL_Window* wnd, int RefreshRateInHz) {
    RenderSubmitEvent(RenderEvent::INIT, "Sokol start");
    int res = cInterfaceRenderDevice::Init(xScr, yScr, mode, wnd, RefreshRateInHz);
    if (res != 0) {
        return res;
    }

    ClearCommands();
    ClearPipelines();
    
    //Init some state
    activePipelineType = PIPELINE_TYPE_TRIANGLE;
    activePipelineMode.cull = CameraCullMode = CULL_CCW;
    
    //Prepare sokol gfx desc
    sg_desc desc = {};
    desc.logger.func = slog_func;
    desc.pipeline_pool_size = PERIMETER_SOKOL_PIPELINES_MAX,
    desc.shader_pool_size = 8,
    desc.buffer_pool_size = 1024 * 8;
    desc.image_pool_size = 1024 * 4; //1024 is enough for PGW+PET game
    desc.context.color_format = SG_PIXELFORMAT_RGBA8;
    desc.context.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;

    //OpenGL / OpenGLES
#ifdef SOKOL_GL
    //Setup some attributes before context creation
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    //Fullscreen anti-aliasing
    //TODO make this configurable, or maybe use sokol?
    /*
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 2);
    //*/

#ifdef SOKOL_GLCORE33
    //Use OpenGL 3.3 Core
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "opengl", SDL_HINT_OVERRIDE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif

#if defined(SOKOL_GLES2) || defined(SOKOL_GLES3)
    //Use OpenGLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    //SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#ifdef SOKOL_GLES2
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "opengles2", SDL_HINT_OVERRIDE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
#else
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "opengles", SDL_HINT_OVERRIDE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
#endif
#endif
#endif

#ifdef SOKOL_D3D11
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "direct3d", SDL_HINT_OVERRIDE);
#endif
    
#ifdef SOKOL_METAL
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "metal", SDL_HINT_OVERRIDE);
#endif

    const char* render_driver = SDL_GetHint(SDL_HINT_RENDER_DRIVER);
    printf("SDL render driver: %s\n", render_driver);

#ifdef SOKOL_METAL
    // Obtain Metal device by creating a SDL Metal View, also useful to retain the Metal device
    sdlMetalView = SDL_Metal_CreateView(sdl_window);
    if (sdlMetalView == nullptr) {
        ErrH.Abort("Error creating SDL Metal View", XERR_CRITICAL, 0, SDL_GetError());
    }
    sokol_metal_setup_desc(sdlMetalView, &desc);
#endif

#ifdef SOKOL_GL
    // Create an OpenGL context associated with the window.
    sdlGlContext = SDL_GL_CreateContext(sdl_window);
    if (sdlGlContext == nullptr) {
        ErrH.Abort("Error creating SDL GL Context", XERR_CRITICAL, 0, SDL_GetError());
    }
    printf("GPU vendor: %s, renderer: %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDER));
#endif
    
    //Call sokol gfx setup
    sg_setup(&desc);
#ifdef PERIMETER_DEBUG
    printf("cSokolRender::Init sg_setup done\n");
#endif

    const simgui_desc_t imgui_desc = {};
    simgui_setup(&imgui_desc);
    const sg_imgui_desc_t sg_imgui_desc = { };
    sg_imgui_init(imgui, &sg_imgui_desc);

    //Create sampler
    sg_sampler_desc sampler_desc = {};
    sampler_desc.label = "SamplerLinear";
    sampler_desc.wrap_u = SG_WRAP_REPEAT;
    sampler_desc.wrap_v = SG_WRAP_REPEAT;
    sampler_desc.min_lod = 0.0f;
    sampler_desc.max_lod = 0.0f;    // for max_lod, zero-initialized means "FLT_MAX"
    //Filter must be linear for small font textures to not look unreadable
    sampler_desc.min_filter = SG_FILTER_NEAREST;
    sampler_desc.mag_filter = SG_FILTER_NEAREST;
    sampler_desc.mipmap_filter = SG_FILTER_NONE;
    sampler = sg_make_sampler(sampler_desc);
    
    //Create empty texture
    sg_image_desc* imgdesc = new sg_image_desc();
    imgdesc->label = nullptr;
    imgdesc->width = imgdesc->height = 64;
    imgdesc->pixel_format = SG_PIXELFORMAT_RGBA8;
    imgdesc->num_mipmaps = 1;
    imgdesc->usage = SG_USAGE_IMMUTABLE;
    size_t pixel_len = sokol_pixelformat_bytesize(imgdesc->pixel_format);
    size_t buf_len = imgdesc->height * imgdesc->width * pixel_len;
    uint8_t* buf = new uint8_t[buf_len];
    memset(buf, 0xFF, buf_len);
    imgdesc->data.subimage[0][0] = { buf, buf_len };
    emptyTexture = new SokolTexture2D(imgdesc);

#ifdef PERIMETER_DEBUG
    emptyTexture->label = "EmptySlotTexture";
#endif

    RenderSubmitEvent(RenderEvent::INIT, "Sokol done");
    return UpdateRenderMode();
}

bool cSokolRender::ChangeSize(int xScr, int yScr, int mode) {
    int mode_mask = RENDERDEVICE_MODE_WINDOW;

    if (xScr == ScreenSize.x && yScr == ScreenSize.y && (RenderMode&mode_mask) == mode) {
        return true; //Nothing to do
    }

    ScreenSize.x = xScr;
    ScreenSize.y = yScr;
    RenderMode &= ~mode_mask;
    RenderMode |= mode;
    
    return UpdateRenderMode() == 0;
}

int cSokolRender::UpdateRenderMode() {
    RenderSubmitEvent(RenderEvent::UPDATE_MODE);
    orthoVP = Mat4f::ID;
    SetOrthographic(orthoVP, ScreenSize.x, -ScreenSize.y, 10, -10);
    return 0;
}

int cSokolRender::Done() {
    RenderSubmitEvent(RenderEvent::DONE, "Sokol start");
    bool do_sg_shutdown = sdl_window != nullptr;
    int ret = cInterfaceRenderDevice::Done();
    activeCommand.Clear();
    ClearCommands();
    ClearPipelines();
    shaders.clear();
    delete emptyTexture;
    emptyTexture = nullptr;
    //At this point sokol stuff should be cleaned up, is important as calling sg_* after this will result in crashes
    //Make sure is called only once, as repeated shutdowns may crash in some backends/OSes
    if (do_sg_shutdown) {
        sg_shutdown();
    }
#ifdef SOKOL_METAL
    if (sdlMetalView != nullptr) {
        SDL_Metal_DestroyView(sdlMetalView);
        sdlMetalView = nullptr;
    }
#endif
#ifdef SOKOL_GL
    if (sdlGlContext != nullptr) {
        SDL_GL_DeleteContext(sdlGlContext);
        sdlGlContext = nullptr;
    }
#endif
    RenderSubmitEvent(RenderEvent::DONE, "Sokol done");
    return ret;
}

int cSokolRender::SetGamma(float fGamma, float fStart, float fFinish) {
    //TODO
    fprintf(stderr, "cSokolRender::SetGamma not implemented!\n");
    return -1;
}

void cSokolRender::DeleteVertexBuffer(VertexBuffer &vb) {
#ifdef PERIMETER_RENDER_TRACKER_RESOURCES
    RenderSubmitEvent(RenderEvent::DELETE_VERTEXBUF, "", &vb);
#endif
    delete vb.sg;
    vb.sg = nullptr;
    vb.FreeData();
}

void cSokolRender::DeleteIndexBuffer(IndexBuffer &ib) {
#ifdef PERIMETER_RENDER_TRACKER_RESOURCES
    RenderSubmitEvent(RenderEvent::DELETE_INDEXBUF, "", &ib);
#endif
    delete ib.sg;
    ib.sg = nullptr;
    ib.FreeData();
}

void cSokolRender::ClearCommands() {
    for (SokolCommand* command : commands) {
        delete command;
    }
    commands.clear();
}

void cSokolRender::ClearPipelines() {
    for (auto pipeline : pipelines) {
        delete pipeline.second;
    }
    pipelines.clear();
}

int cSokolRender::GetClipRect(int *xmin,int *ymin,int *xmax,int *ymax) {
    *xmin = activeCommand.clip[0].x;
    *ymin = activeCommand.clip[0].y;
    *xmax = activeCommand.clip[1].x + activeCommand.clip[0].x;
    *ymax = activeCommand.clip[1].y + activeCommand.clip[0].y;
    return 0;
}

int cSokolRender::SetClipRect(int xmin,int ymin,int xmax,int ymax) {
    int w = xmax-xmin;
    int h = ymax-ymin;
    if (activeCommand.clip[0].x == xmin && activeCommand.clip[0].y == ymin
        && activeCommand.clip[1].x == w && activeCommand.clip[1].y == h) {
        //Nothing to do
        return 0;
    }
    FinishActiveDrawBuffer();
    activeCommand.clip[0].x = xmin;
    activeCommand.clip[0].y = ymin;
    activeCommand.clip[1].x = w;
    activeCommand.clip[1].y = h;
    return 0;
}

void cSokolRender::ResetViewport() {
    if (activeCommand.viewport[0].x == 0
    && activeCommand.viewport[0].y == 0
    && activeCommand.viewport[1] == ScreenSize) {
        //Nothing to do
        return;
    }
    FinishActiveDrawBuffer();
    activeCommand.viewport[0].x = 0;
    activeCommand.viewport[0].y = 0;
    activeCommand.viewport[1] = ScreenSize;;
}

bool cSokolRender::SetScreenShot(const char *fname) {
    //TODO
    fprintf(stderr, "cSokolRender::SetScreenShot not implemented!\n");
    return false;
}

bool cSokolRender::IsEnableSelfShadow() {
    //TODO
    return false;
}

////////////////////////////////////////////////////////////////////////////////////////////

SokolCommand::SokolCommand() {
}

SokolCommand::~SokolCommand() {
    Clear();
}

void SokolCommand::CreateShaderParams() {
    switch (shader_id) {
        default:
        case SOKOL_SHADER_ID_NONE:
            xassert(0);
            break;
        case SOKOL_SHADER_ID_color_tex1:
        case SOKOL_SHADER_ID_color_tex2:
            vs_params = new color_texture_vs_params_t();
            fs_params = new color_texture_fs_params_t();
            vs_params_len = sizeof(color_texture_vs_params_t);            
            fs_params_len = sizeof(color_texture_fs_params_t);
            break;
        case SOKOL_SHADER_ID_normal:
            vs_params = new normal_texture_vs_params_t();
            fs_params = new normal_texture_fs_params_t();
            vs_params_len = sizeof(normal_texture_vs_params_t);            
            fs_params_len = sizeof(normal_texture_fs_params_t);
            break;
        case SOKOL_SHADER_ID_terrain:
            vs_params = new terrain_vs_params_t();
            fs_params = new terrain_fs_params_t();
            vs_params_len = sizeof(terrain_vs_params_t);            
            fs_params_len = sizeof(terrain_fs_params_t);
            break;
    }
}

void SokolCommand::ClearDrawData() {
    if (vertex_buffer) {
        vertex_buffer->DecRef();
        vertex_buffer = nullptr;
    }
    if (index_buffer) {
        index_buffer->DecRef();
        index_buffer = nullptr;
    }
    vertices = 0;
    indices = 0;
    
}

void SokolCommand::ClearShaderParams() {
    switch (shader_id) {
        default:
        case SOKOL_SHADER_ID_NONE:
            break;
        case SOKOL_SHADER_ID_color_tex1:
        case SOKOL_SHADER_ID_color_tex2:
            delete reinterpret_cast<color_texture_vs_params_t*>(vs_params);
            delete reinterpret_cast<color_texture_fs_params_t*>(fs_params);
            break;
        case SOKOL_SHADER_ID_normal:
            delete reinterpret_cast<normal_texture_vs_params_t*>(vs_params);
            delete reinterpret_cast<normal_texture_fs_params_t*>(fs_params);
            break;
        case SOKOL_SHADER_ID_terrain:
            delete reinterpret_cast<terrain_vs_params_t*>(vs_params);
            delete reinterpret_cast<terrain_fs_params_t*>(fs_params);
            break;
    }
    vs_params = nullptr;
    fs_params = nullptr;
    vs_params_len = 0;
    fs_params_len = 0;
}

void SokolCommand::ClearTextures() {
    for (int i = 0; i < PERIMETER_SOKOL_TEXTURES; ++i) {
        SetTexture(i, nullptr);
    }
}

void SokolCommand::Clear() {
    ClearDrawData();
    ClearShaderParams();
    ClearTextures();
}

void SokolCommand::SetTexture(size_t index, SokolResourceTexture* texture) {
    xassert(index<PERIMETER_SOKOL_TEXTURES);
    if (texture) {
        texture->IncRef();
    }
    if (sokol_textures[index]) {
        sokol_textures[index]->DecRef();
    }
    sokol_textures[index] = texture;
}
