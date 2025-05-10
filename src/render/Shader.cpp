#include "Shader.hpp"

SShader::~SShader() {
    destroy();
}

void SShader::destroy() {
    if (program == 0)
        return;

    if (shaderVao)
        glDeleteVertexArrays(1, &shaderVao);

    if (shaderVboPos)
        glDeleteBuffers(1, &shaderVboPos);

    if (shaderVboUv)
        glDeleteBuffers(1, &shaderVboUv);

    glDeleteProgram(program);
    program = 0;
}
