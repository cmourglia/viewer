#ifndef CUBEMAP_HELPERS_GLSL
#define CUBEMAP_HELPERS_GLSL

vec3 CubeCoordToWorld(ivec3 cubeCoord, vec2 cubemapSize)
{
    vec2 texcoord = vec2(cubeCoord.xy) / cubemapSize;
    texcoord = texcoord * 2.0 - 1.0;
    texcoord.y *= -1.0;

    switch (cubeCoord.z)
    {
        case 0: return vec3(1.0, texcoord.y, -texcoord.x);
        case 1: return vec3(-1.0, texcoord.y, texcoord.x);
        case 2: return vec3(texcoord.x, 1.0, -texcoord.y);
        case 3: return vec3(texcoord.x, -1.0, texcoord.y);
        case 4: return vec3(texcoord.x, texcoord.y, 1.0);
        case 5: return vec3(-texcoord.x, texcoord.y, -1.0);
    }

    return vec3(0.0);
}

ivec3 TexCoordToCube(vec3 texcoord, vec2 cubemapSize)
{
    vec3 texcoordAbs = abs(texcoord);
    texcoord /= max(max(texcoordAbs.x, texcoordAbs.y), texcoordAbs.z);

    float cubeFace;
    vec2 uvCoord;
    if (texcoordAbs.x > texcoordAbs.y && texcoordAbs.x > texcoordAbs.z)
    {
        float negx = step(texcoord.x, 0.0);
        uvCoord = mix(-texcoord.zy, vec2(texcoord.z, -texcoord.y), negx);
        cubeFace = negx;
    }
    else if (texcoordAbs.y > texcoordAbs.z)
    {
        float negy =  step(texcoord.y, 0.0);
        uvCoord = mix(texcoord.xz, vec2(texcoord.x, -texcoord.z), negy);
        cubeFace = 2.0 + negy;
    }
    else
    {
        float negz = step(texcoord.z, 0.0);
        uvCoord = mix(vec2(texcoord.x, -texcoord.y), -texcoord.xy, negz);
        cubeFace = 4.0 + negz;
    }

    uvCoord = (uvCoord + 1.0) * 0.5;
    uvCoord = uvCoord * cubemapSize;
    uvCoord = clamp(uvCoord, vec2(0.0), cubemapSize - vec2(1.0));
    return ivec3(ivec2(uvCoord), int(cubeFace));
}

#endif // CUBEMAP_HELPERS_GLSL