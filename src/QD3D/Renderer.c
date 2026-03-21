// RENDERER.C
// (C)2021 Iliyas Jorio
// This file is part of Bugdom. https://github.com/jorio/bugdom


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"
#include <SDL3/SDL.h>
#include <QD3D.h>
#if defined(__EMSCRIPTEN__)
#include "gles3_rhi.h"
#endif

#pragma mark -

/****************************/
/*    PROTOTYPES            */
/****************************/

typedef struct RendererState
{
	GLuint		boundTexture;
	bool		hasClientState_GL_TEXTURE_COORD_ARRAY;
	bool		hasClientState_GL_VERTEX_ARRAY;
	bool		hasClientState_GL_COLOR_ARRAY;
	bool		hasClientState_GL_NORMAL_ARRAY;
	bool		hasState_GL_NORMALIZE;
	bool		hasState_GL_CULL_FACE;
	bool		hasState_GL_ALPHA_TEST;
	bool		hasState_GL_DEPTH_TEST;
	bool		hasState_GL_COLOR_MATERIAL;
	bool		hasState_GL_TEXTURE_2D;
	bool		hasState_GL_BLEND;
	bool		hasState_GL_LIGHTING;
	bool		hasState_GL_FOG;
	bool		hasFlag_glDepthMask;
	bool		blendFuncIsAdditive;
	bool		sceneHasFog;
	GLboolean	wantColorMask;
	const TQ3Matrix4x4*	currentTransform;
} RendererState;

typedef struct MeshQueueEntry
{
	const TQ3TriMeshData*	mesh;
	const TQ3Matrix4x4*		transform;	// may be NULL
	const RenderModifiers*	mods;		// may be NULL
	float					depth;		// used to determine draw order
	bool					meshIsTransparent;
} MeshQueueEntry;

#define MESHQUEUE_MAX_SIZE 4096

static MeshQueueEntry		gMeshQueueEntryPool[MESHQUEUE_MAX_SIZE];
static MeshQueueEntry*		gMeshQueuePtrs[MESHQUEUE_MAX_SIZE];
static int					gMeshQueueSize = 0;
static bool					gFrameStarted = false;

static float				gBackupVertexColors[4*65536];

#if defined(__EMSCRIPTEN__)
/*
 * Emscripten's WebGL2 C bindings do not link glGetTexLevelParameteriv. Track 2D
 * texture dimensions for anything created via Render_LoadTexture (the only
 * allocation path in this project) so Render_UpdateTexture can clamp sub-rects.
 */
#define RENDER_EMSCRIPTEN_TEX_DIM_MAX 512
typedef struct
{
	GLuint	name;
	int		w;
	int		h;
} RenderEmscriptenTexDimEntry;

static RenderEmscriptenTexDimEntry	gEmscriptenTex2DSize[RENDER_EMSCRIPTEN_TEX_DIM_MAX];
static int							gEmscriptenTex2DSizeCount;

static void Render_Emscripten_RecordTex2DSize(GLuint name, int w, int h)
{
	int i;
	for (i = 0; i < gEmscriptenTex2DSizeCount; i++)
	{
		if (gEmscriptenTex2DSize[i].name == name)
		{
			gEmscriptenTex2DSize[i].w = w;
			gEmscriptenTex2DSize[i].h = h;
			return;
		}
	}
	if (gEmscriptenTex2DSizeCount < RENDER_EMSCRIPTEN_TEX_DIM_MAX)
	{
		gEmscriptenTex2DSize[gEmscriptenTex2DSizeCount].name = name;
		gEmscriptenTex2DSize[gEmscriptenTex2DSizeCount].w = w;
		gEmscriptenTex2DSize[gEmscriptenTex2DSizeCount].h = h;
		gEmscriptenTex2DSizeCount++;
	}
}

static void Render_Emscripten_LookupTex2DSize(GLuint name, int* outW, int* outH)
{
	int i;
	for (i = 0; i < gEmscriptenTex2DSizeCount; i++)
	{
		if (gEmscriptenTex2DSize[i].name == name)
		{
			*outW = gEmscriptenTex2DSize[i].w;
			*outH = gEmscriptenTex2DSize[i].h;
			return;
		}
	}
	*outW = 0x7fffffff;
	*outH = 0x7fffffff;
}
#endif

static int DrawOrderComparator(void const* a_void, void const* b_void);

static void BeginShadingPass(const MeshQueueEntry* entry);
static void PrepareOpaqueShading(const MeshQueueEntry* entry);
static void PrepareAlphaShading(const MeshQueueEntry* entry);
#if !defined(__EMSCRIPTEN__)
static void BeginDepthPass(const MeshQueueEntry* entry);
static void SendGeometry(const MeshQueueEntry* entry);
#endif


#pragma mark -

/****************************/
/*    CONSTANTS             */
/****************************/

const TQ3Point3D kQ3Point3D_Zero = {0, 0, 0};

static const RenderModifiers kDefaultRenderMods =
{
	.statusBits = 0,
	.diffuseColor = {1,1,1,1},
	.autoFadeFactor = 1.0f,
	.drawOrder = 0,
};

const RenderModifiers kDefaultRenderMods_UI =
{
	.statusBits = STATUS_BIT_NULLSHADER | STATUS_BIT_NOFOG | STATUS_BIT_NOZWRITE,
	.diffuseColor = {1,1,1,1},
	.autoFadeFactor = 1.0f,
	.drawOrder = kDrawOrder_UI
};

static const RenderModifiers kDefaultRenderMods_FadeOverlay =
{
	.statusBits = STATUS_BIT_NULLSHADER | STATUS_BIT_NOFOG | STATUS_BIT_NOZWRITE,
	.diffuseColor = {1,1,1,1},
	.autoFadeFactor = 1.0f,
	.drawOrder = kDrawOrder_FadeOverlay
};

const RenderModifiers kDefaultRenderMods_DebugUI =
{
	.statusBits = STATUS_BIT_NULLSHADER | STATUS_BIT_NOFOG | STATUS_BIT_NOZWRITE | STATUS_BIT_KEEPBACKFACES | STATUS_BIT_DONTCULL,
	.diffuseColor = {1,1,1,1},
	.autoFadeFactor = 1.0f,
	.drawOrder = kDrawOrder_DebugUI
};

const RenderModifiers kDefaultRenderMods_Pillarbox =
{
	.statusBits = STATUS_BIT_NULLSHADER | STATUS_BIT_NOFOG | STATUS_BIT_NOZWRITE | STATUS_BIT_KEEPBACKFACES | STATUS_BIT_DONTCULL,
	.diffuseColor = {0,0,0,1},
	.autoFadeFactor = 1.0f,
	.drawOrder = kDrawOrder_DebugUI
};

#pragma mark -

/****************************/
/*    VARIABLES             */
/****************************/

static SDL_GLContext gGLContext = NULL;

static RendererState gState;

float gGammaFadeFactor = 1.0f;

static TQ3TriMeshData* gFullscreenQuad = nil;

#pragma mark -

/****************************/
/*    MACROS/HELPERS        */
/****************************/

#if !defined(__EMSCRIPTEN__)

static void __SetInitialState(GLenum stateEnum, bool* stateFlagPtr, bool initialValue)
{
	*stateFlagPtr = initialValue;
	if (initialValue)
		glEnable(stateEnum);
	else
		glDisable(stateEnum);
	CHECK_GL_ERROR();
}

static void __SetInitialClientState(GLenum stateEnum, bool* stateFlagPtr, bool initialValue)
{
	*stateFlagPtr = initialValue;
	if (initialValue)
		glEnableClientState(stateEnum);
	else
		glDisableClientState(stateEnum);
	CHECK_GL_ERROR();
}

static inline void __SetState(GLenum stateEnum, bool* stateFlagPtr, bool enable)
{
	if (enable != *stateFlagPtr)
	{
		if (enable)
			glEnable(stateEnum);
		else
			glDisable(stateEnum);
		*stateFlagPtr = enable;
	}
}

static inline void __SetClientState(GLenum stateEnum, bool* stateFlagPtr, bool enable)
{
	if (enable != *stateFlagPtr)
	{
		if (enable)
			glEnableClientState(stateEnum);
		else
			glDisableClientState(stateEnum);
		*stateFlagPtr = enable;
	}
}

#define SetInitialState(stateEnum, initialValue) __SetInitialState(stateEnum, &gState.hasState_##stateEnum, initialValue)
#define SetInitialClientState(stateEnum, initialValue) __SetInitialClientState(stateEnum, &gState.hasClientState_##stateEnum, initialValue)

#define SetState(stateEnum, value) __SetState(stateEnum, &gState.hasState_##stateEnum, (value))

#define EnableState(stateEnum) __SetState(stateEnum, &gState.hasState_##stateEnum, true)
#define EnableClientState(stateEnum) __SetClientState(stateEnum, &gState.hasClientState_##stateEnum, true)

#define DisableState(stateEnum) __SetState(stateEnum, &gState.hasState_##stateEnum, false)
#define DisableClientState(stateEnum) __SetClientState(stateEnum, &gState.hasClientState_##stateEnum, false)

#define RestoreStateFromBackup(stateEnum, backup) __SetState(stateEnum, &gState.hasState_##stateEnum, (backup)->hasState_##stateEnum)
#define RestoreClientStateFromBackup(stateEnum, backup) __SetClientState(stateEnum, &gState.hasClientState_##stateEnum, (backup)->hasClientState_##stateEnum)

#define SetFlag(glFunction, value) do {				\
	if ((value) != gState.hasFlag_##glFunction) {	\
		glFunction((value)? GL_TRUE: GL_FALSE);		\
		gState.hasFlag_##glFunction = (value);		\
	} } while(0)

static inline void SetColorMask(GLboolean enable)
{
	if (enable != gState.wantColorMask)
	{
		glColorMask(enable, enable, enable, enable);
		gState.wantColorMask = enable;
	}
}

#else /* __EMSCRIPTEN__ */

static inline void SetColorMask(GLboolean enable)
{
	glColorMask(enable, enable, enable, enable);
}

#endif /* !__EMSCRIPTEN__ */

#pragma mark -

//=======================================================================================================

/****************************/
/*    API IMPLEMENTATION    */
/****************************/

#if _DEBUG
void DoFatalGLError(GLenum error, const char* file, int line)
{
	static char alertbuf[1024];
	SDL_snprintf(alertbuf, sizeof(alertbuf), "OpenGL error 0x%x\nin %s:%d", error, file, line);
	DoFatalAlert(alertbuf);
}
#endif

void Render_CreateContext(void)
{
#if defined(__EMSCRIPTEN__)
	gGLContext = SDL_GL_GetCurrentContext();
	if (!gGLContext)
		gGLContext = SDL_GL_CreateContext(gSDLWindow);
#else
	gGLContext = SDL_GL_CreateContext(gSDLWindow);
#endif

	GAME_ASSERT(gGLContext);

#if defined(__EMSCRIPTEN__)
	SDL_GL_MakeCurrent(gSDLWindow, gGLContext);
	glActiveTexture(GL_TEXTURE0);
	GLES3_Init();
#endif

	// On Windows, proc addresses are only valid for the current context,
	// so we must get proc addresses everytime we recreate the context.
	//Render_GetGLProcAddresses();
}

void Render_DeleteContext(void)
{
#if defined(__EMSCRIPTEN__)
	GLES3_Shutdown();
#endif
	if (gGLContext)
	{
		SDL_GL_DestroyContext(gGLContext);
		gGLContext = NULL;
	}
}

void Render_SetDefaultModifiers(RenderModifiers* dest)
{
	SDL_memcpy(dest, &kDefaultRenderMods, sizeof(RenderModifiers));
}

void Render_InitState(const TQ3ColorRGBA* clearColor)
{
#if !defined(__EMSCRIPTEN__)
	glActiveTexture(GL_TEXTURE0);

	SetInitialClientState(GL_VERTEX_ARRAY,				true);
	SetInitialClientState(GL_NORMAL_ARRAY,				false);
	SetInitialClientState(GL_COLOR_ARRAY,				false);
	SetInitialClientState(GL_TEXTURE_COORD_ARRAY,		false);
	SetInitialState(GL_NORMALIZE,		true);		// Normalize normal vectors. Required so lighting looks correct on scaled meshes.
	SetInitialState(GL_CULL_FACE,		true);
	SetInitialState(GL_ALPHA_TEST,		true);
	SetInitialState(GL_DEPTH_TEST,		true);
	SetInitialState(GL_COLOR_MATERIAL,	true);
	SetInitialState(GL_TEXTURE_2D,		false);
	SetInitialState(GL_BLEND,			false);
	SetInitialState(GL_LIGHTING,		true);
	SetInitialState(GL_FOG,				false);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gState.blendFuncIsAdditive = false;		// must match glBlendFunc call above!
	
	glDepthMask(true);
	gState.hasFlag_glDepthMask = true;		// must match glDepthMask call above!

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	gState.wantColorMask = true;			// must match glColorMask call above!

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
#else
	glActiveTexture(GL_TEXTURE0);
	GLES3_InitPipelineState(clearColor->r, clearColor->g, clearColor->b);
	gState.blendFuncIsAdditive = false;
	gState.boundTexture = 0;
#endif
	
	gState.sceneHasFog = false;
	gState.currentTransform = NULL;

	glClearColor(clearColor->r, clearColor->g, clearColor->b, 1.0f);
	
#if !defined(__EMSCRIPTEN__)
	// Set misc GL defaults that apply throughout the entire game
	glAlphaFunc(GL_GREATER, 0.4999f);
	glFrontFace(GL_CCW);
	glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
#endif

	// Set up mesh queue
	gMeshQueueSize = 0;
	SDL_memset(gMeshQueuePtrs, 0, sizeof(gMeshQueuePtrs));

	// Set up fullscreen overlay quad
	if (!gFullscreenQuad)
	{
		gFullscreenQuad = MakeQuadMesh_UI(0, 0, GAME_VIEW_WIDTH, GAME_VIEW_HEIGHT, 0, 0, 1, 1);
	}

	// Clear the buffers
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	CHECK_GL_ERROR();
}

void Render_EndScene(void)
{
	if (gFullscreenQuad)
	{
		Q3TriMeshData_Dispose(gFullscreenQuad);
		gFullscreenQuad = NULL;
	}
}

void Render_EnableFog(
		float camHither,
		float camYon,
		float fogHither,
		float fogYon,
		TQ3ColorRGBA fogColor)
{
	(void) camHither;

#if !defined(__EMSCRIPTEN__)
	glHint(GL_FOG_HINT,		GL_NICEST);
	glFogi(GL_FOG_MODE,		GL_LINEAR);
	glFogf(GL_FOG_START,	fogHither * camYon);
	glFogf(GL_FOG_END,		fogYon * camYon);
	glFogfv(GL_FOG_COLOR,	&fogColor.r);
#else
	GLES3_SetFog(true, fogHither * camYon, fogYon * camYon, fogColor.r, fogColor.g, fogColor.b);
#endif
	gState.sceneHasFog = true;
}

void Render_DisableFog(void)
{
#if defined(__EMSCRIPTEN__)
	GLES3_SetFog(false, 0.f, 1.f, 0.f, 0.f, 0.f);
#endif
	gState.sceneHasFog = false;
}

#pragma mark -

void Render_BindTexture(GLuint textureName)
{
	if (gState.boundTexture != textureName)
	{
		glBindTexture(GL_TEXTURE_2D, textureName);
		gState.boundTexture = textureName;
	}
}

void Render_InvalidateTextureCache(void)
{
	gState.boundTexture = 0;
}

GLuint Render_LoadTexture(
		GLenum internalFormat,
		int width,
		int height,
		GLenum bufferFormat,
		GLenum bufferType,
		const GLvoid* pixels,
		RendererTextureFlags flags)
{
	GAME_ASSERT(gGLContext);

	GLuint textureName;

	glGenTextures(1, &textureName);
	CHECK_GL_ERROR();

	Render_BindTexture(textureName);				// this is now the currently active texture
	CHECK_GL_ERROR();

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, !gGamePrefs.lowDetail? GL_LINEAR: GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, !gGamePrefs.lowDetail? GL_LINEAR: GL_NEAREST);

	if (flags & kRendererTextureFlags_ClampU)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

	if (flags & kRendererTextureFlags_ClampV)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

#if defined(__EMSCRIPTEN__)
	/* WebGL doesn't support GL_BGRA, GL_BGR, GL_UNSIGNED_INT_8_8_8_8_REV,
	   GL_UNSIGNED_SHORT_1_5_5_5_REV. Convert to RGBA/RGB + UNSIGNED_BYTE.
	   When uploading RGBA data, internalFormat must be GL_RGBA (WebGL rejects
	   GL_RGB internalFormat with GL_RGBA format). */
	GLenum uploadInternalFormat = internalFormat;
	GLenum uploadFormat = bufferFormat;
	GLenum uploadType = bufferType;
	const GLvoid* uploadPixels = pixels;
	void* convertedBuffer = NULL;

	if ((bufferFormat == 0x80E1 /* GL_BGRA */ && bufferType == 0x8367 /* GL_UNSIGNED_INT_8_8_8_8_REV */) ||
	    (bufferFormat == 0x80E1 && bufferType == 0x8366 /* GL_UNSIGNED_SHORT_1_5_5_5_REV */))
	{
		uploadInternalFormat = GL_RGBA;
		uploadFormat = GL_RGBA;
		uploadType = GL_UNSIGNED_BYTE;
		if (bufferType == 0x8367)
		{
			/* ARGB -> RGBA (swap component order) */
			convertedBuffer = AllocPtr((size_t)width * height * 4);
			const uint8_t* src = (const uint8_t*)pixels;
			uint8_t* dst = (uint8_t*)convertedBuffer;
			for (int i = 0; i < width * height; i++)
			{
				dst[i*4+0] = src[i*4+1]; dst[i*4+1] = src[i*4+2];
				dst[i*4+2] = src[i*4+3]; dst[i*4+3] = src[i*4+0];
			}
			uploadPixels = convertedBuffer;
		}
		else
		{
			/* 16-bit 1555 -> RGBA8 */
			convertedBuffer = AllocPtr((size_t)width * height * 4);
			const uint16_t* src = (const uint16_t*)pixels;
			uint8_t* dst = (uint8_t*)convertedBuffer;
			for (int i = 0; i < width * height; i++)
			{
				uint16_t w = src[i];
				dst[i*4+0] = (uint8_t)(((w >> 10) & 0x1F) * 255 / 31);
				dst[i*4+1] = (uint8_t)(((w >> 5) & 0x1F) * 255 / 31);
				dst[i*4+2] = (uint8_t)((w & 0x1F) * 255 / 31);
				dst[i*4+3] = (uint8_t)(((w >> 15) & 1) * 255);
			}
			uploadPixels = convertedBuffer;
		}
	}
	else if (bufferFormat == 0x80E0 /* GL_BGR */ && bufferType == GL_UNSIGNED_BYTE)
	{
		uploadFormat = GL_RGB;
		uploadType = GL_UNSIGNED_BYTE;
		convertedBuffer = AllocPtr((size_t)width * height * 3);
		const uint8_t* src = (const uint8_t*)pixels;
		uint8_t* dst = (uint8_t*)convertedBuffer;
		for (int i = 0; i < width * height; i++)
		{
			dst[i*3+0] = src[i*3+2];
			dst[i*3+1] = src[i*3+1];
			dst[i*3+2] = src[i*3+0];
		}
		uploadPixels = convertedBuffer;
	}

	/* WebGL requires internalFormat to match format. Use sized format for compatibility. */
	if (uploadFormat == GL_RGBA)
		uploadInternalFormat = 0x8058; /* GL_RGBA8 - explicit sized format for WebGL2 */
	else if (uploadFormat == GL_RGB)
		uploadInternalFormat = 0x8051; /* GL_RGB8 */

	glTexImage2D(GL_TEXTURE_2D, 0, uploadInternalFormat, width, height, 0,
		    uploadFormat, uploadType, uploadPixels);
	if (convertedBuffer)
		DisposePtr((Ptr)convertedBuffer);
	Render_Emscripten_RecordTex2DSize(textureName, width, height);
#else
	glTexImage2D(
			GL_TEXTURE_2D,
			0,						// mipmap level
			internalFormat,			// format in OpenGL
			width,					// width in pixels
			height,					// height in pixels
			0,						// border
			bufferFormat,			// what my format is
			bufferType,				// size of each r,g,b
			pixels);				// pointer to the actual texture pixels
#endif
	CHECK_GL_ERROR();

	return textureName;
}

void Render_UpdateTexture(
		GLuint textureName,
		int x,
		int y,
		int width,
		int height,
		GLenum bufferFormat,
		GLenum bufferType,
		const GLvoid* pixels,
		int rowBytesInInput)
{
	glBindTexture(GL_TEXTURE_2D, textureName);
	gState.boundTexture = textureName;

#if !defined(__EMSCRIPTEN__)
	GLint pUnpackRowLength = 0;
	// Set unpack row length (if valid rowbytes input given)
	if (rowBytesInInput > 0)
	{
		glGetIntegerv(GL_UNPACK_ROW_LENGTH, &pUnpackRowLength);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, rowBytesInInput);
	}
#endif

#if defined(__EMSCRIPTEN__)
	CHECK_GL_ERROR();

	if (width <= 0 || height <= 0 || pixels == NULL)
		return;

	const int srcRowPixels = (rowBytesInInput > 0) ? rowBytesInInput : width;

	int texW = 0;
	int texH = 0;
	Render_Emscripten_LookupTex2DSize(textureName, &texW, &texH);
	if (texW <= 0 || texH <= 0)
		return;

	int rx = x;
	int ry = y;
	int rw = width;
	int rh = height;
	if (rx < 0)
	{
		rw += rx;
		rx = 0;
	}
	if (ry < 0)
	{
		rh += ry;
		ry = 0;
	}
	if (rx + rw > texW)
		rw = texW - rx;
	if (ry + rh > texH)
		rh = texH - ry;
	if (rw <= 0 || rh <= 0)
		return;

	const int dx = rx - x;
	const int dy = ry - y;

	/* GLES headers may use different token values than our renderer.h aliases — keep hex fallbacks. */
	const int isBGRAfmt = (bufferFormat == GL_BGRA || bufferFormat == (GLenum)0x80E1);
	const int isBGRfmt = (bufferFormat == GL_BGR || bufferFormat == (GLenum)0x80E0);

	int srcBpp = 4;
	if (isBGRfmt && bufferType == GL_UNSIGNED_BYTE)
		srcBpp = 3;
	else if (isBGRAfmt && bufferType == GL_UNSIGNED_SHORT_1_5_5_5_REV)
		srcBpp = 2;
	else if (isBGRAfmt && bufferType == GL_UNSIGNED_INT_8_8_8_8_REV)
		srcBpp = 4;

	const uint8_t* srcBase = (const uint8_t*)pixels
			+ (size_t)dy * (size_t)srcRowPixels * (size_t)srcBpp
			+ (size_t)dx * (size_t)srcBpp;

	/* Same format conversion as Render_LoadTexture for WebGL compatibility. */
	GLenum uploadFormat = bufferFormat;
	GLenum uploadType = bufferType;
	const GLvoid* uploadPixels = srcBase;
	void* convertedBuffer = NULL;

	if ((isBGRAfmt && bufferType == GL_UNSIGNED_INT_8_8_8_8_REV) ||
	    (isBGRAfmt && bufferType == GL_UNSIGNED_SHORT_1_5_5_5_REV))
	{
		uploadFormat = GL_RGBA;
		uploadType = GL_UNSIGNED_BYTE;
		convertedBuffer = AllocPtr((size_t)rw * rh * 4);
		uint8_t* dst = (uint8_t*)convertedBuffer;
		if (bufferType == GL_UNSIGNED_INT_8_8_8_8_REV)
		{
			for (int j = 0; j < rh; j++)
			{
				const uint8_t* row = srcBase + (size_t)j * (size_t)srcRowPixels * 4;
				for (int i = 0; i < rw; i++)
				{
					size_t o = (size_t)j * (size_t)rw + (size_t)i;
					dst[o * 4 + 0] = row[i * 4 + 1];
					dst[o * 4 + 1] = row[i * 4 + 2];
					dst[o * 4 + 2] = row[i * 4 + 3];
					dst[o * 4 + 3] = row[i * 4 + 0];
				}
			}
		}
		else
		{
			for (int j = 0; j < rh; j++)
			{
				const uint16_t* row = (const uint16_t*)(srcBase + (size_t)j * (size_t)srcRowPixels * 2);
				for (int i = 0; i < rw; i++)
				{
					uint16_t wv = row[i];
					size_t o = (size_t)j * (size_t)rw + (size_t)i;
					dst[o * 4 + 0] = (uint8_t)(((wv >> 10) & 0x1F) * 255 / 31);
					dst[o * 4 + 1] = (uint8_t)(((wv >> 5) & 0x1F) * 255 / 31);
					dst[o * 4 + 2] = (uint8_t)((wv & 0x1F) * 255 / 31);
					dst[o * 4 + 3] = (uint8_t)(((wv >> 15) & 1) * 255);
				}
			}
		}
		uploadPixels = convertedBuffer;
	}
	else if (isBGRfmt && bufferType == GL_UNSIGNED_BYTE)
	{
		uploadFormat = GL_RGB;
		uploadType = GL_UNSIGNED_BYTE;
		convertedBuffer = AllocPtr((size_t)rw * rh * 3);
		uint8_t* dst = (uint8_t*)convertedBuffer;
		for (int j = 0; j < rh; j++)
		{
			const uint8_t* row = srcBase + (size_t)j * (size_t)srcRowPixels * 3;
			for (int i = 0; i < rw; i++)
			{
				size_t o = (size_t)j * (size_t)rw + (size_t)i;
				dst[o * 3 + 0] = row[i * 3 + 2];
				dst[o * 3 + 1] = row[i * 3 + 1];
				dst[o * 3 + 2] = row[i * 3 + 0];
			}
		}
		uploadPixels = convertedBuffer;
	}

	x = rx;
	y = ry;
	width = rw;
	height = rh;

	/*
	 * WebGL2 often rejects glTexSubImage2D with GL_UNPACK_ROW_LENGTH (GL_INVALID_VALUE).
	 * Copy strided CPU rows into a tight buffer and upload with default pixel store state.
	 */
	void* tightRows = NULL;
	if (rowBytesInInput > 0 && uploadPixels == srcBase)
	{
		int bpp = (uploadFormat == GL_RGB && uploadType == GL_UNSIGNED_BYTE) ? 3 : 4;
		tightRows = AllocPtr((size_t)width * height * (size_t)bpp);
		const uint8_t* src = (const uint8_t*)uploadPixels;
		uint8_t* dst = (uint8_t*)tightRows;
		for (int r = 0; r < height; r++)
			SDL_memcpy(
					dst + (size_t)r * (size_t)width * (size_t)bpp,
					src + (size_t)r * (size_t)rowBytesInInput * (size_t)bpp,
					(size_t)width * (size_t)bpp);
		uploadPixels = tightRows;
	}

	GLint prevUnpackRow = 0;
	GLint prevUnpackAlign = 4;
	glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevUnpackRow);
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlign);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height,
			uploadFormat, uploadType, uploadPixels);

	CHECK_GL_ERROR();

	glPixelStorei(GL_UNPACK_ROW_LENGTH, prevUnpackRow);
	glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlign);

	if (tightRows)
		DisposePtr((Ptr)tightRows);
	if (convertedBuffer)
		DisposePtr((Ptr)convertedBuffer);
#else
	glTexSubImage2D(
			GL_TEXTURE_2D,
			0,
			x,
			y,
			width,
			height,
			bufferFormat,
			bufferType,
			pixels);
#endif
	CHECK_GL_ERROR();

#if !defined(__EMSCRIPTEN__)
	// Restore unpack row length
	if (rowBytesInInput > 0)
		glPixelStorei(GL_UNPACK_ROW_LENGTH, pUnpackRowLength);
#endif
}

void Render_Load3DMFTextures(TQ3MetaFile* metaFile, GLuint* outTextureNames, bool forceClampUVs)
{
	for (int i = 0; i < metaFile->numTextures; i++)
	{
		TQ3TextureShader* textureShader = &metaFile->textures[i];

		GAME_ASSERT(textureShader->pixmap);

		TQ3TexturingMode meshTexturingMode = kQ3TexturingModeOff;
		GLenum internalFormat;
		GLenum format;
		GLenum type;
		switch (textureShader->pixmap->pixelType)
		{
			case kQ3PixelTypeRGB32:
				meshTexturingMode = kQ3TexturingModeOpaque;
				internalFormat = GL_RGB;
				format = GL_BGRA;
				type = GL_UNSIGNED_INT_8_8_8_8_REV;
				break;
			case kQ3PixelTypeARGB32:
				meshTexturingMode = kQ3TexturingModeAlphaBlend;
				internalFormat = GL_RGBA;
				format = GL_BGRA;
				type = GL_UNSIGNED_INT_8_8_8_8_REV;
				break;
			case kQ3PixelTypeRGB16:
				meshTexturingMode = kQ3TexturingModeOpaque;
				internalFormat = GL_RGB;
				format = GL_BGRA;
				type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
				break;
			case kQ3PixelTypeARGB16:
				meshTexturingMode = kQ3TexturingModeAlphaTest;
				internalFormat = GL_RGBA;
				format = GL_BGRA;
				type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
				break;
			case kQ3PixelTypeRGB24:
				meshTexturingMode = kQ3TexturingModeOpaque;
				internalFormat = GL_RGB;
				format = GL_BGR;
				type = GL_UNSIGNED_BYTE;
				break;
			default:
				DoAlert("3DMF texture: Unsupported kQ3PixelType");
				continue;
		}

		int clampFlags = forceClampUVs ? kRendererTextureFlags_ClampBoth : 0;
		if (textureShader->boundaryU == kQ3ShaderUVBoundaryClamp)
			clampFlags |= kRendererTextureFlags_ClampU;
		if (textureShader->boundaryV == kQ3ShaderUVBoundaryClamp)
			clampFlags |= kRendererTextureFlags_ClampV;

		outTextureNames[i] = Render_LoadTexture(
					 internalFormat,						// format in OpenGL
					 textureShader->pixmap->width,			// width in pixels
					 textureShader->pixmap->height,			// height in pixels
					 format,								// what my format is
					 type,									// size of each r,g,b
					 textureShader->pixmap->image,			// pointer to the actual texture pixels
					 clampFlags);

		// Set glTextureName on meshes
		for (int j = 0; j < metaFile->numMeshes; j++)
		{
			if (metaFile->meshes[j]->internalTextureID == i)
			{
				metaFile->meshes[j]->glTextureName = outTextureNames[i];
				metaFile->meshes[j]->texturingMode = meshTexturingMode;
			}
		}
	}
}

#pragma mark -

void Render_StartFrame(void)
{
	bool didMakeCurrent = SDL_GL_MakeCurrent(gSDLWindow, gGLContext);
	GAME_ASSERT_MESSAGE(didMakeCurrent, SDL_GetError());

	// Clear rendering statistics
	SDL_memset(&gRenderStats, 0, sizeof(gRenderStats));

	// Clear mesh queue
	gMeshQueueSize = 0;

	// Clear stats
	gRenderStats.meshesPass1 = 0;
	gRenderStats.meshesPass2 = 0;
	gRenderStats.triangles = 0;

	// Clear color & depth buffers.
#if !defined(__EMSCRIPTEN__)
	SetFlag(glDepthMask, true);	// The depth mask must be re-enabled so we can clear the depth buffer.
#else
	glDepthMask(GL_TRUE);
#endif

	GLbitfield clearWhat = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
#if OSXPPC
	// On PPC, bypass clear color in lawn levels (the cyc covers enough of the view)
	if (gIsInGame && gLevelType == LEVEL_TYPE_LAWN && gCyclorama && gDebugMode != DEBUG_MODE_WIREFRAME)
		clearWhat &= ~GL_COLOR_BUFFER_BIT;
#endif
	glClear(clearWhat);

	GAME_ASSERT(gState.currentTransform == NULL);

	GAME_ASSERT(!gFrameStarted);
	gFrameStarted = true;
}

void Render_SetViewport(int x, int y, int w, int h)
{
	glViewport(x, y, w, h);
}

void Render_FlushQueue(void)
{
	GAME_ASSERT(gFrameStarted);

	// Nothing to draw?
	if (gMeshQueueSize == 0)
		return;

	//--------------------------------------------------------------
	// SORT DRAW QUEUE ENTRIES
	// Opaque meshes are sorted front-to-back,
	// followed by transparent meshes, sorted back-to-front.
	SDL_qsort(
			gMeshQueuePtrs,
			gMeshQueueSize,
			sizeof(gMeshQueuePtrs[0]),
			DrawOrderComparator
	);

#if defined(__EMSCRIPTEN__)
	{
		int numDeferredColorMeshes = 0;
		const TQ3Matrix4x4* sGLESTransformSlot = NULL;

		GLES3_SetDepthPassGlobals();

		for (int i = 0; i < gMeshQueueSize; i++)
		{
			MeshQueueEntry* entry = gMeshQueuePtrs[i];

			if (!entry->meshIsTransparent)
			{
				GLES3MeshQueueEntry ge = {
					entry->mesh,
					entry->transform,
					entry->mods,
					entry->mods->statusBits,
				};
				BeginShadingPass(entry);
				PrepareOpaqueShading(entry);
				GLES3_SendGeometry(&ge, &sGLESTransformSlot, GLES3_PASS_OPAQUE, NULL);
			}
			else
			{
				GAME_ASSERT(numDeferredColorMeshes <= i);
				gMeshQueuePtrs[numDeferredColorMeshes++] = entry;

				if (!(entry->mods->statusBits & STATUS_BIT_NOZWRITE))
				{
					GLES3MeshQueueEntry ge = {
						entry->mesh,
						entry->transform,
						entry->mods,
						entry->mods->statusBits,
					};
					GLES3_BeginDepthPass(&ge);
					GLES3_SendGeometry(&ge, &sGLESTransformSlot, GLES3_PASS_DEPTH, NULL);
				}
			}
		}

		gRenderStats.meshesPass2 += numDeferredColorMeshes;

		if (numDeferredColorMeshes > 0)
		{
			GLES3_SetTransparentPassGlobals();

			for (int i = 0; i < numDeferredColorMeshes; i++)
			{
				const MeshQueueEntry* entry = gMeshQueuePtrs[i];
				GLES3MeshQueueEntry ge = {
					entry->mesh,
					entry->transform,
					entry->mods,
					entry->mods->statusBits,
				};
				BeginShadingPass(entry);
				PrepareAlphaShading(entry);
				const float* alphaColors = entry->mesh->hasVertexColors ? gBackupVertexColors : NULL;
				bool wantAdditive = !!(entry->mods->statusBits & STATUS_BIT_GLOW);
				GLES3_SetBlendAdditive(wantAdditive);
				GLES3_SendGeometry(&ge, &sGLESTransformSlot, GLES3_PASS_ALPHA, alphaColors);
			}
			GLES3_SetBlendAdditive(false);
		}

		gMeshQueueSize = 0;
		gState.currentTransform = NULL;
	}
#else
	//--------------------------------------------------------------
	// PASS 1: OPAQUE COLOR + DEPTH
	// - Draw opaque meshes (pre-sorted front-to-back) to color AND depth buffers.
	// - Draw transparent meshes (pre-sorted back-to-front after opaque meshes) to depth buffer only.

	int numDeferredColorMeshes = 0;

	glDepthFunc(GL_LESS);
	DisableState(GL_BLEND);

	for (int i = 0; i < gMeshQueueSize; i++)
	{
		MeshQueueEntry* entry = gMeshQueuePtrs[i];

		if (!entry->meshIsTransparent)
		{
			// If the mesh is opaque, draw it now
			BeginShadingPass(entry);
			PrepareOpaqueShading(entry);
			SendGeometry(entry);
		}
		else
		{
			// The mesh is transparent -- defer its color pass
			GAME_ASSERT(numDeferredColorMeshes <= i);
			gMeshQueuePtrs[numDeferredColorMeshes++] = entry;		// shoot back to start of queue for next pass

			// If a transparent mesh wants to write to the Z-buffer, do it now
			if (!(entry->mods->statusBits & STATUS_BIT_NOZWRITE))
			{
				BeginDepthPass(entry);
				SendGeometry(entry);
			}
		}
	}

	//--------------------------------------------------------------
	// PASS 2: ALPHA-BLENDED COLOR
	// - Draw transparent meshes to color buffer only

	gRenderStats.meshesPass2 += numDeferredColorMeshes;

	if (numDeferredColorMeshes > 0)
	{
		EnableState(GL_BLEND);
		DisableState(GL_ALPHA_TEST);
		SetFlag(glDepthMask, false);	// don't write to z buffer
		glDepthFunc(GL_LEQUAL);			// LEQUAL: our meshes' depth info is already in the z buffer (written in pass 1)

		for (int i = 0; i < numDeferredColorMeshes; i++)
		{
			const MeshQueueEntry* entry = gMeshQueuePtrs[i];
			BeginShadingPass(entry);
			PrepareAlphaShading(entry);
			SendGeometry(entry);
		}
	}

	//--------------------------------------------------------------
	// CLEAN UP

	// Clear mesh draw queue
	gMeshQueueSize = 0;

	// Clear transform
	if (NULL != gState.currentTransform)
	{
		glPopMatrix();
		gState.currentTransform = NULL;
	}
#endif
}

void Render_EndFrame(void)
{
	GAME_ASSERT(gFrameStarted);

	Render_FlushQueue();

	gFrameStarted = false;
}

#pragma mark -

static inline float WorldPointToDepth(const TQ3Point3D p)
{
	// This is a simplification of:
	//
	//     Q3Point3D_Transform(&p, &gCameraWorldToFrustumMatrix, &p);
	//     return p.z;
	//
	// This is enough to give us an idea of the depth of a point relative to another.

#define M(x,y) gCameraWorldToFrustumMatrix.value[x][y]
	return p.x*M(0,2) + p.y*M(1,2) + p.z*M(2,2);
#undef M
}

static float GetDepth(
		int						numMeshes,
		TQ3TriMeshData**		meshList,
		const TQ3Point3D*		centerCoord)
{
	if (centerCoord)
	{
		return WorldPointToDepth(*centerCoord);
	}
	else
	{
		// Average centers of all bounding boxes
		float mult = (float) numMeshes / 2.0f;
		TQ3Point3D center = (TQ3Point3D) { 0, 0, 0 };
		for (int i = 0; i < numMeshes; i++)
		{
			center.x += (meshList[i]->bBox.min.x + meshList[i]->bBox.max.x) * mult;
			center.y += (meshList[i]->bBox.min.y + meshList[i]->bBox.max.y) * mult;
			center.z += (meshList[i]->bBox.min.z + meshList[i]->bBox.max.z) * mult;
		}
		return WorldPointToDepth(center);
	}
}

static bool IsMeshTransparent(const TQ3TriMeshData* mesh, const RenderModifiers* mods)
{
	return	mesh->texturingMode == kQ3TexturingModeAlphaBlend
			|| mesh->diffuseColor.a < .999f
			|| mods->diffuseColor.a < .999f
			|| mods->autoFadeFactor < .999f
			|| (mods->statusBits & STATUS_BIT_GLOW)
	;
}

static MeshQueueEntry* NewMeshQueueEntry(void)
{
	MeshQueueEntry* entry = &gMeshQueueEntryPool[gMeshQueueSize];
	gMeshQueuePtrs[gMeshQueueSize] = entry;
	gMeshQueueSize++;
	return entry;
}

void Render_SubmitMeshList(
		int						numMeshes,
		TQ3TriMeshData**		meshList,
		const TQ3Matrix4x4*		transform,
		const RenderModifiers*	mods,
		const TQ3Point3D*		centerCoord)
{
	if (numMeshes <= 0)
		SDL_Log("not drawing this!\n");

	GAME_ASSERT(gFrameStarted);
	GAME_ASSERT(gMeshQueueSize + numMeshes <= MESHQUEUE_MAX_SIZE);

	float depth = GetDepth(numMeshes, meshList, centerCoord);

	for (int i = 0; i < numMeshes; i++)
	{
		MeshQueueEntry* entry = NewMeshQueueEntry();
		entry->mesh				= meshList[i];
		entry->transform		= transform;
		entry->mods				= mods ? mods : &kDefaultRenderMods;
		entry->depth			= depth;
		entry->meshIsTransparent= IsMeshTransparent(entry->mesh, entry->mods);

		gRenderStats.meshesPass1++;
		gRenderStats.triangles += entry->mesh->numTriangles;

		GAME_ASSERT(!(entry->mods->statusBits & STATUS_BIT_HIDDEN));
	}
}

void Render_SubmitMesh(
		const TQ3TriMeshData*	mesh,
		const TQ3Matrix4x4*		transform,
		const RenderModifiers*	mods,
		const TQ3Point3D*		centerCoord)
{
	GAME_ASSERT(gFrameStarted);
	GAME_ASSERT(gMeshQueueSize < MESHQUEUE_MAX_SIZE);

	MeshQueueEntry* entry = NewMeshQueueEntry();
	entry->mesh				= mesh;
	entry->transform		= transform;
	entry->mods				= mods ? mods : &kDefaultRenderMods;
	entry->depth			= GetDepth(1, (TQ3TriMeshData **) &mesh, centerCoord);
	entry->meshIsTransparent= IsMeshTransparent(entry->mesh, entry->mods);

	gRenderStats.meshesPass1++;
	gRenderStats.triangles += entry->mesh->numTriangles;

	GAME_ASSERT(!(entry->mods->statusBits & STATUS_BIT_HIDDEN));
}

#pragma mark -

static int DrawOrderComparator(const void* a_void, const void* b_void)
{
	static const int AFirst		= -1;
	static const int BFirst		= +1;
	static const int DontCare	= 0;

	const MeshQueueEntry* a = *(MeshQueueEntry**) a_void;
	const MeshQueueEntry* b = *(MeshQueueEntry**) b_void;

	// First check manual priority

	if (a->mods->drawOrder < b->mods->drawOrder)
		return AFirst;

	if (a->mods->drawOrder > b->mods->drawOrder)
		return BFirst;

	// A and B have the same manual priority
	// Compare their transparencies (opaque meshes go first)

	if (a->meshIsTransparent != b->meshIsTransparent)
	{
		return b->meshIsTransparent? AFirst: BFirst;
	}

	// A and B have the same manual priority AND transparency
	// Compare their depths

	if (!a->meshIsTransparent)					// both A and B are OPAQUE meshes: order them front-to-back
	{
		if (a->depth < b->depth)				// A is closer to the camera, draw it first
			return AFirst;

		if (a->depth > b->depth)				// B is closer to the camera, draw it first
			return BFirst;
	}
	else										// both A and B are TRANSPARENT meshes: order them back-to-front
	{
		if (a->depth < b->depth)				// A is closer to the camera, draw it last
			return BFirst;

		if (a->depth > b->depth)				// B is closer to the camera, draw it last
			return AFirst;
	}

	return DontCare;
}

#pragma mark -

#if !defined(__EMSCRIPTEN__)

static void SendGeometry(const MeshQueueEntry* entry)
{
	uint32_t statusBits = entry->mods->statusBits;

	const TQ3TriMeshData* mesh = entry->mesh;

	// Cull backfaces or not
	SetState(GL_CULL_FACE, !(statusBits & STATUS_BIT_KEEPBACKFACES));

	// To keep backfaces on a transparent mesh, draw backfaces first, then frontfaces.
	// This enhances the appearance of e.g. translucent spheres,
	// without the need to depth-sort individual faces.
	if (statusBits & STATUS_BIT_KEEPBACKFACES_2PASS)
		glCullFace(GL_FRONT);		// Pass 1: draw backfaces (cull frontfaces)

	// Submit vertex data
	glVertexPointer(3, GL_FLOAT, 0, mesh->points);

	// Submit transformation matrix if any
	if (gState.currentTransform != entry->transform)
	{
		if (gState.currentTransform)	// nuke old transform
			glPopMatrix();

		if (entry->transform)			// apply new transform
		{
			glPushMatrix();
			glMultMatrixf((float*)entry->transform->value);
		}

		gState.currentTransform = entry->transform;
	}

	glDrawElements(GL_TRIANGLES, mesh->numTriangles*3, GL_UNSIGNED_INT, mesh->triangles);
	CHECK_GL_ERROR();

	// Pass 2 to draw transparent meshes without face culling (see above for an explanation)
	if (statusBits & STATUS_BIT_KEEPBACKFACES_2PASS)
	{
		glCullFace(GL_BACK);	// pass 2: draw frontfaces (cull backfaces)
		glDrawElements(GL_TRIANGLES, mesh->numTriangles * 3, GL_UNSIGNED_INT, mesh->triangles);
		CHECK_GL_ERROR();
	}
}

static void BeginDepthPass(const MeshQueueEntry* entry)
{
	const TQ3TriMeshData* mesh = entry->mesh;
	uint32_t statusBits = entry->mods->statusBits;

	GAME_ASSERT(!(statusBits & STATUS_BIT_NOZWRITE));		// assume nozwrite objects were filtered out

	// Never write to color buffer in this pass
	SetColorMask(GL_FALSE);

	// Always write to depth buffer in this pass
	SetFlag(glDepthMask, true);

	glColor4f(1,1,1,1);
	DisableClientState(GL_COLOR_ARRAY);
	DisableClientState(GL_NORMAL_ARRAY);
	EnableState(GL_DEPTH_TEST);

	DisableState(GL_LIGHTING);
	DisableState(GL_FOG);

	// Texture mapping
	if (gDebugMode != DEBUG_MODE_NOTEXTURES &&
			(mesh->texturingMode & kQ3TexturingModeExt_OpacityModeMask) != kQ3TexturingModeOff)
	{
		GAME_ASSERT(mesh->vertexUVs);

		EnableState(GL_ALPHA_TEST);
		EnableState(GL_TEXTURE_2D);
		EnableClientState(GL_TEXTURE_COORD_ARRAY);
		Render_BindTexture(mesh->glTextureName);
		glTexCoordPointer(2, GL_FLOAT, 0, mesh->vertexUVs);
		CHECK_GL_ERROR();
	}
	else
	{
		DisableState(GL_ALPHA_TEST);
		DisableState(GL_TEXTURE_2D);
		DisableClientState(GL_TEXTURE_COORD_ARRAY);
		CHECK_GL_ERROR();
	}
}

static void BeginShadingPass(const MeshQueueEntry* entry)
{
	const TQ3TriMeshData* mesh = entry->mesh;
	uint32_t statusBits = entry->mods->statusBits;

	// Always write to color mask in this pass
	SetColorMask(GL_TRUE);

	// Environment map effect
	if (statusBits & STATUS_BIT_REFLECTIONMAP)
		EnvironmentMapTriMesh(mesh, entry->transform);

	// Apply gouraud or null illumination
	SetState(GL_LIGHTING,
			!( (statusBits & STATUS_BIT_NULLSHADER) || (mesh->texturingMode & kQ3TexturingModeExt_NullShaderFlag) ));

	// Apply fog or not
	SetState(GL_FOG, gState.sceneHasFog && !(statusBits & STATUS_BIT_NOFOG));

	// Texture mapping
	if (gDebugMode != DEBUG_MODE_NOTEXTURES &&
			(mesh->texturingMode & kQ3TexturingModeExt_OpacityModeMask) != kQ3TexturingModeOff)
	{
		EnableState(GL_TEXTURE_2D);
		EnableClientState(GL_TEXTURE_COORD_ARRAY);
		Render_BindTexture(mesh->glTextureName);
		glTexCoordPointer(2, GL_FLOAT, 0, (statusBits & STATUS_BIT_REFLECTIONMAP) ? gEnvMapUVs: mesh->vertexUVs);
		CHECK_GL_ERROR();
	}
	else
	{
		DisableState(GL_TEXTURE_2D);
		DisableClientState(GL_TEXTURE_COORD_ARRAY);
		CHECK_GL_ERROR();
	}

	// Submit normal data if any
	if (mesh->hasVertexNormals && !(statusBits & STATUS_BIT_NULLSHADER))
	{
		EnableClientState(GL_NORMAL_ARRAY);
		glNormalPointer(GL_FLOAT, 0, mesh->vertexNormals);
	}
	else
	{
		DisableClientState(GL_NORMAL_ARRAY);
	}
}

static void PrepareOpaqueShading(const MeshQueueEntry* entry)
{
	const TQ3TriMeshData* mesh = entry->mesh;
	const uint32_t statusBits = entry->mods->statusBits;

	TQ3TexturingMode texturingMode = (mesh->texturingMode & kQ3TexturingModeExt_OpacityModeMask);

	// Write to z-buffer
	SetFlag(glDepthMask, !(statusBits & STATUS_BIT_NOZWRITE));

	// Enable alpha testing if the mesh's texture calls for it
	SetState(GL_ALPHA_TEST, texturingMode == kQ3TexturingModeAlphaTest);

	// Per-vertex colors
	if (mesh->hasVertexColors)
	{
		EnableClientState(GL_COLOR_ARRAY);

		glColorPointer(4, GL_FLOAT, 0, mesh->vertexColors);
	}
	else
	{
		DisableClientState(GL_COLOR_ARRAY);

		// Apply diffuse color for the entire mesh
		glColor4f(
				mesh->diffuseColor.r * entry->mods->diffuseColor.r,
				mesh->diffuseColor.g * entry->mods->diffuseColor.g,
				mesh->diffuseColor.b * entry->mods->diffuseColor.b,
				1.0f);
	}
}

#else /* __EMSCRIPTEN__ */

static void BeginShadingPass(const MeshQueueEntry* entry)
{
	const TQ3TriMeshData* mesh = entry->mesh;
	uint32_t statusBits = entry->mods->statusBits;

	SetColorMask(GL_TRUE);
	if ((statusBits & STATUS_BIT_REFLECTIONMAP) && entry->transform)
		EnvironmentMapTriMesh(mesh, entry->transform);
}

static void PrepareOpaqueShading(const MeshQueueEntry* entry)
{
	(void)entry;
}

#endif /* !__EMSCRIPTEN__ */

static void PrepareAlphaShading(const MeshQueueEntry* entry)
{
	const TQ3TriMeshData* mesh = entry->mesh;

#if !defined(__EMSCRIPTEN__)
	const uint32_t statusBits = entry->mods->statusBits;
	// Set additive alpha blending or not
	bool wantAdditive = !!(statusBits & STATUS_BIT_GLOW);
	if (gState.blendFuncIsAdditive != wantAdditive)
	{
		if (wantAdditive)
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		else
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		gState.blendFuncIsAdditive = wantAdditive;
	}
#endif

	// Per-vertex colors
	if (mesh->hasVertexColors)
	{
#if !defined(__EMSCRIPTEN__)
		EnableClientState(GL_COLOR_ARRAY);

		// OpenGL ignores diffuse color (used for transparency) if we also send
		// per-vertex colors. So, apply transparency to the per-vertex color array.
#endif
		GAME_ASSERT(4 * mesh->numPoints <= (int)(sizeof(gBackupVertexColors) / sizeof(gBackupVertexColors[0])));
		int j = 0;
		for (int v = 0; v < mesh->numPoints; v++)
		{
			gBackupVertexColors[j++] = mesh->vertexColors[v].r;
			gBackupVertexColors[j++] = mesh->vertexColors[v].g;
			gBackupVertexColors[j++] = mesh->vertexColors[v].b;
			gBackupVertexColors[j++] = mesh->vertexColors[v].a * entry->mods->autoFadeFactor;
		}

#if !defined(__EMSCRIPTEN__)
		glColorPointer(4, GL_FLOAT, 0, gBackupVertexColors);
#endif
	}
	else
	{
#if !defined(__EMSCRIPTEN__)
		DisableClientState(GL_COLOR_ARRAY);

		// Apply diffuse color for the entire mesh
		glColor4f(
				mesh->diffuseColor.r * entry->mods->diffuseColor.r,
				mesh->diffuseColor.g * entry->mods->diffuseColor.g,
				mesh->diffuseColor.b * entry->mods->diffuseColor.b,
				mesh->diffuseColor.a * entry->mods->diffuseColor.a * entry->mods->autoFadeFactor);
#endif
	}
}

void Render_ResetColor(void)
{
#if !defined(__EMSCRIPTEN__)
	DisableState(GL_BLEND);
	DisableState(GL_ALPHA_TEST);
	DisableState(GL_LIGHTING);
	DisableState(GL_TEXTURE_2D);
	DisableClientState(GL_NORMAL_ARRAY);
	DisableClientState(GL_COLOR_ARRAY);
	glColor4f(1, 1, 1, 1);
#else
	GLES3_ResetColorState();
#endif
}

#pragma mark -

//=======================================================================================================

TQ3Vector2D FitRectKeepAR(
	int logicalWidth,
	int logicalHeight,
	float displayWidth,
	float displayHeight)
{
	float displayAR = (float)displayWidth / (float)displayHeight;
	float logicalAR = (float)logicalWidth / (float)logicalHeight;

	if (displayAR >= logicalAR)
	{
		return (TQ3Vector2D) { displayHeight * logicalAR, displayHeight };
	}
	else
	{
		return (TQ3Vector2D) { displayWidth, displayWidth / logicalAR };
	}
}

/****************************/
/*    2D    */
/****************************/

void Render_Enter2D_Full640x480(void)
{
	if (gGamePrefs.force4x3AspectRatio)
	{
		TQ3Vector2D fitted = FitRectKeepAR(GAME_VIEW_WIDTH, GAME_VIEW_HEIGHT, gWindowWidth, gWindowHeight);
		glViewport(
			(GLint) (0.5f * (gWindowWidth - fitted.x)),
			(GLint) (0.5f * (gWindowHeight - fitted.y)),
			(GLint) ceilf(fitted.x),
			(GLint) ceilf(fitted.y));
	}
	else
	{
		glViewport(0, 0, gWindowWidth, gWindowHeight);
	}

#if !defined(__EMSCRIPTEN__)
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0,640,480,0,0,1000);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
#else
	GLES3_PushMatrixStack();
	GLES3_SetOrthoProjection(0.f, 640.f, 480.f, 0.f, 0.f, 1000.f);
#endif
}

void Render_Enter2D_NormalizedCoordinates(float aspect)
{
#if !defined(__EMSCRIPTEN__)
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(-aspect, aspect, -1, 1, 0, 1000);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
#else
	GLES3_PushMatrixStack();
	GLES3_SetOrthoProjection(-aspect, aspect, -1.f, 1.f, 0.f, 1000.f);
#endif
}

void Render_Enter2D_NativeResolution(void)
{
#if !defined(__EMSCRIPTEN__)
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, gWindowWidth, gWindowHeight, 0, 0, 1000);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
#else
	GLES3_PushMatrixStack();
	GLES3_SetOrthoProjection(0.f, (float)gWindowWidth, (float)gWindowHeight, 0.f, 0.f, 1000.f);
#endif
}

void Render_Exit2D(void)
{
#if !defined(__EMSCRIPTEN__)
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
#else
	GLES3_PopMatrixStack();
#endif
}

#pragma mark -

//=======================================================================================================

/*******************************************/
/*    BACKDROP/OVERLAY (COVER WINDOW)      */
/*******************************************/

void Render_DrawFadeOverlay(float opacity)
{
	GAME_ASSERT(gFullscreenQuad);

	gFullscreenQuad->texturingMode = kQ3TexturingModeOff;
	gFullscreenQuad->diffuseColor = (TQ3ColorRGBA) { 0,0,0,1.0f-opacity };

	Render_SubmitMesh(gFullscreenQuad, NULL, &kDefaultRenderMods_FadeOverlay, &kQ3Point3D_Zero);
}

#if defined(__EMSCRIPTEN__)
void Render_DrawDebugLines(GLenum mode, const float* xyz, int nverts, float r, float g, float b, float a)
{
	GLES3_DrawLinePrimitives(mode, xyz, nverts, r, g, b, a);
}
#endif

#pragma mark -

TQ3Area Render_GetAdjustedViewportRect(Rect paneClip, int logicalWidth, int logicalHeight)
{
	float scaleX;
	float scaleY;
	float xoff = 0;
	float yoff = 0;

	if (!gGamePrefs.force4x3AspectRatio)
	{
		scaleX = gWindowWidth / (float)logicalWidth;	// scale clip pane to window size
		scaleY = gWindowHeight / (float)logicalHeight;
	}
	else
	{
		TQ3Vector2D fitted = FitRectKeepAR(logicalWidth, logicalHeight, gWindowWidth, gWindowHeight);
		xoff = (gWindowWidth - fitted.x) * 0.5f;
		yoff = (gWindowHeight - fitted.y) * 0.5f;
		scaleX = fitted.x / (float)logicalWidth;
		scaleY = fitted.y / (float)logicalHeight;
	}

	float left   = xoff + scaleX * paneClip.left;
	float right  = xoff + scaleX * (logicalWidth - paneClip.right);
	float top    = yoff + scaleY * paneClip.top;
	float bottom = yoff + scaleY * (logicalHeight - paneClip.bottom);

	// Floor min to avoid seam at edges of HUD if scale ratio is dirty
	left = floorf(left);
	top = floorf(top);
	// Ceil max to avoid seam at edges of HUD if scale ratio is dirty
	right = ceilf(right);
	bottom = ceilf(bottom);

	return (TQ3Area) {{left,top},{right,bottom}};
}
