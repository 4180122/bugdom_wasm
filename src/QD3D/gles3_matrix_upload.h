#pragma once

#include "QD3D.h"

/*
 * Camera matrices (FillProjectionMatrix, FillLookAtMatrix) already store M^T in
 * value[row][col] so that glLoadMatrixf(value[0]) yields the correct column-major
 * GL matrix.  Model transforms are in Quesa row-vector convention.  After the
 * combined MVP is formed with the correct multiplication order
 * (MVP_value = T_quesa * View_value * Proj_value), the flat row-major bytes of
 * MVP_value, when interpreted as column-major by glUniformMatrix4fv(GL_FALSE),
 * automatically produce the correct GLSL mat4 = P * V * M_gl.
 *
 * This function is therefore a plain memcpy of the 16 floats.
 */
static inline void BugdomUploadQuesaMat4ForGLSL(const TQ3Matrix4x4* m, float out[16])
{
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			out[j * 4 + i] = m->value[j][i];
}
