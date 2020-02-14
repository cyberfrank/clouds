depth_stencil_states : {
    depth_test_enable : true
    depth_write_enable : false
    depth_compare_op : "less_equal"
}

raster_states : {
    polygon_mode: "fill"
}

samplers : {
    wrap_linear : {
        anisotropy_enable: true
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
    { name: "coverage" type: "float" }
    { name: "precipitation" type: "float" }
    { name: "cloud_type" type: "float" }
    { name: "weather_map" type: "texture_2d" }
    { name: "wind_vector" type: "float2" }
    { name: "wind_speed" type: "float" }
    { name: "planet_radius" type: "uint" }
    { name: "atmosphere_start" type: "uint" }
    { name: "atmosphere_thickness" type: "uint" }
    { name: "cloud_shape" type: "texture_3d" }
    { name: "cloud_detail" type: "texture_3d" }
    { name: "sampler_wrap_linear" type: "sampler" sampler: "wrap_linear" }   
]

common : [[ 
    float remap(in float value, in float old_min, in float old_max, in float new_min, in float new_max) 
    {
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

    float height_signal(in float altitude, in float height, in float altitude_start)
    {
        float one_over_height = 1 / height;
        float altitude_diff = altitude - altitude_start;
        return altitude_diff * (altitude_diff - height) * one_over_height * one_over_height * -4;
    }

    float density_gradient(in float rh, in float cloud_type)
    {
        const float4 g0 = float4(0.02, 0.05, 0.09, 0.11);  // stratus
        const float4 g1 = float4(0.02, 0.2, 0.48, 0.625);  // stratocumulus
        const float4 g2 = float4(0.01, 0.0625, 0.78, 1.0); // cumulus
        
        float a = 1.0 - saturate(cloud_type * 2.0);
        float b = 1.0 - abs(cloud_type - 0.5) * 2.0;
        float c = saturate(cloud_type - 0.5) * 2.0;

        float4 gradient = g0 * a + g1 * b + g2 * c;

        return smoothstep(gradient.x, gradient.y, rh) - smoothstep(gradient.z, gradient.w, rh);
    }

    float capture_sample(float3 pos, in float rh) 
    {
        Texture3D shape = get_cloud_shape();
        Texture3D detail = get_cloud_detail();
        
        SamplerState sampler = get_sampler_wrap_linear();

        const float BASE_SCALE = 0.00005;
        const float DETAIL_SCALE = 0.0015;
        const float WEATHER_SCALE = 0.00001;

        float2 wind_vec = load_wind_vector();
        float3 wind = float3(wind_vec.x, 0.1, wind_vec.y) * load_time() * load_wind_speed();

        float3 weather = get_weather_map().Sample(sampler, pos.xz * WEATHER_SCALE).rgb;

        float base_cloud = shape.Sample(sampler, pos * BASE_SCALE + wind).r;
        base_cloud *= density_gradient(rh, load_cloud_type());
        
        float density = remap(base_cloud, weather.r * (1.0 - load_coverage()), 1.0, 0.0, 1.0);

        if (density > 0)
        {
            float erosion = detail.Sample(sampler, pos * DETAIL_SCALE + wind * 16.0).r;
            erosion = lerp(erosion, 1.0 - erosion, saturate(rh * 10.0));
            density = remap(density, erosion * 0.4, 1.0, 0.0, 1.0);
        }
        
        // reduce density at the bottom
        return saturate(density * smoothstep(0.0, 1.0, rh));
    }

    float volumetric_shadow(in float3 sun_dir, in float3 origin) 
    {
        const uint steps = 4;
        float step_size = load_atmosphere_thickness() / float(steps);

        float3 increment = sun_dir * step_size;
        float3 position = origin + increment * 0.5;
        
        float shadow = 0.0;

        for (uint step = 0; step < steps; ++step, position += increment) {
            float rh = height_in_shell(position);
            float density = capture_sample(position, rh);
            shadow += density;
        }
        return exp2(-shadow * step_size);
    }

    float3 compute_clip_space(float2 position) {
        float2 nds = 2.0 * position / load_render_target_resolution() - 1.0;
        return float3(nds, 1.0); 
    }

    float2 compute_screen_pos(float2 ndc) {
        return ndc * 0.5 + 0.5;
    }
]]

vertex_shader : {
    import_system_semantics : [ "vertex_id" ]

    exports : [        
        { name: "camera_ray" type: "float3" }
    ] 

    code : [[
        static const float4 pos[3] = {
            { -1,  1,  1, 1 },
            {  3,  1,  1, 1 },
            { -1, -3,  1, 1 },
        };

        bool taa_enabled = load_settings_mask() & SETTINGS_TAA_ENABLED;
        output.position = taa_enabled ? add_taa_offset(pos[input.vertex_id], load_frame_number(), load_render_target_resolution()) : pos[input.vertex_id];

        float4x4 inverse_view = load_camera_inverse_view();
        
        float4 cp = float4(pos[input.vertex_id].xy, 1, 1);
        cp = taa_enabled ? remove_taa_offset(cp, load_frame_number(), load_render_target_resolution()) : cp;
        float4 p = mul(cp, load_camera_inverse_projection());
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
            output.color = float4(0.02, 0.02, 0.02, 1);
            return output;
        }

        float R = load_planet_radius();

        float3 offset = load_camera_position() + float3(0, R, 0);
        float3 inner = ray_sphere_intersect(R + load_atmosphere_start(), offset, v);
        float3 outer = ray_sphere_intersect(R + load_atmosphere_start() + load_atmosphere_thickness(), offset, v);

        uint steps = load_num_steps();
        float step_size = distance(inner, outer) / steps;
        float t = step_size * 1.0;

        float transmittance = 1.0;
        float3 scattered_light = float3(0, 0, 0);
        
        float3 sun_dir = load_sun_direction();
        float cos_theta = dot(v, sun_dir);
        // dual lobe phase function
        float hg_dual_phase = lerp(hg_phase(cos_theta, 0.8), hg_phase(cos_theta, -0.2), 0.5);


        float3 sun_color = get_sky_color_physical(sun_dir);
        float3 sky_light = get_sky_color_physical(float3(0, -1, 0)) * load_precipitation();

        float dist = R;

        for (uint step = 0; step < steps; ++step) {
            float3 pos = inner + v * t;
            // get relative height in atmosphere
            float rh = height_in_shell(pos);

            float density = capture_sample(pos, rh);

            // improved scattering equation proposed by Seb Hillaire
            if (density > 0)
            {
                dist = min(dist, t);

                float sigma_sca = 0.125 * density;              // scattering coefficient
                float sigma_ext = (sigma_sca + 0.05) * density; // scattering extinction

                float3 ambient = sky_light * smoothstep(0.0, 1.0, rh);

                float3 S = (volumetric_shadow(sun_dir, pos) * sun_color * hg_dual_phase + ambient) * sigma_sca;
                float Tr = exp(-sigma_ext * step_size);
                float3 Sint = (S - S * Tr) / sigma_ext;

                scattered_light += transmittance * Sint;
                transmittance *= Tr;
            }
            
            // opt out early for low transmittance
            if (transmittance < 0.1) break;

            t += step_size;
        }

        float fog_amount = 1.0 - exp(-dist * 0.0001);
        float3 background_color = get_sky_color_physical(v);
        scattered_light = lerp(scattered_light, background_color * (1.0 - transmittance), fog_amount);

        float3 color = scattered_light + background_color * transmittance;

        output.color = float4(color, 1);
        return output;
    ]]
}
compile : {
    includes : [ "common", "sampling" ]

    variations : [
        { systems : [ "frame_system", "viewer_system", "sky_system" ] }
    ]
}