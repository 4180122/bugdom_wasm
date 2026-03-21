/*
 * Unit tests for the Quesa-to-GLSL matrix pipeline.
 *
 * Conventions used in this codebase:
 *   - Game/Quesa matrices: row-vector, p' = p * M, stored as value[row][col].
 *   - Camera matrices (FillProjectionMatrix, FillLookAtMatrix): store M^T in
 *     value[][] so that glLoadMatrixf(value[0]) yields the correct GL matrix.
 *     The same bytes also work as row-vector matrices for Q3Point3D_Transform.
 *   - GLSL upload: memcpy of value[][] to glUniformMatrix4fv(GL_FALSE) makes
 *     GLSL interpret the bytes as column-major, giving mat4 = value^T.
 *   - Combined MVP must be formed as:  MVP_value = T_quesa * View * Proj
 *     so that GLSL sees  MVP_value^T = Proj^T * View^T * T_quesa^T = P*V*M_gl.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "QD3D.h"
#include "QD3DMath.h"
#include "gles3_matrix_upload.h"

#define EPSILON 1e-5f
#define ASSERT_NEAR(a, b) \
	do { \
		float _a = (a), _b = (b); \
		if (fabsf(_a - _b) > EPSILON) { \
			fprintf(stderr, "FAIL %s:%d: %.8f != %.8f\n", __FILE__, __LINE__, _a, _b); \
			assert(0); \
		} \
	} while (0)

/* ---- local helpers replicating game functions we test against ---- */

static void MatMul(const TQ3Matrix4x4* a, const TQ3Matrix4x4* b, TQ3Matrix4x4* result)
{
	TQ3Matrix4x4 tmp;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			tmp.value[i][j] = a->value[i][0]*b->value[0][j]
			                 + a->value[i][1]*b->value[1][j]
			                 + a->value[i][2]*b->value[2][j]
			                 + a->value[i][3]*b->value[3][j];
	*result = tmp;
}

static void MakeIdentity(TQ3Matrix4x4* m)
{
	memset(m, 0, sizeof(*m));
	m->value[0][0] = m->value[1][1] = m->value[2][2] = m->value[3][3] = 1.f;
}

static void FillProjectionMatrix(TQ3Matrix4x4* m, float fov, float aspect, float hither, float yon)
{
	float f = 1.0f / tanf(fov/2.0f);
#define M(x,y) m->value[x][y]
	M(0,0) = f/aspect;  M(1,0) = 0;  M(2,0) = 0;                          M(3,0) = 0;
	M(0,1) = 0;         M(1,1) = f;  M(2,1) = 0;                          M(3,1) = 0;
	M(0,2) = 0;         M(1,2) = 0;  M(2,2) = (yon+hither)/(hither-yon);  M(3,2) = 2*yon*hither/(hither-yon);
	M(0,3) = 0;         M(1,3) = 0;  M(2,3) = -1;                         M(3,3) = 0;
#undef M
}

static void FillLookAtMatrix(TQ3Matrix4x4* m,
		const TQ3Point3D* eye, const TQ3Point3D* target, const TQ3Vector3D* upDir)
{
	TQ3Vector3D forward;
	Q3Point3D_Subtract(eye, target, &forward);
	Q3Vector3D_Normalize(&forward, &forward);
	TQ3Vector3D left;
	Q3Vector3D_Cross(upDir, &forward, &left);
	Q3Vector3D_Normalize(&left, &left);
	TQ3Vector3D up;
	Q3Vector3D_Cross(&forward, &left, &up);

	float tx = Q3Vector3D_Dot((TQ3Vector3D*)eye, &left);
	float ty = Q3Vector3D_Dot((TQ3Vector3D*)eye, &up);
	float tz = Q3Vector3D_Dot((TQ3Vector3D*)eye, &forward);

#define M(x,y) m->value[x][y]
	M(0,0) = left.x;     M(1,0) = left.y;     M(2,0) = left.z;     M(3,0) = -tx;
	M(0,1) = up.x;       M(1,1) = up.y;       M(2,1) = up.z;       M(3,1) = -ty;
	M(0,2) = forward.x;  M(1,2) = forward.y;  M(2,2) = forward.z;  M(3,2) = -tz;
	M(0,3) = 0;           M(1,3) = 0;           M(2,3) = 0;          M(3,3) = 1;
#undef M
}

/* Corrected ortho: stores O^T (translations in row 3) matching FillProjectionMatrix. */
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

/* Simulate what GLSL does: interpret upload[] as column-major mat4, multiply by pos. */
static void GlslMultiply(const float upload[16], const float pos[4], float result[4])
{
	for (int row = 0; row < 4; row++) {
		result[row] = 0;
		for (int col = 0; col < 4; col++)
			result[row] += upload[col * 4 + row] * pos[col];
	}
}

/* Standard column-vector reference: compute P * V * M * pos using textbook math. */
static void ReferenceTransform(
		const TQ3Matrix4x4* proj_value,
		const TQ3Matrix4x4* view_value,
		const TQ3Matrix4x4* model_quesa,
		const float pos[4], float result[4])
{
	/*
	 * proj_value = P^T, view_value = V^T (camera convention).
	 * model_quesa: row-vector convention.  M_gl = model_quesa^T.
	 * We want P * V * M_gl * pos.
	 *
	 * Build each GL matrix from its value[][] representation, then multiply.
	 */
	float P[4][4], V[4][4], Mgl[4][4];

	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) {
			P[i][j] = proj_value->value[j][i];
			V[i][j] = view_value->value[j][i];
			Mgl[i][j] = model_quesa->value[j][i];
		}

	/* PV = P * V */
	float PV[4][4] = {{0}};
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			for (int k = 0; k < 4; k++)
				PV[i][j] += P[i][k] * V[k][j];

	/* PVM = PV * Mgl */
	float PVM[4][4] = {{0}};
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			for (int k = 0; k < 4; k++)
				PVM[i][j] += PV[i][k] * Mgl[k][j];

	for (int i = 0; i < 4; i++) {
		result[i] = 0;
		for (int j = 0; j < 4; j++)
			result[i] += PVM[i][j] * pos[j];
	}
}

static void TransformDirWorldToEye(const TQ3Matrix4x4* view,
		float wx, float wy, float wz, float* ox, float* oy, float* oz)
{
	float x = view->value[0][0]*wx + view->value[1][0]*wy + view->value[2][0]*wz;
	float y = view->value[0][1]*wx + view->value[1][1]*wy + view->value[2][1]*wz;
	float z = view->value[0][2]*wx + view->value[1][2]*wy + view->value[2][2]*wz;
	float len = sqrtf(x*x + y*y + z*z);
	if (len > 1e-8f) { x /= len; y /= len; z /= len; }
	*ox = x; *oy = y; *oz = z;
}

/* ====================================================================== */

static int tests_run = 0;
#define RUN(fn) do { printf("  %-50s", #fn); fn(); tests_run++; printf("ok\n"); } while(0)

/* ---- Test 1: upload function is memcpy ---- */
static void test_upload_is_memcpy(void)
{
	TQ3Matrix4x4 M;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			M.value[i][j] = (float)(i * 4 + j + 1);

	float out[16];
	BugdomUploadQuesaMat4ForGLSL(&M, out);

	for (int i = 0; i < 16; i++)
		ASSERT_NEAR(out[i], ((float*)M.value)[i]);
}

/* ---- Test 2: identity matrix survives round-trip ---- */
static void test_identity_roundtrip(void)
{
	TQ3Matrix4x4 I;
	MakeIdentity(&I);
	float out[16];
	BugdomUploadQuesaMat4ForGLSL(&I, out);

	float pos[4] = {3.f, -7.f, 11.f, 1.f};
	float result[4];
	GlslMultiply(out, pos, result);
	ASSERT_NEAR(result[0], 3.f);
	ASSERT_NEAR(result[1], -7.f);
	ASSERT_NEAR(result[2], 11.f);
	ASSERT_NEAR(result[3], 1.f);
}

/* ---- Test 3: translation matrix (camera convention: stored as M^T) ---- */
static void test_translation_camera_convention(void)
{
	TQ3Matrix4x4 T;
	MakeIdentity(&T);
	T.value[3][0] = 10.f;
	T.value[3][1] = 20.f;
	T.value[3][2] = 30.f;

	float out[16];
	BugdomUploadQuesaMat4ForGLSL(&T, out);

	float pos[4] = {1.f, 2.f, 3.f, 1.f};
	float result[4];
	GlslMultiply(out, pos, result);

	/* GLSL mat4 = T^T, so mat4 has translation in last column.
	 * mat4 * pos = pos + translation (for w=1). */
	ASSERT_NEAR(result[0], 11.f);
	ASSERT_NEAR(result[1], 22.f);
	ASSERT_NEAR(result[2], 33.f);
	ASSERT_NEAR(result[3], 1.f);
}

/* ---- Test 4: MVP multiplication order ---- */
static void test_mvp_multiplication_order(void)
{
	TQ3Matrix4x4 Proj, View, Model;

	FillProjectionMatrix(&Proj, 1.2f, 1.5f, 0.1f, 1000.f);

	TQ3Point3D eye = {0, 5, 10};
	TQ3Point3D target = {0, 0, 0};
	TQ3Vector3D up = {0, 1, 0};
	FillLookAtMatrix(&View, &eye, &target, &up);

	MakeIdentity(&Model);
	Model.value[3][0] = 3.f;
	Model.value[3][1] = -1.f;
	Model.value[3][2] = 2.f;

	/* Correct order: MVP = Model * View * Proj */
	TQ3Matrix4x4 MV, MVP;
	MatMul(&Model, &View, &MV);
	MatMul(&MV, &Proj, &MVP);

	float upload[16];
	BugdomUploadQuesaMat4ForGLSL(&MVP, upload);

	float pos[4] = {1.f, 2.f, -3.f, 1.f};
	float glsl_result[4], ref_result[4];
	GlslMultiply(upload, pos, glsl_result);
	ReferenceTransform(&Proj, &View, &Model, pos, ref_result);

	for (int i = 0; i < 4; i++)
		ASSERT_NEAR(glsl_result[i], ref_result[i]);
}

/* ---- Test 5: wrong MVP order gives wrong results ---- */
static void test_wrong_mvp_order_fails(void)
{
	TQ3Matrix4x4 Proj, View, Model;

	FillProjectionMatrix(&Proj, 1.0f, 1.333f, 1.f, 500.f);

	TQ3Point3D eye = {10, 10, 10};
	TQ3Point3D target = {0, 0, 0};
	TQ3Vector3D up = {0, 1, 0};
	FillLookAtMatrix(&View, &eye, &target, &up);

	MakeIdentity(&Model);
	Model.value[3][0] = 5.f;

	/* WRONG order: Proj * View * T (as the buggy code did) */
	TQ3Matrix4x4 MV_wrong, MVP_wrong;
	MatMul(&View, &Model, &MV_wrong);
	MatMul(&Proj, &MV_wrong, &MVP_wrong);

	/* CORRECT order */
	TQ3Matrix4x4 MV_right, MVP_right;
	MatMul(&Model, &View, &MV_right);
	MatMul(&MV_right, &Proj, &MVP_right);

	float upload_wrong[16], upload_right[16];
	BugdomUploadQuesaMat4ForGLSL(&MVP_wrong, upload_wrong);
	BugdomUploadQuesaMat4ForGLSL(&MVP_right, upload_right);

	float pos[4] = {1.f, 0.f, 0.f, 1.f};
	float result_wrong[4], result_right[4], ref[4];

	GlslMultiply(upload_wrong, pos, result_wrong);
	GlslMultiply(upload_right, pos, result_right);
	ReferenceTransform(&Proj, &View, &Model, pos, ref);

	/* Right order matches reference */
	for (int i = 0; i < 4; i++)
		ASSERT_NEAR(result_right[i], ref[i]);

	/* Wrong order does NOT match reference (at least one component differs) */
	int any_differ = 0;
	for (int i = 0; i < 4; i++)
		if (fabsf(result_wrong[i] - ref[i]) > EPSILON)
			any_differ = 1;
	assert(any_differ && "wrong order should produce different results");
}

/* ---- Test 6: ortho matrix convention ---- */
static void test_ortho_convention(void)
{
	TQ3Matrix4x4 Ortho;
	Matrix4_Ortho(&Ortho, -10.f, 10.f, -5.f, 5.f, -1.f, 1.f);

	float upload[16];
	BugdomUploadQuesaMat4ForGLSL(&Ortho, upload);

	/* Center of the ortho volume should map to origin. */
	float center[4] = {0.f, 0.f, 0.f, 1.f};
	float result[4];
	GlslMultiply(upload, center, result);
	ASSERT_NEAR(result[0], 0.f);
	ASSERT_NEAR(result[1], 0.f);
	ASSERT_NEAR(result[2], 0.f);
	ASSERT_NEAR(result[3], 1.f);

	/* Right edge should map to x=+1. */
	float right_edge[4] = {10.f, 0.f, 0.f, 1.f};
	GlslMultiply(upload, right_edge, result);
	ASSERT_NEAR(result[0], 1.f);
	ASSERT_NEAR(result[3], 1.f);

	/* Top edge should map to y=+1. */
	float top_edge[4] = {0.f, 5.f, 0.f, 1.f};
	GlslMultiply(upload, top_edge, result);
	ASSERT_NEAR(result[1], 1.f);

	/* glOrtho convention: z_ndc = -2/(f-n)*z - (f+n)/(f-n).
	 * With n=-1,f=1: z_ndc = -z.  So z=-1 → +1 and z=1 → -1. */
	float zn_pt[4] = {0.f, 0.f, -1.f, 1.f};
	GlslMultiply(upload, zn_pt, result);
	ASSERT_NEAR(result[2], 1.f);

	float zf_pt[4] = {0.f, 0.f, 1.f, 1.f};
	GlslMultiply(upload, zf_pt, result);
	ASSERT_NEAR(result[2], -1.f);
}

/* ---- Test 7: ortho with asymmetric bounds and MVP path ---- */
static void test_ortho_asymmetric_mvp(void)
{
	TQ3Matrix4x4 Ortho, View;
	Matrix4_Ortho(&Ortho, 0.f, 640.f, 480.f, 0.f, -1.f, 1.f);
	MakeIdentity(&View);

	/* No model transform: MVP = View * Proj (View = I, so MVP = Proj) */
	TQ3Matrix4x4 MVP;
	MatMul(&View, &Ortho, &MVP);

	float upload[16];
	BugdomUploadQuesaMat4ForGLSL(&MVP, upload);

	/* Top-left (0,0) should map to (-1, +1) in NDC (y-flip because bottom > top). */
	float tl[4] = {0.f, 0.f, 0.f, 1.f};
	float result[4];
	GlslMultiply(upload, tl, result);
	ASSERT_NEAR(result[0], -1.f);
	ASSERT_NEAR(result[1], 1.f);

	/* Bottom-right (640, 480) should map to (+1, -1). */
	float br[4] = {640.f, 480.f, 0.f, 1.f};
	GlslMultiply(upload, br, result);
	ASSERT_NEAR(result[0], 1.f);
	ASSERT_NEAR(result[1], -1.f);
}

/* ---- Test 8: perspective projection maps far plane correctly ---- */
static void test_perspective_far_plane(void)
{
	TQ3Matrix4x4 Proj;
	float hither = 0.5f, yon = 100.f;
	FillProjectionMatrix(&Proj, (float)M_PI / 2.f, 1.f, hither, yon);

	float upload[16];
	BugdomUploadQuesaMat4ForGLSL(&Proj, upload);

	/* A point at z = -yon (in view space, looking down -Z) should have z_ndc = +1 after w-divide. */
	float pt[4] = {0.f, 0.f, -yon, 1.f};
	float result[4];
	GlslMultiply(upload, pt, result);
	float z_ndc = result[2] / result[3];
	ASSERT_NEAR(z_ndc, 1.f);

	/* Near plane: z = -hither should have z_ndc = -1. */
	float near_pt[4] = {0.f, 0.f, -hither, 1.f};
	GlslMultiply(upload, near_pt, result);
	z_ndc = result[2] / result[3];
	ASSERT_NEAR(z_ndc, -1.f);
}

/* ---- Test 9: TransformDirWorldToEye correctness ---- */
static void test_transform_dir_world_to_eye(void)
{
	TQ3Matrix4x4 View;
	TQ3Point3D eye = {0, 0, 5};
	TQ3Point3D target = {0, 0, 0};
	TQ3Vector3D up = {0, 1, 0};
	FillLookAtMatrix(&View, &eye, &target, &up);

	/* Camera at (0,0,5) looking at origin: left=(1,0,0), up=(0,1,0), backward=(0,0,1).
	 * World +X aligns with view +X (left/right axis). */
	float ox, oy, oz;
	TransformDirWorldToEye(&View, 1, 0, 0, &ox, &oy, &oz);
	ASSERT_NEAR(ox, 1.f);
	ASSERT_NEAR(oy, 0.f);
	ASSERT_NEAR(oz, 0.f);

	/* World +Y aligns with view +Y (up axis). */
	TransformDirWorldToEye(&View, 0, 1, 0, &ox, &oy, &oz);
	ASSERT_NEAR(ox, 0.f);
	ASSERT_NEAR(oy, 1.f);
	ASSERT_NEAR(oz, 0.f);

	/* World +Z aligns with view +Z (backward = eye - target direction). */
	TransformDirWorldToEye(&View, 0, 0, 1, &ox, &oy, &oz);
	ASSERT_NEAR(ox, 0.f);
	ASSERT_NEAR(oy, 0.f);
	ASSERT_NEAR(oz, 1.f);
}

/* ---- Test 10: view matrix transforms eye to origin ---- */
static void test_view_transforms_eye_to_origin(void)
{
	TQ3Matrix4x4 View;
	TQ3Point3D eye = {3, 7, -2};
	TQ3Point3D target = {0, 0, 0};
	TQ3Vector3D up = {0, 1, 0};
	FillLookAtMatrix(&View, &eye, &target, &up);

	float upload[16];
	BugdomUploadQuesaMat4ForGLSL(&View, upload);

	float eye_pos[4] = {eye.x, eye.y, eye.z, 1.f};
	float result[4];
	GlslMultiply(upload, eye_pos, result);

	ASSERT_NEAR(result[0], 0.f);
	ASSERT_NEAR(result[1], 0.f);
	ASSERT_NEAR(result[2], 0.f);
	ASSERT_NEAR(result[3], 1.f);
}

/* ---- Test 11: full pipeline with nontrivial model + view + proj ---- */
static void test_full_pipeline(void)
{
	TQ3Matrix4x4 Proj, View, Model;

	FillProjectionMatrix(&Proj, 1.0f, 16.f/9.f, 0.1f, 500.f);

	TQ3Point3D eye = {5, 10, 15};
	TQ3Point3D target = {0, 0, 0};
	TQ3Vector3D up = {0, 1, 0};
	FillLookAtMatrix(&View, &eye, &target, &up);

	/* Quesa row-vector model transform: translate (2,0,0) then rotate 45 deg around Y */
	MakeIdentity(&Model);
	float angle = (float)M_PI / 4.f;
	float c = cosf(angle), s = sinf(angle);
	Model.value[0][0] = c;   Model.value[0][2] = s;
	Model.value[2][0] = -s;  Model.value[2][2] = c;
	Model.value[3][0] = 2.f;

	/* Correct order */
	TQ3Matrix4x4 MV, MVP;
	MatMul(&Model, &View, &MV);
	MatMul(&MV, &Proj, &MVP);

	float upload[16];
	BugdomUploadQuesaMat4ForGLSL(&MVP, upload);

	float test_points[][4] = {
		{0.f, 0.f, 0.f, 1.f},
		{1.f, 0.f, 0.f, 1.f},
		{0.f, 1.f, 0.f, 1.f},
		{0.f, 0.f, 1.f, 1.f},
		{-3.f, 2.5f, 7.f, 1.f},
	};

	for (int p = 0; p < 5; p++) {
		float glsl[4], ref[4];
		GlslMultiply(upload, test_points[p], glsl);
		ReferenceTransform(&Proj, &View, &Model, test_points[p], ref);
		for (int i = 0; i < 4; i++)
			ASSERT_NEAR(glsl[i], ref[i]);
	}
}

/* ---- Test 12: MV matrix alone (for lighting/fog eye-space computations) ---- */
static void test_modelview_eye_space(void)
{
	TQ3Matrix4x4 View, Model;

	TQ3Point3D eye = {0, 0, 10};
	TQ3Point3D target = {0, 0, 0};
	TQ3Vector3D up = {0, 1, 0};
	FillLookAtMatrix(&View, &eye, &target, &up);

	MakeIdentity(&Model);
	Model.value[3][0] = 5.f;

	TQ3Matrix4x4 MV;
	MatMul(&Model, &View, &MV);

	float upload[16];
	BugdomUploadQuesaMat4ForGLSL(&MV, upload);

	/* Model origin (0,0,0) with translation (5,0,0) ends up at world (5,0,0).
	 * Camera at (0,0,10) looking at origin:
	 *   left/right = (1,0,0), up = (0,1,0), backward = (0,0,1).
	 *   view_x = dot((5,0,0)-(0,0,10), right) = dot((5,0,-10), (1,0,0)) = 5
	 *   view_y = 0
	 *   view_z = dot((5,0,-10), (0,0,1)) = -10 */
	float origin[4] = {0.f, 0.f, 0.f, 1.f};
	float result[4];
	GlslMultiply(upload, origin, result);

	ASSERT_NEAR(result[0], 5.f);
	ASSERT_NEAR(result[1], 0.f);
	ASSERT_NEAR(result[2], -10.f);
	ASSERT_NEAR(result[3], 1.f);
}

/* ====================================================================== */

int main(void)
{
	printf("gles_matrix_test:\n");
	RUN(test_upload_is_memcpy);
	RUN(test_identity_roundtrip);
	RUN(test_translation_camera_convention);
	RUN(test_mvp_multiplication_order);
	RUN(test_wrong_mvp_order_fails);
	RUN(test_ortho_convention);
	RUN(test_ortho_asymmetric_mvp);
	RUN(test_perspective_far_plane);
	RUN(test_transform_dir_world_to_eye);
	RUN(test_view_transforms_eye_to_origin);
	RUN(test_full_pipeline);
	RUN(test_modelview_eye_space);
	printf("all %d tests passed.\n", tests_run);
	return 0;
}
