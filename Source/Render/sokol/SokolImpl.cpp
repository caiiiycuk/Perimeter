#define SOKOL_TRACE_HOOKS
#define SOKOL_IMGUI_NO_SOKOL_APP

// This file imports sokol header with implementation def set
#include <string>
#include "xerrhand.h"
#define SOKOL_IMPL
#define SOKOL_ASSERT xassert
#include <sokol_gfx.h>
#include <sokol_log.h>

#include <imgui.h>
#include <util/sokol_imgui.h>
#include <util/sokol_gfx_imgui.h>
