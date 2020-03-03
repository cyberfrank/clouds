blend_states : {
    logical_operation_enable: false
    render_target_0 : {
        blend_enable: true
        write_mask : "red|green|blue|alpha"
    }
}

imports : [
    { name: "texture" type: "texture_2d" }
]

vertex_shader : {
    import_system_semantics : [ "vertex_id" ]

    code : [[
        static const float2 pos[3] = {
            { -1,  1 },
            {  3,  1 },
            { -1, -3 },
        };

        output.position = float4(pos[input.vertex_id], 1, 1);
        return output;
    ]]
}

pixel_shader : {
    exports : [
        { name: "color" type: "float4" }
    ]

    code : [[
        float2 res = load_render_target_resolution();
        float aspect = res.y / res.x;
        int3 pos = int3(input.position.x - 64 * aspect, res.y - input.position.y - 32 * aspect, 0) * 4;
        output.color = get_texture().Load(pos);
        return output;
    ]]
}

compile : {
    includes : [ "common" ]

    variations : [
        { systems: [ "viewer_system" ] }
    ]
}