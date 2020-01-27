depth_stencil_states : {
    depth_test_enable : true
    depth_write_enable : true
    depth_compare_op : "less_equal"
}

raster_states : {
    polygon_mode: "fill"
}

blend_states : {
    logical_operation_enable: false
    render_target_0 : {
        blend_enable: true
    }
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
    { name: "coverage" type: "float" }
    { name: "sampler_wrap_linear" type: "sampler" sampler: "wrap_linear" }    
]

common : [[    
    float3 ray_sphere_intersect(float offset, in float3 dir) {
        float radius = 10000 + offset;
        float3 camera_pos = load_camera_position();
        float3 pos = camera_pos - float3(0, -10000, 0);
        float a = dot(dir, dir) * 2;
        float b = dot(dir, pos) * 2;
        float c = dot(pos, pos);
        float disc = b * b - 2 * a * (c - radius * radius);
        float t = max(0, (-b + sqrt(disc)) / a);
        return pos + dir * t;
    }

    float get_density(float3 pos) {
        return 0;
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

        Texture3D shape = get_cloud_shape();
        SamplerState sampler = get_sampler_wrap_linear();

        float3 col = shape.Sample(sampler, v).rgb;
        col *= load_coverage();
        output.color = float4(col.x, col.y, col.z, 0.5);
        return output;
    ]]
}

compile : {
    includes : [ "common", "sampling" ]

    variations : [        
        { systems : [ "frame_system", "viewer_system" ] },
    ]
}