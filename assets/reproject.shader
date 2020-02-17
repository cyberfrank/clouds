depth_stencil_states : {
    depth_test_enable : false
    depth_write_enable : false
}

raster_states : {
    polygon_mode: "fill"
}

samplers : {
    clamp_linear : {
        anisotropy_enable: true
        min_filter: "linear"
        max_filter: "linear"
        mip_mode: "linear"
        address_u: "clamp"
        address_v: "clamp"
        address_w: "clamp"
    }
}

imports : [
    { name: "textures" type: "texture_2d" elements: 2 }
    { name: "sampler_clamp_linear" type: "sampler" sampler: "clamp_linear" }   
]

common : [[
    float3 compute_clip_space(in float2 position) {
        float2 nds = 2.0 * position / load_render_target_resolution() - 1.0;
        return float3(nds, 1.0);
    }

    float2 compute_screen_pos(in float2 ndc) {
        return ndc * 0.5 + 0.5;
    }
]]

vertex_shader : {
    import_system_semantics : [ "vertex_id" ]

    exports : [        
        { name: "uv" type: "float2"}
    ] 

    code : [[
        static const float4 pos[3] = {
            { -1,  1,  1, 1 },
            {  3,  1,  1, 1 },
            { -1, -3,  1, 1 },
        };
    
        output.position = pos[input.vertex_id];

        static const float2 uvs[3] = {
            { 0, 0 },
            { 2, 0 },
            { 0, 2 },
        };
        output.uv = uvs[input.vertex_id];

        return output;
    ]]
}

pixel_shader : {
    exports : [
        { name : "color" type: "float4" }
    ]

    code : [[       
        float3 clip_space_pos = compute_clip_space(input.position.xy);
        float4 camera_to_world = mul(float4(clip_space_pos, 1), load_camera_inverse_view_projection());
        camera_to_world /= camera_to_world.w;

        // last frame sample position (p')
        float4 p = mul(camera_to_world, load_camera_last_view_projection());
        p /= p.w;

        float2 screen_pos = compute_screen_pos(p.xy);
        bool outside = any(abs(screen_pos - 0.5) > 0.5);
        
        SamplerState sampler = get_sampler_clamp_linear();
        float4 current = get_textures(0).Sample(sampler, input.uv);
        float4 history = get_textures(1).Sample(sampler, screen_pos);

        output.color = outside ? current : lerp(current, history, 0.05);
        return output;
    ]]
}

compile : {
    includes : [ "common" ]

    variations : [
        { systems: [ "viewer_system" ] },
    ]
}