#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif

uniform float alpha;

uniform sampler2D textureOne;
uniform sampler2D textureTwo;

varying vec2 v_texcoord;

vec4 texOneColor;
vec4 texTwoColor;

void main()
{
  texOneColor = texture2D(textureOne, v_texcoord);
  texTwoColor = texture2D(textureTwo, v_texcoord);

  gl_FragColor = mix(texOneColor, texTwoColor, alpha);
}
