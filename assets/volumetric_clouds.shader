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
    { name: "sampler_wrap_linear" type: "sampler" sampler: "wrap_linear" }    
]

common : [[  
    #define PLANET_RADIUS 10000
    #define ATMOSPHERE_START 1000
    #define ATMOSPHERE_THICKNESS 1500

    float remap(in float value, in float old_min, in float old_max, in float new_min, in float new_max) {
        return new_min + (((value - old_min) / (old_max - old_min)) * (new_max - new_min));
    }

    // project to atmosphere shell
    float3 ray_sphere_intersect(float offset, in float3 dir) {
        float radius = PLANET_RADIUS + offset;
        float3 camera_pos = load_camera_position();
        float3 pos = camera_pos - float3(0, -PLANET_RADIUS, 0);
        float a = dot(dir, dir) * 2;
        float b = dot(dir, pos) * 2;
        float c = dot(pos, pos);
        float disc = b * b - 2 * a * (c - radius * radius);
        float t = max(0, (-b + sqrt(disc)) / a);
        return pos + dir * t;
    }

    // get the relative altitude in the atmosphere between 0 and 1
    float altitude_in_atmosphere(in float3 pos)
    {
        return 0;
    }

    float height_signal(in float height, in float altitude_start, in float3 pos)
    {
        // parabola function
        // global maximum is always = 1

        // (x-a)*(x-a-h)*(-4/h^2)
        // x = altitude in atmosphere
    
        // a = cloud starting altitude
        // the clouds starts to appear at this altitude
        // in range 0-1 (0 = atmosphere start, 1 = atmosphere end)

        // h = cloud thickness (height)
        // the cloud will appear for this much space

        float height_scaled = height * ATMOSPHERE_THICKNESS;
        float altitude_start_scaled = altitude_start * ATMOSPHERE_THICKNESS + ATMOSPHERE_START;
        
        // the current altitude (x) is the distance from the center
        float current_altitude = distance(pos, float3(0, 0, 0)); 
        float one_over_height = 1.0 / height_scaled;
        float altitude_diff = current_altitude - altitude_start_scaled;

        // result: density at point
        return altitude_diff * (altitude_diff - height_scaled) * one_over_height * one_over_height * -4;
    }

    float get_density(float3 pos) {
        Texture3D shape = get_cloud_shape();
        Texture3D detail = get_cloud_detail();
        SamplerState sampler = get_sampler_wrap_linear();

        float density = 0.1;
        density *= height_signal(0.1, 0, pos);
        density *= shape.Sample(sampler, pos * 0.0001).r;

        return density;
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
    
        bool taa_enabled = false; //load_settings_mask() & SETTINGS_TAA_ENABLED;
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

        if (dot(v, float3(0, 1, 0)) < 0.0)
        {
            output.color = float4(get_sky_color_physical(v), 1);
            return output;
        }

        float3 entry = ray_sphere_intersect(ATMOSPHERE_START, v);
        float3 exit = ray_sphere_intersect(ATMOSPHERE_START + ATMOSPHERE_THICKNESS * 10, v);

        uint steps = 3;
        float step_size = distance(entry, exit) / steps;
        float t = step_size * 0.5;
        float density = 0;

        float cos_theta = dot(v, load_sun_direction());
        // dual lobe phase function
        float hg_dual = lerp(hg_phase(cos_theta, 0.8), hg_phase(cos_theta, 0.5), 0.5);
        for (uint step = 0; step < steps; ++step)
        {
            float3 pos = entry + v * t;
            density += get_density(pos);

            if (density > 0)
            {
                // light is evaluated for every sample with density > 0
                // 
                
            }

            t += step_size;
        }

        float transmittance = exp(-density * 0.0001);

        float3 col = get_sky_color_physical(v) * transmittance;
        output.color = float4(col.x, col.y, col.z, 1);
        
        return output;
    ]]
}

creation_graph_node : {
    name: "volumetric_clouds"
    display_name: "Volumetric Clouds"
    category: "Shader/Output"

    inputs : [
        { name: "cloud_shape" display_name: "Cloud Shape" target: { name: "cloud_shape" } optional: false }
        { name: "cloud_detail" display_name: "Cloud Detail" target: { name: "cloud_detail" } optional: false }
    ]
}

compile : {
    includes : [ "common", "sampling" ]

    variations : [        
        //{ systems : [ "frame_system", "viewer_system" ] },
        { systems : [ "frame_system", "viewer_system", "sky_system" ] },
    ]
}