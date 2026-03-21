/* GLES3 / WebGL2 immediate renderer — replaces fixed-function GL on Emscripten. */

#if defined(__EMSCRIPTEN__)

#include "gles3_rhi.h"
#include "gles3_matrix_upload.h"
#include "game.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

enum
{
	kMaxMatrixStack = 8,
	kMaxFillLights = 4,
};

static GLuint gMeshProgram;
static GLuint gLineProgram;
static GLuint gVAO;
static GLuint gLineVAO;
static GLuint gVBO;
static GLuint gEBO;

static GLint gLoc_uMVP;
static GLint gLoc_uModelView;
static GLint gLoc_uDiffuse;
static GLint gLoc_uUseTexture;
static GLint gLoc_uTex;
static GLint gLoc_uNullShader;
static GLint gLoc_uUseLighting;
static GLint gLoc_uAlphaTest;
static GLint gLoc_uAlphaRef;
static GLint gLoc_uAmbient;
static GLint gLoc_uLightDir[kMaxFillLights];
static GLint gLoc_uLightDiffuse[kMaxFillLights];
static GLint gLoc_uNumLights;
static GLint gLoc_uUseFog;
static GLint gLoc_uFogColor;
static GLint gLoc_uFogStart;
static GLint gLoc_uFogEnd;
static GLint gLoc_uHasNormals;
static GLint gLoc_uHasVertexColor;

static GLint gLineLoc_uMVP;
static GLint gLineLoc_uColor;

static TQ3Matrix4x4 gProj;
static TQ3Matrix4x4 gView;
static TQ3Matrix4x4 gStackProj[kMaxMatrixStack];
static TQ3Matrix4x4 gStackView[kMaxMatrixStack];
static int gMatStackDepth;

static float gAmbient[4];
static float gLightDirEye[kMaxFillLights][3];
static float gLightDiffuse[kMaxFillLights][3];
static int gNumLights;

static bool gSceneFog;
static float gFogStart, gFogEnd;
static float gFogColor[3];

static TQ3Matrix4x4 gIdentity;

static const char kVS[] =
	"#version 300 es\n"
	"precision highp float;\n"
	"layout(location = 0) in vec3 aPosition;\n"
	"layout(location = 1) in vec3 aNormal;\n"
	"layout(location = 2) in vec2 aUV;\n"
	"layout(location = 3) in vec4 aColor;\n"
	"uniform mat4 uMVP;\n"
	"uniform mat4 uModelView;\n"
	"uniform int uHasNormals;\n"
	"uniform int uHasVertexColor;\n"
	"out vec3 vEye;\n"
	"out vec3 vNormal;\n"
	"out vec2 vUV;\n"
	"out vec4 vColor;\n"
	"void main() {\n"
	"  vec4 posE = uModelView * vec4(aPosition, 1.0);\n"
	"  vEye = posE.xyz;\n"
	"  if (uHasNormals != 0) {\n"
	"    mat3 R = mat3(uModelView);\n"
	"    vNormal = normalize(transpose(inverse(R)) * aNormal);\n"
	"  } else {\n"
	"    vNormal = vec3(0.0, 0.0, 1.0);\n"
	"  }\n"
	"  vUV = aUV;\n"
	"  vColor = (uHasVertexColor != 0) ? aColor : vec4(1.0);\n"
	"  gl_Position = uMVP * vec4(aPosition, 1.0);\n"
	"}\n";

static const char kFS[] =
	"#version 300 es\n"
	"precision highp float;\n"
	"in vec3 vEye;\n"
	"in vec3 vNormal;\n"
	"in vec2 vUV;\n"
	"in vec4 vColor;\n"
	"uniform vec4 uDiffuse;\n"
	"uniform int uUseTexture;\n"
	"uniform sampler2D uTex;\n"
	"uniform int uNullShader;\n"
	"uniform int uUseLighting;\n"
	"uniform int uAlphaTest;\n"
	"uniform float uAlphaRef;\n"
	"uniform vec3 uAmbient;\n"
	"uniform vec3 uLightDir[4];\n"
	"uniform vec3 uLightDiffuse[4];\n"
	"uniform int uNumLights;\n"
	"uniform int uUseFog;\n"
	"uniform vec3 uFogColor;\n"
	"uniform float uFogStart;\n"
	"uniform float uFogEnd;\n"
	"out vec4 fragColor;\n"
	"void main() {\n"
	"  vec4 texc = (uUseTexture != 0) ? texture(uTex, vUV) : vec4(1.0);\n"
	"  vec3 mat = vColor.rgb * uDiffuse.rgb;\n"
	"  float baseA = vColor.a * uDiffuse.a * texc.a;\n"
	"  vec3 colorRgb = mat * texc.rgb;\n"
	"  if (uAlphaTest != 0) {\n"
	"    if (baseA <= uAlphaRef) discard;\n"
	"  }\n"
	"  if (uNullShader != 0) {\n"
	"    vec3 outc = colorRgb;\n"
	"    if (uUseFog != 0) {\n"
	"      float dist = length(vEye);\n"
	"      float f = clamp((uFogEnd - dist) / max(uFogEnd - uFogStart, 0.0001), 0.0, 1.0);\n"
	"      outc = mix(uFogColor, outc, f);\n"
	"    }\n"
	"    fragColor = vec4(outc, baseA);\n"
	"  } else if (uUseLighting != 0) {\n"
	"    vec3 N = normalize(vNormal);\n"
	"    vec3 lit = uAmbient * colorRgb;\n"
	"    for (int i = 0; i < 4; i++) {\n"
	"      if (i >= uNumLights) break;\n"
	"      vec3 L = normalize(uLightDir[i]);\n"
	"      float d = max(dot(N, L), 0.0);\n"
	"      lit += uLightDiffuse[i] * d * colorRgb;\n"
	"    }\n"
	"    if (uUseFog != 0) {\n"
	"      float dist = length(vEye);\n"
	"      float f = clamp((uFogEnd - dist) / max(uFogEnd - uFogStart, 0.0001), 0.0, 1.0);\n"
	"      lit = mix(uFogColor, lit, f);\n"
	"    }\n"
	"    fragColor = vec4(lit, baseA);\n"
	"  } else {\n"
	"    vec3 outc = colorRgb;\n"
	"    if (uUseFog != 0) {\n"
	"      float dist = length(vEye);\n"
	"      float f = clamp((uFogEnd - dist) / max(uFogEnd - uFogStart, 0.0001), 0.0, 1.0);\n"
	"      outc = mix(uFogColor, outc, f);\n"
	"    }\n"
	"    fragColor = vec4(outc, baseA);\n"
	"  }\n"
	"}\n";

static const char kLineVS[] =
	"#version 300 es\n"
	"precision highp float;\n"
	"layout(location = 0) in vec3 aPos;\n"
	"uniform mat4 uMVP;\n"
	"void main() { gl_Position = uMVP * vec4(aPos, 1.0); }\n";

static const char kLineFS[] =
	"#version 300 es\n"
	"precision highp float;\n"
	"uniform vec4 uColor;\n"
	"out vec4 fragColor;\n"
	"void main() { fragColor = uColor; }\n";

static GLuint CompileShader(GLenum type, const char* src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[2048];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		DoFatalAlert(log);
	}
	return s;
}

static GLuint LinkProgram(GLuint vs, GLuint fs)
{
	GLuint p = glCreateProgram();
	glAttachShader(p, vs);
	glAttachShader(p, fs);
	glBindAttribLocation(p, 0, "aPosition");
	glBindAttribLocation(p, 1, "aNormal");
	glBindAttribLocation(p, 2, "aUV");
	glBindAttribLocation(p, 3, "aColor");
	glLinkProgram(p);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		char log[2048];
		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		DoFatalAlert(log);
	}
	return p;
}

/*
 * uModelView is the same column-major mat4 as glLoadMatrix (transpose of Quesa storage).
 * For a direction, eye = (V^T)*d_w; component i uses column i of Quesa's upper 3x3.
 */
static void TransformDirWorldToEye(const TQ3Matrix4x4* view, float wx, float wy, float wz, float* ox, float* oy, float* oz)
{
	float x = view->value[0][0] * wx + view->value[1][0] * wy + view->value[2][0] * wz;
	float y = view->value[0][1] * wx + view->value[1][1] * wy + view->value[2][1] * wz;
	float z = view->value[0][2] * wx + view->value[1][2] * wy + view->value[2][2] * wz;
	float len = sqrtf(x * x + y * y + z * z);
	if (len > 1e-8f)
	{
		x /= len;
		y /= len;
		z /= len;
	}
	*ox = x;
	*oy = y;
	*oz = z;
}

void GLES3_Init(void)
{
	Q3Matrix4x4_SetIdentity(&gIdentity);
	Q3Matrix4x4_SetIdentity(&gProj);
	Q3Matrix4x4_SetIdentity(&gView);
	gMatStackDepth = 0;

	GLuint vs = CompileShader(GL_VERTEX_SHADER, kVS);
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFS);
	gMeshProgram = LinkProgram(vs, fs);

	gLoc_uMVP = glGetUniformLocation(gMeshProgram, "uMVP");
	gLoc_uModelView = glGetUniformLocation(gMeshProgram, "uModelView");
	gLoc_uDiffuse = glGetUniformLocation(gMeshProgram, "uDiffuse");
	gLoc_uUseTexture = glGetUniformLocation(gMeshProgram, "uUseTexture");
	gLoc_uTex = glGetUniformLocation(gMeshProgram, "uTex");
	gLoc_uNullShader = glGetUniformLocation(gMeshProgram, "uNullShader");
	gLoc_uUseLighting = glGetUniformLocation(gMeshProgram, "uUseLighting");
	gLoc_uAlphaTest = glGetUniformLocation(gMeshProgram, "uAlphaTest");
	gLoc_uAlphaRef = glGetUniformLocation(gMeshProgram, "uAlphaRef");
	gLoc_uAmbient = glGetUniformLocation(gMeshProgram, "uAmbient");
	for (int i = 0; i < kMaxFillLights; i++)
	{
		char name[48];
		SDL_snprintf(name, sizeof(name), "uLightDir[%d]", i);
		gLoc_uLightDir[i] = glGetUniformLocation(gMeshProgram, name);
		SDL_snprintf(name, sizeof(name), "uLightDiffuse[%d]", i);
		gLoc_uLightDiffuse[i] = glGetUniformLocation(gMeshProgram, name);
	}
	gLoc_uNumLights = glGetUniformLocation(gMeshProgram, "uNumLights");
	gLoc_uUseFog = glGetUniformLocation(gMeshProgram, "uUseFog");
	gLoc_uFogColor = glGetUniformLocation(gMeshProgram, "uFogColor");
	gLoc_uFogStart = glGetUniformLocation(gMeshProgram, "uFogStart");
	gLoc_uFogEnd = glGetUniformLocation(gMeshProgram, "uFogEnd");
	gLoc_uHasNormals = glGetUniformLocation(gMeshProgram, "uHasNormals");
	gLoc_uHasVertexColor = glGetUniformLocation(gMeshProgram, "uHasVertexColor");

	GLuint lvs = CompileShader(GL_VERTEX_SHADER, kLineVS);
	GLuint lfs = CompileShader(GL_FRAGMENT_SHADER, kLineFS);
	gLineProgram = glCreateProgram();
	glAttachShader(gLineProgram, lvs);
	glAttachShader(gLineProgram, lfs);
	glBindAttribLocation(gLineProgram, 0, "aPos");
	glLinkProgram(gLineProgram);
	glDeleteShader(lvs);
	glDeleteShader(lfs);
	gLineLoc_uMVP = glGetUniformLocation(gLineProgram, "uMVP");
	gLineLoc_uColor = glGetUniformLocation(gLineProgram, "uColor");

	glGenVertexArrays(1, &gVAO);
	glGenVertexArrays(1, &gLineVAO);
	glGenBuffers(1, &gVBO);
	glGenBuffers(1, &gEBO);
}

void GLES3_Shutdown(void)
{
	if (gMeshProgram)
		glDeleteProgram(gMeshProgram);
	if (gLineProgram)
		glDeleteProgram(gLineProgram);
	gMeshProgram = gLineProgram = 0;
	if (gVAO)
		glDeleteVertexArrays(1, &gVAO);
	if (gLineVAO)
		glDeleteVertexArrays(1, &gLineVAO);
	if (gVBO)
		glDeleteBuffers(1, &gVBO);
	if (gEBO)
		glDeleteBuffers(1, &gEBO);
	gVAO = gLineVAO = gVBO = gEBO = 0;
}

void GLES3_SetCameraMatrices(const TQ3Matrix4x4* proj, const TQ3Matrix4x4* view)
{
	gProj = *proj;
	gView = *view;
}

void GLES3_PushMatrixStack(void)
{
	GAME_ASSERT(gMatStackDepth < kMaxMatrixStack);
	gStackProj[gMatStackDepth] = gProj;
	gStackView[gMatStackDepth] = gView;
	gMatStackDepth++;
}

void GLES3_PopMatrixStack(void)
{
	GAME_ASSERT(gMatStackDepth > 0);
	gMatStackDepth--;
	gProj = gStackProj[gMatStackDepth];
	gView = gStackView[gMatStackDepth];
}

static void Matrix4_Ortho(TQ3Matrix4x4* m, float l, float r, float b, float t, float n, float f)
{
	memset(m->value, 0, sizeof(m->value));
	m->value[0][0] = 2.0f / (r - l);
	m->value[1][1] = 2.0f / (t - b);
	m->value[2][2] = -2.0f / (f - n);
	m->value[3][0] = -(r + l) / (r - l);
	m->value[3][1] = -(t + b) / (t - b);
	m->value[3][2] = -(f + n) / (f - n);
	m->value[3][3] = 1.0f;
}

void GLES3_SetOrthoProjection(float left, float right, float bottom, float top, float nearVal, float farVal)
{
	Matrix4_Ortho(&gProj, left, right, bottom, top, nearVal, farVal);
	Q3Matrix4x4_SetIdentity(&gView);
}

void GLES3_LoadLightColors(const void* lightDefPtr)
{
	const QD3DLightDefType* L = (const QD3DLightDefType*)lightDefPtr;

	gAmbient[0] = L->ambientBrightness * L->ambientColor.r;
	gAmbient[1] = L->ambientBrightness * L->ambientColor.g;
	gAmbient[2] = L->ambientBrightness * L->ambientColor.b;
	gAmbient[3] = 1.0f;

	gNumLights = (int)L->numFillLights;
	if (gNumLights > kMaxFillLights)
		gNumLights = kMaxFillLights;

	for (int i = 0; i < gNumLights; i++)
	{
		gLightDiffuse[i][0] = L->fillColor[i].r * L->fillBrightness[i];
		gLightDiffuse[i][1] = L->fillColor[i].g * L->fillBrightness[i];
		gLightDiffuse[i][2] = L->fillColor[i].b * L->fillBrightness[i];
	}
}

void GLES3_UpdateLightDirections(const void* lightDefPtr, const TQ3Matrix4x4* worldToView)
{
	const QD3DLightDefType* L = (const QD3DLightDefType*)lightDefPtr;

	int n = (int)L->numFillLights;
	if (n > kMaxFillLights)
		n = kMaxFillLights;

	for (int i = 0; i < n; i++)
	{
		TQ3Vector3D dir = L->fillDirection[i];
		Q3Vector3D_Normalize(&dir, &dir);
		float wx = -dir.x;
		float wy = -dir.y;
		float wz = -dir.z;
		TransformDirWorldToEye(worldToView, wx, wy, wz,
				&gLightDirEye[i][0], &gLightDirEye[i][1], &gLightDirEye[i][2]);
	}
}

void GLES3_SetFog(bool sceneHasFog, float fogStart, float fogEnd, float fogR, float fogG, float fogB)
{
	gSceneFog = sceneHasFog;
	gFogStart = fogStart;
	gFogEnd = fogEnd;
	gFogColor[0] = fogR;
	gFogColor[1] = fogG;
	gFogColor[2] = fogB;
}

void GLES3_InitPipelineState(float clearR, float clearG, float clearB)
{
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(clearR, clearG, clearB, 1.0f);
	glActiveTexture(GL_TEXTURE0);
}

void GLES3_SetDepthPassGlobals(void)
{
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);
}

void GLES3_SetTransparentPassGlobals(void)
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	glDepthFunc(GL_LEQUAL);
}

void GLES3_SetBlendAdditive(bool enable)
{
	if (enable)
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	else
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GLES3_BeginDepthPass(const GLES3MeshQueueEntry* entry)
{
	const TQ3TriMeshData* mesh = entry->mesh;
	(void)mesh;
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);
}

static void UploadMeshInterleaved(
		const TQ3TriMeshData* mesh,
		const TQ3Param2D* uvOverride,
		const float* colorOverride,
		int useVertexColor,
		int hasNormals)
{
	const int n = mesh->numPoints;
	const size_t stride = 12 * sizeof(float);
	const size_t bufSize = (size_t)n * stride;
	float* tmp = (float*)malloc(bufSize);
	GAME_ASSERT(tmp);

	for (int i = 0; i < n; i++)
	{
		float* base = tmp + i * 12;
		base[0] = mesh->points[i].x;
		base[1] = mesh->points[i].y;
		base[2] = mesh->points[i].z;
		if (hasNormals)
		{
			base[3] = mesh->vertexNormals[i].x;
			base[4] = mesh->vertexNormals[i].y;
			base[5] = mesh->vertexNormals[i].z;
		}
		else
		{
			base[3] = 0.f;
			base[4] = 0.f;
			base[5] = 1.f;
		}
		float u, v;
		if (uvOverride)
		{
			u = uvOverride[i].u;
			v = uvOverride[i].v;
		}
		else if (mesh->vertexUVs)
		{
			u = mesh->vertexUVs[i].u;
			v = mesh->vertexUVs[i].v;
		}
		else
		{
			u = 0.f;
			v = 0.f;
		}
		base[6] = u;
		base[7] = v;
		if (useVertexColor)
		{
			if (colorOverride)
			{
				base[8] = colorOverride[i * 4 + 0];
				base[9] = colorOverride[i * 4 + 1];
				base[10] = colorOverride[i * 4 + 2];
				base[11] = colorOverride[i * 4 + 3];
			}
			else
			{
				base[8] = mesh->vertexColors[i].r;
				base[9] = mesh->vertexColors[i].g;
				base[10] = mesh->vertexColors[i].b;
				base[11] = mesh->vertexColors[i].a;
			}
		}
		else
		{
			base[8] = 1.f;
			base[9] = 1.f;
			base[10] = 1.f;
			base[11] = 1.f;
		}
	}

	glBindBuffer(GL_ARRAY_BUFFER, gVBO);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bufSize, tmp, GL_STREAM_DRAW);
	free(tmp);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)mesh->numTriangles * 3 * sizeof(uint32_t), mesh->triangles, GL_STREAM_DRAW);
}

void GLES3_SendGeometry(
		GLES3MeshQueueEntry* entry,
		const TQ3Matrix4x4** currentTransformSlot,
		GLES3PassPhase pass,
		const float* alphaVertexColorsScratch)
{
	const TQ3TriMeshData* mesh = entry->mesh;
	const uint32_t statusBits = entry->statusBits;
	const RenderModifiers* mods = entry->mods;

	const TQ3Matrix4x4* T = entry->transform;
	*currentTransformSlot = T;

	TQ3Matrix4x4 MV;
	if (T)
		MatrixMultiply((TQ3Matrix4x4*)T, &gView, &MV);
	else
		MV = gView;

	TQ3Matrix4x4 MVP;
	MatrixMultiply(&MV, &gProj, &MVP);

	const TQ3Param2D* uvOverride = NULL;
	if (statusBits & STATUS_BIT_REFLECTIONMAP)
		uvOverride = gEnvMapUVs;

	int hasNormals = (mesh->hasVertexNormals && !(statusBits & STATUS_BIT_NULLSHADER)) ? 1 : 0;

	int uploadVertColor = 0;
	const float* colorOv = NULL;

	if (pass == GLES3_PASS_DEPTH)
	{
		uploadVertColor = 0;
		hasNormals = 0;
	}
	else if (pass == GLES3_PASS_OPAQUE)
	{
		uploadVertColor = mesh->hasVertexColors ? 1 : 0;
		colorOv = NULL;
	}
	else
	{
		uploadVertColor = mesh->hasVertexColors ? 1 : 0;
		colorOv = alphaVertexColorsScratch;
	}

	UploadMeshInterleaved(mesh, uvOverride, colorOv, uploadVertColor, hasNormals);

	if (statusBits & STATUS_BIT_KEEPBACKFACES)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);

	glBindVertexArray(gVAO);
	glBindBuffer(GL_ARRAY_BUFFER, gVBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gEBO);

	const GLsizei stride = (GLsizei)(12 * sizeof(float));
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));

	glUseProgram(gMeshProgram);
	CHECK_GL_ERROR();

	float mvp[16];
	float mv[16];
	BugdomUploadQuesaMat4ForGLSL(&MVP, mvp);
	BugdomUploadQuesaMat4ForGLSL(&MV, mv);
	glUniformMatrix4fv(gLoc_uMVP, 1, GL_FALSE, mvp);
	glUniformMatrix4fv(gLoc_uModelView, 1, GL_FALSE, mv);

	float dr = mesh->diffuseColor.r * mods->diffuseColor.r;
	float dg = mesh->diffuseColor.g * mods->diffuseColor.g;
	float db = mesh->diffuseColor.b * mods->diffuseColor.b;
	float da = mesh->diffuseColor.a * mods->diffuseColor.a;
	if (pass == GLES3_PASS_ALPHA && !mesh->hasVertexColors)
		da *= mods->autoFadeFactor;

	int texturingMode = mesh->texturingMode & kQ3TexturingModeExt_OpacityModeMask;
	int useTex = (gDebugMode != DEBUG_MODE_NOTEXTURES && texturingMode != kQ3TexturingModeOff) ? 1 : 0;
	glUniform1i(gLoc_uUseTexture, useTex);
	glUniform1i(gLoc_uTex, 0);
	if (useTex)
	{
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, mesh->glTextureName);
		Render_InvalidateTextureCache();
	}

	int nullShader = ((statusBits & STATUS_BIT_NULLSHADER) || (mesh->texturingMode & kQ3TexturingModeExt_NullShaderFlag)) ? 1 : 0;
	TQ3TexturingMode tm = (TQ3TexturingMode)texturingMode;

	if (pass == GLES3_PASS_DEPTH)
	{
		glUniform4f(gLoc_uDiffuse, 1.f, 1.f, 1.f, 1.f);
		glUniform1i(gLoc_uNullShader, 1);
		glUniform1i(gLoc_uUseLighting, 0);
		glUniform1i(gLoc_uAlphaTest, (tm == kQ3TexturingModeAlphaTest) ? 1 : 0);
		glUniform1i(gLoc_uUseFog, 0);
		glUniform1i(gLoc_uHasNormals, 0);
		glUniform1i(gLoc_uHasVertexColor, 0);
	}
	else if (pass == GLES3_PASS_OPAQUE)
	{
		glUniform4f(gLoc_uDiffuse, dr, dg, db, da);
		glUniform1i(gLoc_uNullShader, nullShader);
		glUniform1i(gLoc_uUseLighting, (!nullShader && hasNormals) ? 1 : 0);
		glUniform1i(gLoc_uAlphaTest, (tm == kQ3TexturingModeAlphaTest) ? 1 : 0);
		glUniform1i(gLoc_uUseFog, (gSceneFog && !(statusBits & STATUS_BIT_NOFOG)) ? 1 : 0);
		glUniform1i(gLoc_uHasNormals, hasNormals);
		glUniform1i(gLoc_uHasVertexColor, mesh->hasVertexColors ? 1 : 0);
	}
	else
	{
		glUniform4f(gLoc_uDiffuse, dr, dg, db, da);
		glUniform1i(gLoc_uNullShader, nullShader);
		glUniform1i(gLoc_uUseLighting, (!nullShader && hasNormals) ? 1 : 0);
		glUniform1i(gLoc_uAlphaTest, 0);
		glUniform1i(gLoc_uUseFog, (gSceneFog && !(statusBits & STATUS_BIT_NOFOG)) ? 1 : 0);
		glUniform1i(gLoc_uHasNormals, hasNormals);
		glUniform1i(gLoc_uHasVertexColor, mesh->hasVertexColors ? 1 : 0);
	}

	glUniform1f(gLoc_uAlphaRef, 0.4999f);

	glUniform3fv(gLoc_uAmbient, 1, gAmbient);
	for (int li = 0; li < kMaxFillLights; li++)
	{
		glUniform3fv(gLoc_uLightDir[li], 1, gLightDirEye[li]);
		glUniform3fv(gLoc_uLightDiffuse[li], 1, gLightDiffuse[li]);
	}
	glUniform1i(gLoc_uNumLights, gNumLights);

	glUniform3fv(gLoc_uFogColor, 1, gFogColor);
	glUniform1f(gLoc_uFogStart, gFogStart);
	glUniform1f(gLoc_uFogEnd, gFogEnd);

	glDrawElements(GL_TRIANGLES, mesh->numTriangles * 3, GL_UNSIGNED_INT, (void*)0);
	CHECK_GL_ERROR();

	if (statusBits & STATUS_BIT_KEEPBACKFACES_2PASS)
	{
		glCullFace(GL_FRONT);
		glDrawElements(GL_TRIANGLES, mesh->numTriangles * 3, GL_UNSIGNED_INT, (void*)0);
		CHECK_GL_ERROR();
		glCullFace(GL_BACK);
	}
}

void GLES3_ResetColorState(void)
{
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void GLES3_DrawLinePrimitives(GLenum mode, const float* xyz, int nverts, float r, float g, float b, float a)
{
	if (nverts < 2)
		return;

	TQ3Matrix4x4 MV = gView;
	TQ3Matrix4x4 MVP;
	MatrixMultiply(&MV, &gProj, &MVP);
	float mvp[16];
	BugdomUploadQuesaMat4ForGLSL(&MVP, mvp);

	glBindBuffer(GL_ARRAY_BUFFER, gVBO);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nverts * 3 * sizeof(float), xyz, GL_STREAM_DRAW);

	glBindVertexArray(gLineVAO);
	glBindBuffer(GL_ARRAY_BUFFER, gVBO);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
	glEnableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(3);

	glUseProgram(gLineProgram);
	glUniformMatrix4fv(gLineLoc_uMVP, 1, GL_FALSE, mvp);
	glUniform4f(gLineLoc_uColor, r, g, b, a);
	CHECK_GL_ERROR();
	glDrawArrays(mode, 0, nverts);
	CHECK_GL_ERROR();
}

#endif /* __EMSCRIPTEN__ */
