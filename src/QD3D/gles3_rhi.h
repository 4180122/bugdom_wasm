#pragma once

#if defined(__EMSCRIPTEN__)

#include <GLES3/gl3.h>
#include <QD3D.h>
#include <stdbool.h>
#include <stddef.h>

struct RenderModifiers;

typedef enum GLES3PassPhase
{
	GLES3_PASS_DEPTH = 0,
	GLES3_PASS_OPAQUE = 1,
	GLES3_PASS_ALPHA = 2,
} GLES3PassPhase;

typedef struct GLES3MeshQueueEntry
{
	const TQ3TriMeshData*			mesh;
	const TQ3Matrix4x4*				transform;
	const struct RenderModifiers*	mods;
	uint32_t						statusBits;
} GLES3MeshQueueEntry;

void GLES3_Init(void);
void GLES3_Shutdown(void);

void GLES3_SetCameraMatrices(const TQ3Matrix4x4* proj, const TQ3Matrix4x4* view);

void GLES3_PushMatrixStack(void);
void GLES3_PopMatrixStack(void);
void GLES3_SetOrthoProjection(float left, float right, float bottom, float top, float nearVal, float farVal);

/* lightDefPtr is QD3DLightDefType* (opaque here to avoid extra includes). */
void GLES3_LoadLightColors(const void* lightDefPtr);
void GLES3_UpdateLightDirections(const void* lightDefPtr, const TQ3Matrix4x4* worldToView);

void GLES3_SetFog(bool sceneHasFog, float fogStart, float fogEnd, float fogR, float fogG, float fogB);

void GLES3_InitPipelineState(float clearR, float clearG, float clearB);

void GLES3_BeginDepthPass(const GLES3MeshQueueEntry* entry);

void GLES3_SendGeometry(
		GLES3MeshQueueEntry* entry,
		const TQ3Matrix4x4** currentTransformSlot,
		GLES3PassPhase pass,
		const float* alphaVertexColorsScratch);

void GLES3_ResetColorState(void);

void GLES3_SetDepthPassGlobals(void);
void GLES3_SetTransparentPassGlobals(void);
void GLES3_SetBlendAdditive(bool enable);

void GLES3_DrawLinePrimitives(GLenum mode, const float* xyz, int nverts, float r, float g, float b, float a);

#endif /* __EMSCRIPTEN__ */
