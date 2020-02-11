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
    { name: "weather_texture" type: "texture_2d" }
    { name: "num_steps" type: "uint" }
    { name: "coverage" type: "float" }
    { name: "precipitation" type: "float" }
    { name: "sampler_wrap_linear" type: "sampler" sampler: "wrap_linear" }     
    // main hdr accumulation output 
    { name: "image" type: "texture_2d" }
]

common : [[  
    #define PLANET_RADIUS 6371000
    #define ATMOSPHERE_START 1500
    #define ATMOSPHERE_THICKNESS 2000

    float remap(in float value, in float old_min, in float old_max, in float new_min, in float new_max) 
    {
        return new_min + (((value - old_min) / (old_max - old_min)) * (new_max - new_min));
    }

    float linear_step(in float value, in float min, in float max)
    {
        return saturate((value - min) / (max - min));
    }

    float erode(in float x, in float e)
    {
        return max(1 - (1 - x) / e, 0);
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

    float capture_sample(float3 pos, in float rh) 
    {
        Texture3D shape = get_cloud_shape();
        Texture3D detail = get_cloud_detail();
        
        SamplerState sampler = get_sampler_wrap_linear();

        const float BASE_SCALE = 0.000151;
        const float DETAIL_SCALE = 0.0012;
        const float WEATHER_SCALE = 0.00004;

        float coverage = load_coverage();
        float fade_amount = 5;
        float3 wind = float3(0.05, 0.0, 0.05) * load_time() * 0.5;

        // float3 weather = get_weather_texture().Sample(sampler, pos.xz * WEATHER_SCALE).rgb;

        float base_cloud = shape.Sample(sampler, pos * BASE_SCALE + wind).r;

        float density = remap(base_cloud * height_signal(rh, 1.0, 0.0), 1.0 - coverage, 1.0, 0.0, 1.0);
        
        // float erosion = detail.Sample(sampler, pos * DETAIL_SCALE).r;
        // density = remap(density, erosion * 0.5, 1.0, 0.0, 1.0);

        return saturate(density * (rh / fade_amount));
    }

    float volumetric_shadow(in float3 sun_dir, in float3 origin) 
    {
        uint steps = 4;
        float step_size = 2;
        float t = step_size * 0.5;
        float shadow = 1.0;
        for (uint step = 0; step < steps; ++step) {
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
    
        output.position = pos[input.vertex_id];

        float4x4 inverse_view = load_camera_inverse_view();
        float4 p = mul(float4(pos[input.vertex_id].xy, 1, 1), load_camera_inverse_projection());
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

        float3 offset = load_camera_position() + float3(0, PLANET_RADIUS, 0);
        float3 inner = ray_sphere_intersect(PLANET_RADIUS + ATMOSPHERE_START, offset, v);
        float3 outer = ray_sphere_intersect(PLANET_RADIUS + ATMOSPHERE_START + ATMOSPHERE_THICKNESS, offset, v);

        uint steps = load_num_steps();
        float step_size = distance(inner, outer) / steps;
        float t = step_size * halton_sequence_2d(load_frame_number()).x;

        float transmittance = 1.0;
        float3 scattered_light = float3(0, 0, 0);
        
        float3 sun_dir = load_sun_direction();
        float cos_theta = dot(v, sun_dir);
        // dual lobe phase function
        float hg_dual_phase = lerp(hg_phase(cos_theta, 0.8), hg_phase(cos_theta, -0.2), 0.5);

        float3 reflected;
        float3 sun_color = get_sky_color_physical(sun_dir, reflected);
        float3 sky_light = get_sky_color_physical(float3(0, -1, 0), reflected);

        for (uint step = 0; step < steps; ++step) {
            float3 pos = inner + v * t;
            // get relative height in atmosphere
            float rh = height_in_shell(pos);

            float density = capture_sample(pos, rh);

            // improved scattering equation proposed by Seb Hillaire
            if (density > 0)
            {
                float3 ambient = sky_light * smoothstep(0.0, 1.0, rh) * (1.0 - load_precipitation());

                float sigma_sca = 0.125 * density;              // scattering coefficient
                float sigma_ext = (sigma_sca + 0.05) * density; // scattering extinction

                float3 S = (volumetric_shadow(sun_dir, pos) * sun_color * hg_dual_phase + ambient) * sigma_sca;
                float Tr = exp(-sigma_ext * step_size);
                float3 Sint = (S - S * Tr) / sigma_ext;

                scattered_light += transmittance * Sint;
                transmittance *= Tr;
            }

            // opt out early for low transmittance
            if (transmittance < 0.01) break;

            t += step_size;
        }

        /*
        Offset starting step in range 0-1
        Calculate world position of P in clip space position using current inv view projection

        Pwp = Pcs * (VP)^-1

        Calculate P'cs, the position P from last frame. Multiply Pwp and the last view projection
        P'cs = Pwp * (VP)last

        Sample previous frame at position P'
        Cp' = texture(P')

        Blend between current and previous frame with blend faction 'a'
        Cp * a + Cp' * (1 - a)

        */

        float4 clip_space_position = input.position;
        float4 position = mul(clip_space_position, load_camera_inverse_view_projection());

        float3 background_color = get_sky_color_physical(v, reflected);
        float3 color = scattered_light + (background_color + reflected) * transmittance;

        output.color = float4(color, 1);
        return output;
    ]]
}

// creation_graph_node : {
//     name: "volumetric_clouds"
//     display_name: "Volumetric Clouds"
//     category: "Shader/Output"

//     inputs : [
//         { name: "cloud_shape" display_name: "Cloud Shape" target: { name: "cloud_shape" } optional: false }
//         { name: "displacement" display_name: "Displacement" target: { name: "displacement" } optional: false }
//         { name: "cloud_detail" display_name: "Cloud Detail" target: { name: "cloud_detail" } optional: false }
//         { name: "weather_texture" display_name: "Weather Texture" target: { name: "weather_texture" } optional: true }
//         { name: "num_steps" display_name: "Num Steps" target: { name: "num_steps" } optional: true }
//     ]
// }

compile : {
    includes : [ "common", "sampling" ]

    variations : [
        { systems : [ "frame_system", "viewer_system", "sky_system" ] },
    ]
}