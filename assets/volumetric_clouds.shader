raster_states : {
    polygon_mode: "fill"
}

samplers : {
    wrap_linear : {
        anisotropy_enable: false
        min_filter: "linear"
        max_filter: "linear"
        mip_mode: "linear"
        address_u: "wrap"
        address_v: "wrap"
        address_w: "wrap"
    }
}

imports : [
    { name: "num_steps" type: "uint" }
    { name: "shadow_quality" type: "uint" }
    { name: "coverage" type: "float" }
    { name: "blend_factor" type: "float" }
    { name: "erosion" type: "float" }
    { name: "cloud_type" type: "float" }
    { name: "weather_map" type: "texture_2d" }
    { name: "wind_vector" type: "float2" }
    { name: "wind_speed" type: "float" }
    { name: "planet_radius" type: "uint" }
    { name: "atmosphere_start" type: "uint" }
    { name: "atmosphere_thickness" type: "uint" }
    { name: "scattering" type: "float" }
    { name: "absorption" type: "float" }
    { name: "draw_distance" type: "float"}
    { name: "cloud_shape" type: "texture_3d" }
    { name: "cloud_detail" type: "texture_3d" }
    { name: "sampler_wrap_linear" type: "sampler" sampler: "wrap_linear" } 
    { name: "history" type: "texture_2d" }
]

common : [[ 
    float remap(in float value, in float old_min, in float old_max, in float new_min, in float new_max) {
        return new_min + (((value - old_min) / (old_max - old_min)) * (new_max - new_min));
    }

    float3 ray_sphere_intersect(in float radius, in float3 o, in float3 d) {
        float a = radius * radius - dot(o, o);
        float b = dot(o, d);
        float t = sqrt(b * b + a) - b;
        return o + d * t;
    }

    float height_in_shell(in float3 pos) {
        float y = length(pos) - load_planet_radius() - load_atmosphere_start();
        return saturate(y / load_atmosphere_thickness());
    }

    float height_signal(in float rh, in float cloud_type) {
        const float4 g0 = float4(0.0, 0.1, 0.2, 0.3);  // stratus
        const float4 g1 = float4(0.02, 0.2, 0.48, 0.625);  // stratocumulus
        const float4 g2 = float4(0.0, 0.1625, 0.88, 0.98); // cumulus
        
        float a = 1.0 - saturate(cloud_type * 2.0);
        float b = 1.0 - abs(cloud_type - 0.5) * 2.0;
        float c = saturate(cloud_type - 0.5) * 2.0;

        float4 gradient = g0 * a + g1 * b + g2 * c;

        return smoothstep(gradient.x, gradient.y, rh) - smoothstep(gradient.z, gradient.w, rh);
    }

    float capture_sample(float3 pos, in float rh) {
        Texture3D shape = get_cloud_shape();
        Texture3D detail = get_cloud_detail();
        
        SamplerState sampler = get_sampler_wrap_linear();

        const float BASE_SCALE = 0.00015;
        const float DETAIL_SCALE = 0.0012;
        const float WEATHER_SCALE = 0.00002;

        float2 wind_vec = load_wind_vector();
        float3 wind = float3(wind_vec.x, 0.2, wind_vec.y) * load_time() * load_wind_speed();

        float3 weather = get_weather_map().Sample(sampler, pos.xz * WEATHER_SCALE).rgb;

        float base_cloud = shape.Sample(sampler, pos * BASE_SCALE + wind).r;
        base_cloud *= height_signal(rh, weather.b);

        float density = remap(base_cloud, (1.0 - weather.r) * (1.0 - load_coverage()), 1.0, 0.0, 1.0);

        if (density > 0)
        {
            float erosion = detail.Sample(sampler, pos * DETAIL_SCALE + wind).r;
            erosion = lerp(erosion, 1.0 - erosion, saturate(rh * 10.0));
            density = remap(density, erosion * load_erosion(), 1.0, 0.0, 1.0);
        }
        
        // reduce density at the bottom
        return saturate(density * smoothstep(0.0, 2.0, rh));
    }

    float volumetric_shadow(in float3 sun_dir, in float3 origin) {
        const uint steps = load_shadow_quality() * 2 + 1;
        float step_size = load_atmosphere_thickness() / float(steps);

        float t = step_size * 0.5;
        float shadow = 0.0;
        float3 position;

        for (uint step = 0; step < steps; ++step, t += step_size) {
            position = origin + sun_dir * t;
            float rh = height_in_shell(position);
            float density = capture_sample(position, rh);
            shadow += density;
        }
        // extinction coefficient should be added to ensure energy conservation
        // float ext = load_scattering() * shadow + load_absorption();
        float sigma_s = load_scattering() * shadow;
        float sigma_t = (sigma_s + load_absorption()) * shadow;
        return exp2(-sigma_t * step_size);
    }

    float2 reproject(float3 wpos) {
        float4 last_cp = mul(float4(wpos, 1), load_camera_last_view_projection());
        last_cp /= last_cp.w;

        return float2(last_cp.x, -last_cp.y) * 0.5 + 0.5;
    }

    float radical_inverse_vdc(uint i) {
        return reversebits(i) * 2.3283064365386963e-10;
    }
]]

vertex_shader : {
    import_system_semantics : [ "vertex_id" ]

    exports : [        
        { name: "camera_ray" type: "float3" }
        { name: "uv" type: "float2" }
        { name: "frag_pos" type: "float4" }
    ] 

    code : [[
        static const float4 pos[3] = {
            { -1,  1,  1, 1 },
            {  3,  1,  1, 1 },
            { -1, -3,  1, 1 },
        };

        static const float2 uvs[3] = {
            { 0, 0 },
            { 2, 0 },
            { 0, 2 },
        };

        output.uv = uvs[input.vertex_id];
        output.position = pos[input.vertex_id];

        float4x4 inverse_view = load_camera_inverse_view();
        float4 cp = float4(pos[input.vertex_id].xy, 1, 1);
        float4 p = mul(cp, load_camera_inverse_projection());
        output.frag_pos = p;
        output.camera_ray = mul(p.xyz, (float3x3)inverse_view);
        return output;
    ]]
}

pixel_shader : {
    exports : [
        { name : "color" type: "float4" }
    ]

    code : [[
        float3 v = normalize(input.camera_ray);

        if (v.y < 0)
        {
            output.color = float4(0, 0, 0, 1);
            return output;
        }

        float2 screen_pos = reproject(load_camera_position() + v * load_draw_distance());
        bool outside = any(abs(screen_pos - 0.5) > 0.499);

        float radius = load_planet_radius();
        float3 offset = load_camera_position() + float3(0, radius, 0);
        float3 inner = ray_sphere_intersect(radius + load_atmosphere_start(), offset, v);
        float3 outer = ray_sphere_intersect(radius + load_atmosphere_start() + load_atmosphere_thickness(), offset, v);

        uint steps = load_num_steps();
        // to avoid artifacts at the edges, more steps are required where the 
        // previous frame information was not `available`
        if (outside) {
            steps *= 4;
        }

        float draw_distance = min(distance(inner, outer), load_draw_distance());
        float step_size = draw_distance / steps;
        float t = step_size * radical_inverse_vdc(load_frame_number());
        
        float3 sun_dir = load_sun_direction();
        float3 sun_color = get_sky_color_physical(sun_dir);
        float3 sky_light = get_sky_color_physical(float3(0, -1, 0));
        float transmittance = 1.0;
        float3 scattered_light = float3(0, 0, 0);
        float cos_theta = dot(v, sun_dir);

        float hg_dual_phase = lerp(hg_phase(cos_theta, 0.8), hg_phase(cos_theta, -0.2), 0.5);

        float dist = radius;

        for (uint step = 0; step < steps; ++step, t += step_size) {
            float3 pos = inner + v * t;
            // get relative height in atmosphere
            float rh = height_in_shell(pos);

            float density = capture_sample(pos, rh);

            // improved scattering equation proposed by Seb Hillaire
            if (density > 0)
            {
                dist = min(dist, t);

                float sigma_s = load_scattering() * density;
                float sigma_t = (sigma_s + load_absorption()) * density;

                float3 ambient = sky_light * smoothstep(0.0, 1.0, rh);

                float3 S = (volumetric_shadow(sun_dir, pos) * sun_color * hg_dual_phase + ambient) * sigma_s;
                float Tr = exp(-sigma_t * step_size);
                float3 Sint = (S - S * Tr) / sigma_t;

                scattered_light += transmittance * Sint;
                transmittance *= Tr;
            }

            // opt out early for low transmittance
            if (transmittance < 0.1) break;
            
        }
        
        float3 background_color = get_sky_color_physical(v);
        float fog_amount = 1.0 - exp(-dist * (1.0 / load_draw_distance()));
        scattered_light = lerp(scattered_light, background_color * (1.0 - transmittance), saturate(fog_amount));

        // @hack background should in reality be added before the cloud pass
        float4 curr_frame = float4(scattered_light + background_color * transmittance, transmittance);
        float4 last_frame = get_history().Sample(get_sampler_wrap_linear(), screen_pos);

        output.color = outside ? curr_frame : lerp(last_frame, curr_frame, load_blend_factor());
        return output;
    ]]
}
compile : {
    includes : [ "common", "sampling" ]

    variations : [
        { systems : [ "frame_system", "viewer_system", "sky_system" ] }
    ]
}