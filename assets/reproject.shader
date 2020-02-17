depth_stencil_states : {
    depth_test_enable : true
    depth_write_enable : false
    depth_compare_op : "less_equal"
}

raster_states : {
    polygon_mode: "fill"
}

samplers : {
    linear : {
        anisotropy_enable: true
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
    //
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
        // use linear filtering for upscaling
        float4 clouds = get_textures(0).Sample(get_sampler_linear(), input.uv);
        // hdr accumulation target
        float4 hdr = get_textures(1).Load(int3(input.position.xy, 0));

        output.color = float4(clouds.rgb + hdr.rgb * clouds.a, 1.0);
        return output;
    ]]
}

compile : {
    includes : [ "common" ]

    variations : [
        { systems: [ "viewer_system" ] },
    ]
}