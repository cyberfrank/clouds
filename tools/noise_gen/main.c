#include <foundation/carray.inl>
#include <foundation/temp_allocator.h>
#include <foundation/log.h>
#include <foundation/math.inl>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.inl"

#include "noise.inl"

typedef void(*tm_write_pixel_func)(tm_vec3_t *p, uint8_t *output, int output_width);

float remap(float value, float old_min, float old_max, float new_min, float new_max)
{
    return new_min + (((value - old_min) / (old_max - old_min)) * (new_max - new_min));
}

void worley_test(tm_vec3_t *p, uint8_t *output, int output_width)
{
    const float cell_count = 4.0f;

    float noise = 1.0f - worley_noise_3(*p, cell_count);
    noise *= 255.0f;

    output[0] = (uint8_t)noise;
    output[1] = (uint8_t)noise;
    output[2] = (uint8_t)noise;
}

void base_noise(tm_vec3_t *p, uint8_t *output, int output_width)
{
    float noise = perlin_noise_fbm(*p, 8.0f, 3);

    output[0] = (uint8_t)(noise * 255.0f);
    output[1] = (uint8_t)(noise * 255.0f);
    output[2] = (uint8_t)(noise * 255.0f);
}

void create_texture_3d(const char *file, int size, int components, tm_write_pixel_func writer)
{
    TM_INIT_TEMP_ALLOCATOR(ta);

    const int stride = size * sizeof(uint8_t) * components;
    const int volume_size = stride * size * size;

    uint8_t *texels = tm_temp_alloc(ta, volume_size);
    uint8_t *pixel = tm_temp_alloc(ta, components);

    for (int s = 0; s < size; ++s)
    {
        for (int t = 0; t < size; ++t)
        {
            for (int r = 0; r < size; ++r)
            {
                tm_vec3_t pos = tm_vec3_mul(TM_MAKE_VEC3((float)s, (float)t, (float)r), 1.0f / size);

                writer(&pos, pixel, components);

                int idx = r * size * size + t * size + s;
                memcpy(&texels[idx * components], pixel, sizeof(uint8_t) * components);
            }
        }
    }

    tm_logger_api->printf(TM_LOG_TYPE_INFO, "Created '%s' volume texture (%i KB)\n", file, volume_size / 1000);

    stbi_write_tga(file, size * size, size, components, texels);
    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);

}

int main(int argv, char **argc)
{
    tm_logger_api->add_logger(tm_logger_api->printf_logger);
    
    // create_texture_3d("base.tga", 128, 3, worley_test);
    create_texture_3d("perlin.tga", 128, 3, base_noise);
    create_texture_3d("detail.tga", 32, 3, worley_test);

    tm_logger_api->printf(TM_LOG_TYPE_INFO, "Done.\n");
    return 0;
}