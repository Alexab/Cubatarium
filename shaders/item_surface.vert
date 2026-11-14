#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec2 aUv;
layout(location=2) in vec3 aNormal;
uniform mat4 uMvp;
uniform mat4 uModel;
out vec2 vUv;
out vec3 vNormal;
out vec3 vPosition;
void main()
{
    gl_Position=uMvp*vec4(aPosition,1.0);
    vPosition=(uModel*vec4(aPosition,1.0)).xyz;
    vNormal=transpose(inverse(mat3(uModel)))*aNormal;
    vUv=aUv;
}
