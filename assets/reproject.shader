depth_stencil_states : {
    depth_test_enable : true
    depth_write_enable : false
    depth_compare_op : "less_equal"
}

samplers : {
    linear : {
        anisotropy_enable: false
        min_filter: "linear"
        max_filter: "linear"
        mip_mode: "linear"
    }
}

imports : [
    { name: "textures" type: "texture_2d" elements: 2 }
    { name: "sampler_linear" type: "sampler" }
]

common : [[
]]

vertex_shader : {
    import_system_semantics : [ "vertex_id" ]

    exports : [        
        { name: "uv" type: "float2" }
        { name: "camera_ray" type: "float3" }
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

        float4x4 inverse_view = load_camera_inverse_view();
        
        float4 cp = float4(pos[input.vertex_id].xy, 1, 1);
        float4 p = mul(cp, load_camera_inverse_projection());
        output.camera_ray = mul(p.xyz, (float3x3)inverse_view);
        return output;
    ]]
}

pixel_shader : {
    exports : [
        { name: "color" type: "float4" }
    ]

    code : [[
        float3 v = normalize(input.camera_ray);

        // hdr accumulation target
        float4 hdr = get_textures(1).Load(int3(input.position.xy, 0));

        if (v.y > 0)
            hdr.rgb = float3(0, 0, 0);

        float4 clouds = get_textures(0).Sample(get_sampler_linear(), input.uv);
        output.color = float4(clouds.rgb + hdr.rgb * clouds.a, 1.0);
        return output;
    ]]
}

compile : {
    includes : [ "common" ]

    variations : [
        { systems: [ "viewer_system" ] }
        { systems: [ "viewer_system", "sky_system" ] }
    ]
}