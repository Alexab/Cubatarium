#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in ivec4 aJoints;
layout(location = 3) in vec4 aWeights;

uniform mat4 mvp_matrix;
uniform mat4 uBones[64];

out vec2 uv;

void main()
{
  mat4 skinMat = uBones[aJoints.x] * aWeights.x;
  skinMat += uBones[aJoints.y] * aWeights.y;
  skinMat += uBones[aJoints.z] * aWeights.z;
  skinMat += uBones[aJoints.w] * aWeights.w;
  vec4 skinned = skinMat * vec4(aPos, 1.0);
  gl_Position = mvp_matrix * skinned;
  uv = aTexCoord;
}
