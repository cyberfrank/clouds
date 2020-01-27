imports : [
    { name: "sun_position" type: "float3" }
    { name: "sun_direction" type: "float3" }
    { name: "beta_r" type: "float3" }
    { name: "beta_m" type: "float3" }
    { name: "mie_g" type: "float" }
]

common : [[
    #define PI 3.1415926
    #define SUN_ANGULAR_COS 0.9999567

    float phase_function_r(in float mu) {
        return (3.0 / (16.0 * PI)) * (1.0 + mu * mu);
    }

    float phase_function_m(in float mu, in float g) {
        float g2 = g * g;
        float inv = 1.0 / pow(1.0 - 2.0 * g * mu + g2, 1.5);
        return (1.0 / (4.0 * PI)) * ((1.0 - g2) * inv);
    }

    float3 get_atmosphere_color_physical(in float3 dir) {
        float3 color = float3(0, 0, 0);

        float3 sun_pos = load_sun_position();
        float3 sun_dir = load_sun_direction();
        float intensity = 1.0;

        float3 beta_r = load_beta_r();
        float3 beta_m = load_beta_m();

        // optical length
        float zenith = acos(max(0.0, dir.y));
        float inverse = 1.0 / (cos(zenith) + 0.15 * pow(93.885 - ((zenith * 180.0) / PI), -1.253));
        float s_r = 8.4e3 * inverse;
        float s_m = 1.25e3 * inverse;

        float3 fex = exp(-beta_r * s_r + beta_m + s_m);
        float cos_theta = dot(sun_dir, dir);

        float r_phase = phase_function_r(cos_theta * 0.5 + 0.5);
        float m_phase = phase_function_m(cos_theta, load_mie_g());
        float3 beta_r_theta = beta_r * r_phase;
        float3 beta_m_theta = beta_m * m_phase;

        float y_dot = 1.0 - sun_dir.y;
        y_dot *= y_dot * y_dot * y_dot * y_dot;
        float3 betas = (beta_r_theta + beta_m_theta) / (beta_r + beta_m);
        float3 inscattered = pow(intensity * betas * (1.0 - fex), float3(1.5, 1.5, 1.5));
        inscattered *= lerp(float3(1, 1, 1), pow(intensity * betas * fex, float3(0.5, 0.5, 0.5)), clamp(y_dot, 0, 1));

        float3 l0 = 0.1 * fex;

        // float sun_disk = smoothstep(SUN_ANGULAR_COS, SUN_ANGULAR_COS + 0.00002, cos_theta);
        float sun_disk = 0; // TODO
        l0 += (intensity * 15000.0 * fex) * sun_disk;

        color = (inscattered + l0) * 0.04 + float3(0, 0.0003, 0.00075);

        return color;
    }
]]

system : {    
}