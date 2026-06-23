#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif

varying vec2 v_texcoord;

uniform vec4 skyColor; // Цвет неба, передаваемый из C++

void main()
{
    // Создаем градиент от основного цвета вверху к более светлому внизу
    vec3 skyTop = skyColor.rgb;    // Основной цвет вверху
    vec3 skyBottom = skyColor.rgb * 1.3; // Более светлый цвет внизу (увеличиваем яркость)

    // Интерполируем цвет на основе Y координаты текстуры
    vec3 finalColor = mix(skyBottom, skyTop, v_texcoord.y);

    gl_FragColor = vec4(finalColor, 1.0);
}
