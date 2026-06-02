#ifndef GREEDY_SHADER_MODE_H
#define GREEDY_SHADER_MODE_H

namespace cutum {

enum class GreedyShaderMode {
 TransparentColor = 0,
 ShellDepthPrepass = 1,
 FuzzyOnly = 2,
};

int GreedyShaderModeToUniform(GreedyShaderMode mode);

} // namespace cutum

#endif
