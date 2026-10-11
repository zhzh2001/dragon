// The micro-benchmark's vertex program: about what the game's sm2 terrain
// does per vertex -- a transform, a sun term, a fog factor.
float4x4 mvp : register(c0);
float4 sun : register(c4);
float4 fog : register(c5);
struct VSIn { float3 pos : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : POSITION; float4 colour : COLOR0; float2 uv : TEXCOORD0; float fog : TEXCOORD1; };
VSOut main(VSIn i) {
    VSOut o;
    o.pos = mul(float4(i.pos, 1), mvp);
    float d = saturate(dot(i.normal, sun.xyz));
    o.colour = float4(0.25 + 0.75 * d, 0.3 + 0.6 * d, 0.2 + 0.5 * d, 1);
    o.uv = i.uv * 8;
    o.fog = saturate((o.pos.w - fog.x) * fog.y);
    return o;
}
