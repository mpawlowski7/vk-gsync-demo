#version 450

layout (location = 0) out vec3 outColor;

// Draw rectangle with 5% screen width
const vec3 vertices[6] = vec3[6](
    vec3(-1.0,-1.0, 0.0),
    vec3(-1.0, 1.0, 0.0),
    vec3(-0.9, 1.0, 0.0),
    vec3(-1.0,-1.0, 0.0),
    vec3(-0.9,-1.0, 0.0),
    vec3(-0.9, 1.0, 0.0)
);

layout (push_constant) uniform constants
{
  float x;
} currentStep;

void main()
{
    gl_Position = vec4(vertices[gl_VertexIndex].x + currentStep.x, vertices[gl_VertexIndex].yz, 1.0);
    outColor = vec3(0.9f, 0.9f, 0.9f);
}
