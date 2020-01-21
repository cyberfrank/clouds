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
    { name: "tm" type: "float4x4" }
    { name: "cloud_shape" type: "texture_3d" } 
    { name: "cloud_detail" type: "texture_3d" }
    { name: "planet_radius" type: "float" }
    { name: "sampler_wrap_linear" type: "sampler" sampler: "wrap_linear" }    
]

common : [[    
    float3 ray_sphere_intersect(float offset, in float3 dir) {
        float radius = load_planet_radius() + offset;
        float3 camera_pos = load_camera_position();
        float3 pos = camera_pos - float3(0, -load_planet_radius(), 0);
        float a = dot(dir, dir) * 2;
        float b = dot(dir, pos) * 2;
        float c = dot(pos, pos);
        float disc = b * b - 2 * a * (c - radius * radius);
        float t = max(0, (-b + sqrt(disc)) / a);
        return pos + dir * t;
    }

    float get_density(float3 pos) {
        SamplerState samp = get_sampler_wrap_linear();
        float density = get_cloud_shape().Sample(samp, 0.0002 * pos).r;

        return density;
    }
]]

vertex_shader : {
    import_system_semantics : [ "vertex_id" ]

    exports : [        
        { name: "camera_ray" type: "float3"}
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
        output.camera_ray = mul(mul(p.xyz, (float3x3)inverse_view), (float3x3)load_tm());
        
        return output;
    ]]
}

pixel_shader : {
    exports : [
        { name : "color" type: "float4" }
    ]

    code : [[
        const int steps = 16;

        float3 eye_dir = normalize(input.camera_ray);

        const float atmosphere_start = 1500;
        const float atmosphere_end = min(80000, atmosphere_start + 2000);

        float3 entry = ray_sphere_intersect(atmosphere_start, eye_dir);
        float3 exit = ray_sphere_intersect(atmosphere_end, eye_dir);

        float step_size = distance(entry, exit) / steps;

        float t = 0;
        float density;
        float transmittance;
        float3 pos;
        for (int step = 0; step < steps; ++step) {
            pos = entry + eye_dir * t;

            density = get_density(pos);

            t += step_size;
        }
        output.color = float4(density, density, density, 1);
        return output;
    ]]
}

creation_graph_node : {
    name: "volumetric-clouds"
    display_name: "Volumetric Clouds"
    category: "Shader"
    inputs : [
        { name: "cloud_shape" display_name: "Cloud Shape" target: { name: "cloud_shape" element: 0 } optional: false },
        { name: "cloud_detail" display_name: "Cloud Detail" target: { name: "cloud_detail" element: 0 } optional: true },
        { name: "planet_radius" display_name: "Planet Radius" target: { name: "planet_radius" element: 0} optional: true }
    ]
}

compile : {
    includes : [ "common", "sampling" ]

    variations : [        
        { systems : [ "frame_system", "viewer_system" ] },
    ]
}