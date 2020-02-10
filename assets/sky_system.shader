imports : [
    { name: "sun_direction" type: "float3" }
    { name: "sun_intensity" type: "float" }
    { name: "sun_size" type: "float" }
    { name: "beta_r" type: "float3" }
    { name: "beta_m" type: "float3" }
    { name: "mie_g" type: "float" }
    { name: "turbidity" type: "float" }
]

common : [[
    #define PI 3.1415926

    float rayleigh_phase(in float cos_theta) {
        return (3.0 / (16.0 * PI)) * (1.0 + cos_theta * cos_theta);
    }

    float hg_phase(in float cos_theta, in float g) {
        const float g2 = g * g;
        return (1.0 / (4.0 * PI)) * ((1.0 - g2) / pow(1.0 - 2.0 * g * cos_theta + g2, 1.5));
    }

    float sun_intensity(in float cos_theta) {
        const float steepness = 1.4142136; // sqrt(2)
        return load_sun_intensity() * max(0, 1.0 - exp(-((PI / steepness - acos(cos_theta)) / steepness)));
    }

    float3 get_sky_color_physical(in float3 dir, out float3 reflected /* sun disk and and reflectance */) {
        float3 sun_dir = normalize(load_sun_direction());
        float sun_e = sun_intensity(sun_dir.y);

        float3 beta_r = load_beta_r();

        const float3 wavelength = float3(680e-9, 550e-9, 450e-9);
        const float s = 4.0 - 2.0;
        const float c = (0.2 * load_turbidity()) * 10e-18;
        float3 term = 0.434 * c * PI * pow((2.0 * PI) / wavelength, float3(s, s, s));
        float3 beta_m = load_beta_m() * term;

        // optical length
        float zenith_angle = acos(max(0.0, dir.y));
        float denom = cos(zenith_angle) + 0.15 * pow(93.885 - ((zenith_angle * 180.0) / PI), -1.253);
        float s_r = 8.4e3 / denom;
        float s_m = 1.25e3 / denom;

        // combined extinction factor
        float3 ex = exp(-beta_r * s_r + beta_m * s_m);

        float cos_theta = dot(sun_dir, dir);
        float r_phase = rayleigh_phase(cos_theta);
        float m_phase = hg_phase(cos_theta, load_mie_g());
        float3 beta_r_theta = beta_r * r_phase;
        float3 beta_m_theta = beta_m * m_phase;

        float3 betas = (beta_r_theta + beta_m_theta) / (beta_r + beta_m);
        float3 inscattered = pow(sun_e * betas * (1.0 - ex), float3(1.5, 1.5, 1.5));
        inscattered *= lerp(float3(1.0, 1.0, 1.0), pow(sun_e * betas * ex, float3(0.5, 0.5, 0.5)), clamp(pow(1.0 - sun_dir.y, 5.0), 0, 1));

        // input parameter
        reflected = ex * 0.1;

        // sun disk
        float sun_angular_cos = 1 - (load_sun_size() * 1e-4);
        float sun_disk = smoothstep(sun_angular_cos, sun_angular_cos + 1e-10, cos_theta);
        reflected += (sun_e * 150000.0 * ex * sun_disk);

        return inscattered;
    }
]]

system : {    
}