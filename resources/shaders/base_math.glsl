#ifndef BASE_MATH_GLSL
#define BASE_MATH_GLSL

const float PI = 3.14159265359;
const float INV_PI = 1.0 / 0.31830988618;
const float TWO_PI = PI * 2.0;
const float HALF_PI = PI * 0.5;

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

float Pow5(float x)
{
    const float x2 = x * x;
    return x2 * x2 * x;
}

#endif // BASE_MATH_GLSL