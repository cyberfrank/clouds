samplers : {
    linear : {
        anisotropy_enable: false
        min_filter: "linear"
        max_filter: "linear"
        mip_mode: "linear"
    }
}

imports : [
]

common : [[
    float set_range(float value, float low, float high) {
        return saturate((value - low)/(high - low));
    }

    float hash12(float2 p, float f)
    {
        p = abs(p % f);
        float3 p3 = frac(float3(p.xyx) * .1031);
        p3 += dot(p3, p3.yzx + 33.33);
        return frac((p3.x + p3.y) * p3.z);
    }

   float perlin_noise(float2 x, float freq) 
    {
        x *= freq; // tile


        float2 f = frac(x);
        x = floor(x);

        float a = hash12(x, freq);
        float b = hash12(x + float2(1.0, 0.0), freq);
        float c = hash12(x + float2(0.0, 1.0), freq);
        float d = hash12(x + float2(1.0, 1.0), freq);

        float2 u = f * f * (3.0 - 2.0 * f);
        return lerp(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
    }

    float fbm(float2 p, float freq, int octaves) {
        float amp = 0.5;
        float noise = 0.0;
        for (int i = 0; i < octaves; ++i) {
            noise += amp * perlin_noise(p, freq);
            amp *= 0.5;
            freq *= 2.0;
        }
        return noise;
    }
]]

vertex_shader : {
    import_system_semantics : [ "vertex_id" ]

    exports : [        
        { name: "uv" type: "float2" }
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
        { name: "color" type: "float4" }
    ]

    code : [[
        float2 uv = input.uv;

        const float coverage = 0.45;
        const float smoothness = 0.5;

        float r = fbm(uv, 16, 5);
        r = set_range(r, coverage, 1.0 + 2.0 * smoothness - 1.0);

        float b = fbm(uv, 8, 5);
        b = set_range(b, 1.0, 0.0);

        output.color = saturate(float4(r, 0, b, 1));
        return output;
    ]]
}

compile : {
    includes : [ "common" ]

    variations : [
        { systems: [ "frame_system", "viewer_system" ] }
    ]
}