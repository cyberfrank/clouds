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
    { name: "cloud_shape" type: "texture_3d" }
    { name: "cloud_detail" type: "texture_3d" }
    { name: "displacement" type: "texture_2d" }
    { name: "weather_texture" type: "texture_2d" }
    { name: "num_steps" type: "uint" }
    { name: "sampler_wrap_linear" type: "sampler" sampler: "wrap_linear" }      
]

common : [[  
    #define PLANET_RADIUS 6371000
    #define ATMOSPHERE_START 1500
    #define ATMOSPHERE_THICKNESS 5000

    float remap(in float value, in float old_min, in float old_max, in float new_min, in float new_max) 
    {
        return new_min + (((value - old_min) / (old_max - old_min)) * (new_max - new_min));
    }

    float linear_step(in float min, in float max, in float value)
    {
        return saturate((value - min) / (max - min));
    }

    float3 ray_sphere_intersect(in float radius, in float3 o, in float3 d) {
        float a = radius * radius - dot(o, o);
        float b = dot(o, d);
        float t = sqrt(b * b + a) - b;
        return o + d * t;
    }

    float height_in_shell(in float3 pos) {
        float y = length(pos) - PLANET_RADIUS - ATMOSPHERE_START;
        return saturate(y / ATMOSPHERE_THICKNESS);
    }

    float height_signal(in float altitude, in float height, in float altitude_start)
    {
        // (x-a)*(x-a-h)*(-4/h^2)
        float one_over_height = 1 / height;
        float altitude_diff = altitude - altitude_start;
        return altitude_diff * (altitude_diff - height) * one_over_height * one_over_height * -4;
    }

    float capture_sample(in float3 pos, in float rh) 
    {
        Texture3D shape = get_cloud_shape();
        
        SamplerState sampler = get_sampler_wrap_linear();

        const float NOISE_SCALE = 0.000005 * 10;

        float coverage = 0.66;
        float edge_smoothness = 1.0;
        float3 wind = float3(0.05, 0.0, 0.05) * load_time();

        float3 weather = saturate(get_weather_texture().Sample(sampler, pos.xz * 0.000009).rgb);
        
        float density = smoothstep(0.0, edge_smoothness, weather.r + (coverage - 1));
        density *= height_signal(rh, 0.4, 0.5);
        
        float erosion = 1.0 - shape.Sample(sampler, pos * NOISE_SCALE + wind).r;
        density = linear_step(erosion, 1, density);

        return saturate(density);
    }

    float volumetric_shadow(in float3 sun_dir, in float3 origin) 
    {
        float step_size = 2;
        float t = step_size * 0.5;
        float shadow = 1.0;

        // four steps at expontial growth
        for (uint step = 0; step < 5; ++step) {
            float3 p = origin + sun_dir * t;
            float rh = height_in_shell(p);

            // outside atmosphere
            if (rh > 1) break;
            
            float density = capture_sample(p, rh);
            shadow *= exp(-density * step_size);

            step_size *= step_size;
            t += step_size;
        }
        
        return shadow;
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
        float3 background_color = get_sky_color_physical(v);

        if (v.y < 0)
        {
            output.color = float4(0.25, 0.23, 0.21, 1);
            return output;
        }

        float3 offset = load_camera_position() + float3(0, PLANET_RADIUS, 0);
        float3 inner = ray_sphere_intersect(PLANET_RADIUS + ATMOSPHERE_START, offset, v);
        float3 outer = ray_sphere_intersect(PLANET_RADIUS + ATMOSPHERE_START + ATMOSPHERE_THICKNESS * 2, offset, v);

        uint steps = 512;
        float step_size = distance(inner, outer) / steps;
        float t = step_size * 0.5;
        float3 sun_dir = load_sun_direction();
        float cos_theta = dot(v, sun_dir);

        float transmittance = 1.0;
        float3 scattered_light = float3(0, 0, 0);
        
        // dual lobe phase function
        float hg_dual_phase = lerp(hg_phase(cos_theta, 0.8), hg_phase(cos_theta, -0.5), 0.5);

        for (uint step = 0; step < steps; ++step) {
            float3 pos = inner + v * t;
            // get relative height in atmosphere
            float rh = height_in_shell(pos);

            float density = capture_sample(pos, rh);

            // evaluate light for density > 0
            // improved scattering equation proposed by S. Hillaire
            if (density > 0)
            {
                // note: ambient color can also be multiplied by precipitation here
                // sample global ambient probe
                float3 ambient = lerp(float3(0.25, 0.23, 0.21), float3(1, 1, 1) * 0.8, rh);
                ambient *= 0.8;

                float sigma_sca = 0.125 * density; // scattering coefficient
                float sigma_ext = (0.125 + 0.0622) * density; // scattering extinction

                // 150000000
                float3 sun = float3(1.0, 0.9, 0.7) * load_sun_intensity();

                float3 S = (volumetric_shadow(sun_dir, pos) * sun * float3(1.0, 0.9, 0.7) * hg_dual_phase + ambient) * sigma_sca;
                float Tr = exp(-sigma_ext * step_size);
                float3 Sint = (S - S * Tr) / sigma_ext;

                scattered_light += transmittance * Sint;
                transmittance *= Tr;
            }

            // opt out early for low transmittance
            if (transmittance < 0.01) break;

            t += step_size;
        }

        float3 color = scattered_light + background_color * transmittance;

        output.color = float4(color, 1);
        return output;
    ]]
}

creation_graph_node : {
    name: "volumetric_clouds"
    display_name: "Volumetric Clouds"
    category: "Shader/Output"

    inputs : [
        { name: "cloud_shape" display_name: "Cloud Shape" target: { name: "cloud_shape" } optional: false }
        { name: "displacement" display_name: "Displacement" target: { name: "displacement" } optional: false }
        { name: "cloud_detail" display_name: "Cloud Detail" target: { name: "cloud_detail" } optional: false }
        { name: "weather_texture" display_name: "Weather Texture" target: { name: "weather_texture" } optional: true }
        { name: "num_steps" display_name: "Num Steps" target: { name: "num_steps" } optional: true }
    ]
}

compile : {
    includes : [ "common", "sampling" ]

    variations : [        
        { systems : [ "frame_system", "viewer_system", "sky_system" ] },
    ]
}