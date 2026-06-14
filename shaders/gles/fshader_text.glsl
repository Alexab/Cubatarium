#version 300 es
precision mediump float;
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D textTexture;
uniform vec3 textColor;

void main()
{
    // Получаем значение из текстуры (R канал)
    float alpha = texture(textTexture, TexCoord).r;
    
    // Применяем цвет текста с прозрачностью
    FragColor = vec4(textColor, alpha);
}
