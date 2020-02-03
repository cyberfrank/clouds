depth_stencil_states : {
    depth_test_enable : true
    depth_write_enable : false
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
    { name: "weather_texture" type: "texture_2d" }
    { name: "sampler_wrap_linear" type: "sampler" sampler: "wrap_linear" }      
]

common : [[  
    #define PLANET_RADIUS 63710
    #define ATMOSPHERE_START 1500
    #define ATMOSPHERE_THICKNESS 4500

    float remap(in float value, in float old_min, in float old_max, in float new_min, in float new_max) {
        return new_min + (((value - old_min) / (old_max - old_min)) * (new_max - new_min));
    }

    // project to atmosphere shell
    float3 project_to_shell(in float offset, in float3 dir) {
        float radius = PLANET_RADIUS + offset;
        float3 camera_pos = load_camera_position();
        float3 pos = camera_pos + float3(0, PLANET_RADIUS, 0);
        float a = dot(dir, dir) * 2;
        float b = dot(dir, pos) * 2;
        float c = dot(pos, pos);
        float disc = b * b - 2 * a * (c - radius * radius);
        float t = max(0, (-b + sqrt(disc)) / a);
        return camera_pos + dir * t;
    }

    float get_shape(in float3 pos)
    {
        Texture3D shape = get_cloud_shape();
        SamplerState sampler = get_sampler_wrap_linear();

        return shape.Sample(sampler, pos * 0.0001).r;
    }

    float get_detail(in float3 pos)
    {
        return 0;
    }

    float height_gradient(in float coverage, in float height)
    {
        return pow(coverage, clamp(remap(height, 0.7, 0.8, 1.0, 0.8), 0.8, 1.0));
    }

    float height_signal(float height, float altitude_start, float3 pos)
    {
        // parabola function
        // (x-a)*(x-a-h)*(-4/h^2)
        // x = altitude in atmosphere
        // a = start altitude
        // h = cloud height
        // height *= ATMOSPHERE_THICKNESS;
        // altitude_start = altitude_start * ATMOSPHERE_THICKNESS;

        float altitude = (length(pos) - (ATMOSPHERE_START + PLANET_RADIUS)) / ATMOSPHERE_THICKNESS;

        float one_over_height = 1.0 / height;
        float altitude_diff = altitude - altitude_start;
        return altitude_diff * (altitude_diff - height) * one_over_height * one_over_height * -4;
    }

    float get_density(in float3 pos) {
        float density = 1;
        //density = get_weather_texture().Sample(get_sampler_wrap_linear(), 0.00003 * pos.xz).r;
        density *= height_signal(1, 0, pos);
        density *= get_shape(pos);

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

        if (dot(v, float3(0, 1, 0)) < 0.0)
        {
            // output.color = float4(get_sky_color_physical(v), 1);
            // return output;
        }

        float3 inner = project_to_shell(ATMOSPHERE_START, v);
        float3 outer = project_to_shell(ATMOSPHERE_START + PLANET_RADIUS, v);

        uint steps = 20;
        float step_size = distance(inner, outer) / steps;
        float t = step_size * 0.5;

        float density = 0;
        float accum_density = 0;
        float transmittance = 1;
        float cos_theta = dot(v, load_sun_direction());
        // dual lobe phase function
        float hg_dual = lerp(hg_phase(cos_theta, 0.8), hg_phase(cos_theta, 0.5), 0.5);
        // 
        for (uint step = 0; step < steps; ++step)
        {
            float3 pos = inner + v * t;
            density = get_density(pos);

            if (density > 0)
            {
                transmittance += density;
            }

            t += step_size;
        }

        float3 col = get_sky_color_physical(v) * exp(-transmittance * 0.5);
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
        { name: "weather_texture" display_name: "Weather Texture" target: { name: "weather_texture" } optional: true }
    ]
}

compile : {
    includes : [ "common", "sampling" ]

    variations : [        
        //{ systems : [ "frame_system", "viewer_system" ] },
        { systems : [ "frame_system", "viewer_system", "sky_system" ] },
    ]
}