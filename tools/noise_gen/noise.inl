#include <foundation/api_types.h>
#include <foundation/math.inl>
#include <foundation/macros.h>

static inline tm_vec3_t tm_vec3_fade(tm_vec3_t v) 
{
    const tm_vec3_t res = {
        v.x * v.x * v.x * (v.x * (v.x * 6.0f - 15.0f) + 10.0f),
        v.y * v.y * v.y * (v.y * (v.y * 6.0f - 15.0f) + 10.0f),
        v.z * v.z * v.z * (v.z * (v.z * 6.0f - 15.0f) + 10.0f)
    };
    return res;
}

static inline tm_vec3_t tm_vec3_mod(tm_vec3_t v, float f)
{
    const tm_vec3_t res = {
        v.x - f * floorf(v.x / f),
        v.y - f * floorf(v.y / f),
        v.z - f * floorf(v.z / f)
    };
    return res;
}

float hash_noise(float x, float y, float z)
{
    float n = sinf(tm_vec3_dot(TM_MAKE_VEC3(x, y, z), TM_MAKE_VEC3(12.9898f, 78.233f, 47.387f))) * 43758.5453f;
    n = n - floorf(n);
    return n;
}

tm_vec3_t hash_vec(float x, float y, float z)
{
    static const tm_vec3_t basis[12] = {
        { 0.7071f, 0.7071f, 0.0f },
        { 0.7071f, -0.7071f, 0.0f },
        { -0.7071f, 0.7071f, 0.0f },
        { -0.7071f, -0.7071f, 0.0f },
        { 0.7071f, 0.0f, 0.7071f },
        { 0.7071f, 0.0f, -0.7071f },
        {-0.7071f, 0.0f, 0.7071f },
        {-0.7071f, 0.0f, -0.7071f },
        { 0.0f, 0.7071f, 0.7071f },
        { 0.0f, 0.7071f, -0.7071f },
        { 0.0f, -0.7071f, 0.7071f },
        { 0.0f, -0.7071f, -0.7071f },
    };
    float a = hash_noise(x, y, z);
    return basis[(int)floorf(12.0f * a)];
}

float perlin_noise_3(tm_vec3_t pt, float frequency)
{
    pt = tm_vec3_mul(pt, frequency);
    tm_vec3_t f = TM_MAKE_VEC3(floorf(pt.x), floorf(pt.y), floorf(pt.z));
    tm_vec3_t r = tm_vec3_sub(pt, f);
    tm_vec3_t u = tm_vec3_fade(r);

    tm_vec3_t g = tm_vec3_add(f, TM_MAKE_VEC3(1, 1, 1));
    g = tm_vec3_mod(g, frequency);
    f = tm_vec3_mod(f, frequency);
    
    float nnn = tm_vec3_dot(hash_vec(f.x, f.y, f.z), r);
    float nnp = tm_vec3_dot(hash_vec(f.x, f.y, g.z), tm_vec3_sub(r, TM_MAKE_VEC3(0, 0, 1)));
    float npn = tm_vec3_dot(hash_vec(f.x, g.y, f.z), tm_vec3_sub(r, TM_MAKE_VEC3(0, 1, 0)));
    float npp = tm_vec3_dot(hash_vec(f.x, g.y, g.z), tm_vec3_sub(r, TM_MAKE_VEC3(0, 1, 1)));
    float pnn = tm_vec3_dot(hash_vec(g.x, f.y, f.z), tm_vec3_sub(r, TM_MAKE_VEC3(1, 0, 0)));
    float pnp = tm_vec3_dot(hash_vec(g.x, f.y, g.z), tm_vec3_sub(r, TM_MAKE_VEC3(1, 0, 1)));
    float ppn = tm_vec3_dot(hash_vec(g.x, g.y, f.z), tm_vec3_sub(r, TM_MAKE_VEC3(1, 1, 0)));
    float ppp = tm_vec3_dot(hash_vec(g.x, g.y, g.z), tm_vec3_sub(r, TM_MAKE_VEC3(1, 1, 1)));

    float nn = tm_lerp(nnn, pnn, u.x);
    float np = tm_lerp(nnp, pnp, u.x);
    float pn = tm_lerp(npn, ppn, u.x);
    float pp = tm_lerp(npp, ppp, u.x);

    float n = tm_lerp(nn, pn, u.y);
    float p = tm_lerp(np, pp, u.y);

    return tm_lerp(n, p, u.z);
}

float perlin_noise_fbm(tm_vec3_t pt, float frequency, int octaves)
{
    float sum = 0.0f;
    float weight = 0.5f;
    float weight_sum = 0.0f;

    for (int o = 0; o < octaves; ++o)
    {
        sum += weight * perlin_noise_3(pt, frequency);

        weight_sum += weight;
        weight *= weight;
        frequency *= 2;
    }
    
    return (sum / weight_sum) * 0.5f + 0.5f;
}

float worley_noise_3(tm_vec3_t pt, float cell_count)
{
    return 0;
}