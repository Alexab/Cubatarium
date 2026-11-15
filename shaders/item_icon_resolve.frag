#version 330 core
uniform sampler2D uSamples;
out vec4 FragColor;
vec3 linearColor(vec3 c)
{
    return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));
}
vec3 displayColor(vec3 c)
{
    return mix(c*12.92,1.055*pow(max(c,vec3(0.0)),vec3(1.0/2.4))-0.055,step(vec3(0.0031308),c));
}
void main()
{
    ivec2 origin=ivec2(gl_FragCoord.xy)*4;
    vec4 sum=vec4(0.0);
    for(int y=0;y<4;++y)
        for(int x=0;x<4;++x)
        {
            vec4 c=texelFetch(uSamples,origin+ivec2(x,y),0);
            sum+=vec4(linearColor(c.rgb),c.a);
        }
    float weight=1.0/16.0;
    FragColor=vec4(displayColor(sum.rgb*weight),sum.a*weight);
}
