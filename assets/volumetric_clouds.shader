depth_stencil_states : {
    depth_test_enable : true
    depth_write_enable : true
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
    //{ name: "textures" type: "texture_3d" elements: 2 }
    { name: "coverage" type: "float" }
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
        //{ name: "camera_ray" type: "float3"}
    ] 

    code : [[
        static const float4 pos[3] = {
            { -1,  1,  1, 1 },
            {  3,  1,  1, 1 },
            { -1, -3,  1, 1 },
        };
    
        bool taa_enabled = load_settings_mask() & SETTINGS_TAA_ENABLED;
        output.position = taa_enabled ? add_taa_offset(pos[input.vertex_id], load_frame_number(), load_render_target_resolution()) : pos[input.vertex_id];

        return output;
    ]]
}

pixel_shader : {
    exports : [
        { name : "color" type: "float4" }
    ]

    code : [[
        float t = 1;
        output.color = float4(t, t, t, 0);
        return output;
    ]]
}

compile : {
    includes : [ "common", "sampling" ]

    variations : [        
        { systems : [ "frame_system", "viewer_system" ] },
    ]
}