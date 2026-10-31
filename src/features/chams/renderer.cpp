#include <stdafx.hpp>
#include <diagnostics/performance.hpp>
#include <game/state/pose.hpp>
#include <game/state/services.hpp>
#include <game/memory/bound_services.hpp>
#include <simulation/collision.hpp>
#include <render/index_run.hpp>
#include <features/chams/renderer.hpp>
#include <features/chams/mesh_cache.hpp>
#include <features/chams/effect_bounds.hpp>
#include <features/visuals/visuals.hpp>

#include <d3dcompiler.h>

#include <cstddef>

#pragma comment(lib, "d3dcompiler.lib")

namespace features::chams
{

namespace
{

// skinned_vertex must map directly onto the D3D11 input layout below --
// no CPU-side repack between extract_mesh's output and the GPU buffer.
static_assert(sizeof(skinned_vertex) == 56);
static_assert(offsetof(skinned_vertex, position) == 0);
static_assert(offsetof(skinned_vertex, normal) == 12);
static_assert(offsetof(skinned_vertex, tangent) == 24);
static_assert(offsetof(skinned_vertex, uv) == 40);
static_assert(offsetof(skinned_vertex, bone_indices) == 48);
static_assert(offsetof(skinned_vertex, bone_weights) == 52);

struct cb_view_projection
{
    float matrix[4][4];
    float eye[3];
    float time; // seconds, for the animated materials
};

struct cb_bones
{
    float matrix[renderer::k_max_bones][4][4];
};

struct gpu_material
{
    float color[4];
    float tint[4];
    int type;
    float roughness;
    float metalness;
    float exponent;
    float falloff;
    float fresnel_fill;
    float strength;
    float speed;
};
static_assert(sizeof(gpu_material) == 64);

struct cb_material
{
    gpu_material visible{};
    gpu_material invisible{};
    int split_by_world_depth{};
    int visible_enabled{1};
    int invisible_enabled{};
    int layer{}; // 0 = both, 1 = visible only, 2 = invisible only
    int antialias_split{};
    float shell_expand{};
    float effect_progress{};
    float effect_seed{};
};
static_assert(sizeof(cb_material) % 16 == 0);
struct cb_bloom
{
    float texel[2]{};
    float direction[2]{};
    float strength{};
    float target_size[2]{};
    float padding{};
};
static_assert(sizeof(cb_bloom) % 16 == 0);

struct cb_world_effect
{
    float surface_color[4]{};
    float line_color[4]{};
    float eye_and_distance[4]{}; // eye, then the render distance (0: none)
    float smoke_spheres[8][4]{};
    std::uint32_t smoke_count{};
    std::uint32_t clip_to_smoke{};
    std::uint32_t style{};    // config::visual_profile::world_style
    std::uint32_t shadows{};  // the shadow map is bound
    float sun_direction[4]{}; // towards the sun; w the shadow map texel size in uv
    float line_params[4]{};   // line width in pixels, overall opacity
    float light_view_projection[4][4]{};
};
static_assert(sizeof(cb_world_effect) == 288);

void write_gpu_material(gpu_material& out, const config::visual_profile::chams::material& material)
{
    const auto& color = material.color;
    out.color[0] = color.r / 255.0f;
    out.color[1] = color.g / 255.0f;
    out.color[2] = color.b / 255.0f;
    out.color[3] = color.a / 255.0f;

    const auto& tint = material.tint;
    out.tint[0] = tint.r / 255.0f;
    out.tint[1] = tint.g / 255.0f;
    out.tint[2] = tint.b / 255.0f;
    out.tint[3] = tint.a / 255.0f;

    out.type = std::clamp(material.type, 0, static_cast<int>(config::visual_profile::chams::material_type_count) - 1);
    out.roughness = material.roughness;
    out.metalness = material.metalness;
    out.exponent = material.exponent;
    out.falloff = material.falloff;
    out.fresnel_fill = material.fresnel_fill;
    out.strength = material.strength;
    out.speed = material.speed;
}

constexpr const char* k_shader_source = R"(
			cbuffer CBViewProjection : register(b0)
			{
				row_major float4x4 g_ViewProjection;
				float3 g_EyePos;
				float  g_Time;
			};

			cbuffer CBBones : register(b1)
			{
				row_major float4x4 g_Bones[128];
			};

			struct MaterialData
			{
				float4 Color;
				float4 Tint;
				int    Type;
				float  Roughness;
				float  Metalness;
				float  Exponent;
				float  Falloff;
				float  FresnelFill;
				float  Strength;
				float  Speed;
			};

			cbuffer CBMaterial : register(b2)
			{
				MaterialData g_Visible;
				MaterialData g_Invisible;
				int g_SplitByWorldDepth;
				int g_VisibleEnabled;
				int g_InvisibleEnabled;
				int g_Layer;
				int g_AntialiasSplit;
				float g_ShellExpand;
				float g_EffectProgress;
				float g_EffectSeed;
			};

			Texture2D<float> g_WorldDepth : register(t0);

			struct VS_INPUT
			{
				float3 Position    : POSITION;
				float3 Normal      : NORMAL;
				float4 Tangent     : TANGENT;
				float2 UV          : TEXCOORD0;
				uint4  BoneIndices : BLENDINDICES;
				float4 BoneWeights : BLENDWEIGHT;
			};

			struct PS_INPUT
			{
				float4 Position : SV_POSITION;
				float3 WorldPos : TEXCOORD0;
				float3 Normal   : TEXCOORD1;
			};

			PS_INPUT VS_Main(VS_INPUT input)
			{
				PS_INPUT output;
				float4 skinned = float4(0, 0, 0, 0);
				float3 normal  = float3(0, 0, 0);

				[unroll]
				for (int i = 0; i < 4; i++)
				{
					float weight = input.BoneWeights[i];
					if (weight > 0.0001f)
					{
						float4x4 bone = g_Bones[input.BoneIndices[i]];
						skinned += mul(bone, float4(input.Position, 1.0f)) * weight;
						// Bones are rotation + uniform scale only, so the rotation
						// block transforms the normal directly -- no inverse
						// transpose needed.
						normal  += mul((float3x3)bone, input.Normal) * weight;
					}
				}
				skinned.w = 1.0f;
				skinned.xyz += normalize(normal) * g_ShellExpand;

				output.Position = mul(g_ViewProjection, skinned);
				output.WorldPos = skinned.xyz;
				output.Normal   = normal;
				return output;
			}

			float4 Shade(MaterialData material, PS_INPUT input)
			{
				float3 N = normalize(input.Normal);
				float3 V = normalize(g_EyePos - input.WorldPos);

				// Two-sided: flip inward-facing normals so back faces and any
				// mirrored geometry light the same way as the front.
				N = dot(N, V) < 0.0f ? -N : N;

				// The light sits at the camera, so L == V and there is no angle at
				// which an enemy falls into shadow.
				float3 L = V;
				float3 H = normalize(L + V);
				float  ndv = saturate(dot(N, V));
				float  ndl = saturate(dot(N, L));
				float  ndh = saturate(dot(N, H));
				float  rim = 1.0f - ndv;

				float3 rgb = material.Color.rgb;

				if (material.Type == 1) // shaded
				{
					float  shininess = lerp(96.0f, 4.0f, saturate(material.Roughness));
					float  spec = pow(ndh, shininess) * (1.0f - saturate(material.Roughness));
					float3 specColor = lerp(float3(1, 1, 1), material.Color.rgb, saturate(material.Metalness));
					float3 diffuse = material.Color.rgb * (0.25f + 0.75f * ndl);
					// A soft self-lit fresnel rim gives the body some shape at its
					// silhouette instead of reading as one flat fill.
					float  fres = rim * rim * 0.20f;
					rgb = lerp(diffuse, material.Color.rgb * 0.18f, saturate(material.Metalness))
						+ specColor * spec + material.Color.rgb * fres;
				}
				else if (material.Type == 2) // glow
				{
					rgb = material.Color.rgb * (pow(rim, max(material.Exponent, 0.01f)) + 0.12f);
				}
				else if (material.Type == 3) // glow outline
				{
					// Falloff sets how tight the rim band is, fresnel fill lifts
					// the interior so the body is not fully hollow.
					float band = pow(rim, max(material.Falloff * 8.0f, 0.01f));
					rgb = material.Color.rgb * (band * max(material.Exponent, 0.0f) + saturate(material.FresnelFill));
				}
				else if (material.Type == 4) // iridescent
				{
					float3 sweep = 0.5f + 0.5f * cos(6.28318f * (rim * 2.0f + g_Time * material.Speed * 0.15f + float3(0.0f, 0.33f, 0.67f)));
					float  spec = pow(ndh, lerp(96.0f, 4.0f, saturate(material.Roughness)));
					rgb = lerp(material.Color.rgb, sweep, saturate(material.Strength)) * (0.35f + 0.65f * ndl)
						+ spec * (1.0f - saturate(material.Roughness));
				}
				else if (material.Type == 5) // water
				{
					// A rippling water surface rather than a single scrolling band.
					// Several sine waves of different frequency and direction are
					// summed over the world position so the highlights drift and
					// interfere the way light does on moving water. Driven by world
					// position (this pipeline never decodes UVs) and g_Time.
					float  t = g_Time * max(material.Speed, 0.0001f);
					float3 p = input.WorldPos;
					float  w  = sin(p.x * 0.11f + p.z * 0.07f + t * 2.0f);
					       w += sin(p.z * 0.17f - p.y * 0.09f + t * 1.6f) * 0.7f;
					       w += sin((p.x + p.y) * 0.08f + t * 1.1f) * 0.5f;
					       w += sin(p.y * 0.23f + t * 2.7f) * 0.35f;
					w = w * 0.4f; // back into roughly [-1, 1]
					float  ripple = 0.5f + 0.5f * w;

					// Sharpen the crests into thin bright lines, like specular
					// glints skating across the surface.
					float  crest = pow(saturate(ripple), 3.0f);
					float  fres  = pow(1.0f - ndv, 2.5f);

					float3 baseCol = material.Color.rgb * (0.32f + 0.45f * ndl);
					float3 sheen   = lerp(material.Color.rgb, float3(1.0f, 1.0f, 1.0f), 0.65f);
					rgb = baseCol
						+ sheen * (crest * 0.65f * max(material.Strength, 0.15f) + fres * 0.45f);
				}
				else if (material.Type == 6) // glossy
				{
					float spec = pow(ndh, max(material.Exponent * 16.0f, 1.0f));
					float band = pow(rim, max(material.Falloff * 8.0f, 0.01f));
					rgb = material.Color.rgb * (0.25f + 0.75f * ndl)
						+ material.Tint.rgb * (spec + band * saturate(material.FresnelFill));
				}

				return float4(rgb, material.Color.a);
			}

			float Hash11(float value)
			{
				return frac(sin(value * 91.3458f + g_EffectSeed * 17.17f) * 47453.5453f);
			}

			[maxvertexcount(3)]
			void GS_Death(triangle PS_INPUT input[3], uint primitiveId : SV_PrimitiveID,
				inout TriangleStream<PS_INPUT> stream)
			{
				float progress = saturate(g_EffectProgress);
				float3 center = (input[0].WorldPos + input[1].WorldPos + input[2].WorldPos) / 3.0f;
				float angle = Hash11((float)primitiveId + 1.0f) * 6.2831853f;
				float radial = 24.0f + Hash11((float)primitiveId + 19.0f) * 52.0f;
				float3 direction = float3(cos(angle), sin(angle),
					0.25f + Hash11((float)primitiveId + 47.0f) * 0.85f);
				float3 displacement = direction * radial * progress * progress;
				displacement.z -= 58.0f * progress * progress;
				float shrink = lerp(1.0f, 0.18f, progress);
				[unroll]
				for (int vertex = 0; vertex < 3; ++vertex)
				{
					PS_INPUT output = input[vertex];
					output.WorldPos = center + (input[vertex].WorldPos - center) * shrink + displacement;
					output.Position = mul(g_ViewProjection, float4(output.WorldPos, 1.0f));
					stream.Append(output);
				}
				stream.RestartStrip();
			}

			float4 Composite(PS_INPUT input, float occlusion)
			{
				if (g_SplitByWorldDepth == 0)
				{
					return Shade(g_Visible, input);
				}

				// Blend only the one-pixel geometric transition. This preserves the
				// exact world-depth classification while avoiding a full-scene MSAA
				// render target and resolve.
				if (g_AntialiasSplit != 0 && g_Layer == 0
					&& g_VisibleEnabled != 0 && g_InvisibleEnabled != 0)
				{
					return lerp(Shade(g_Visible, input), Shade(g_Invisible, input), occlusion);
				}
				if (g_AntialiasSplit != 0 && g_VisibleEnabled != 0
					&& g_InvisibleEnabled == 0 && g_Layer != 2)
				{
					if (occlusion >= 0.999f) discard;
					float4 shaded = Shade(g_Visible, input);
					shaded.a *= 1.0f - occlusion;
					return shaded;
				}
				if (g_AntialiasSplit != 0 && g_InvisibleEnabled != 0
					&& g_VisibleEnabled == 0 && g_Layer != 1)
				{
					if (occlusion <= 0.001f) discard;
					float4 shaded = Shade(g_Invisible, input);
					shaded.a *= occlusion;
					return shaded;
				}

				if (occlusion >= 0.5f)
				{
					if (g_InvisibleEnabled == 0 || g_Layer == 1) discard;
					return Shade(g_Invisible, input);
				}

				if (g_VisibleEnabled == 0 || g_Layer == 2) discard;
				return Shade(g_Visible, input);
			}

			float4 PS_Main(PS_INPUT input) : SV_TARGET
			{
				uint width, height;
				g_WorldDepth.GetDimensions(width, height);
				int2 pixel = clamp(int2(input.Position.xy), int2(0, 0), int2(width - 1, height - 1));
				const float epsilon = 0.00000012f;
				float occlusion = input.Position.z > g_WorldDepth.Load(int3(pixel, 0)) + epsilon ? 1.0f : 0.0f;
				if (g_AntialiasSplit != 0)
				{
					int2 limit = int2(width - 1, height - 1);
					occlusion += input.Position.z > g_WorldDepth.Load(int3(clamp(pixel + int2(-1, 0), int2(0, 0), limit), 0)) + epsilon ? 1.0f : 0.0f;
					occlusion += input.Position.z > g_WorldDepth.Load(int3(clamp(pixel + int2( 1, 0), int2(0, 0), limit), 0)) + epsilon ? 1.0f : 0.0f;
					occlusion += input.Position.z > g_WorldDepth.Load(int3(clamp(pixel + int2(0, -1), int2(0, 0), limit), 0)) + epsilon ? 1.0f : 0.0f;
					occlusion += input.Position.z > g_WorldDepth.Load(int3(clamp(pixel + int2(0,  1), int2(0, 0), limit), 0)) + epsilon ? 1.0f : 0.0f;
					occlusion *= 0.2f;
				}
				return Composite(input, occlusion);
			}

			float4 PS_BloomMask(PS_INPUT input) : SV_TARGET
			{
				// Bloom needs a saturated silhouette only. Sampling the full-resolution
				// world-depth texture from a half-resolution target used mismatched pixel
				// coordinates and desaturated/discarded large parts of the glow mask.
				return float4(g_Visible.Color.rgb * g_Visible.Color.a,
					g_Visible.Color.a);
			}

		)";

// No Flash / No Smoke world view. The depth pre-pass uses VS_World alone; the colour pass adds GS_World (each corner
// gets its barycentric coordinate) and PS_World: flat-shaded lit surfaces (normal from the screen derivatives, a sun with
// a shadow map of the static map, sky/ground ambient, a little light from the eye) and anti-aliased triangle edges drawn
// in the same shader, so edges and surfaces share one depth and never flicker against each other. With a smoke clip a
// surface shows only where the view to it passes through a smoke sphere (inside a smoke: everything), fading softly at
// the smoke's silhouette.
constexpr const char* k_world_shader_source = R"(
			cbuffer CBViewProjection : register(b0)
			{
				row_major float4x4 g_ViewProjection;
				float3 g_EyePos;
				float  g_Time;
			};

			cbuffer CBWorldEffect : register(b1)
			{
				float4 g_SurfaceColor;
				float4 g_LineColor;
				float4 g_EyeAndDistance;
				float4 g_SmokeSpheres[8];
				uint g_SmokeCount;
				uint g_ClipToSmoke;
				uint g_Style;
				uint g_Shadows;
				float4 g_SunDirection;
				float4 g_LineParams;
				row_major float4x4 g_LightViewProjection;
			};

			Texture2D<float> g_ShadowMap : register(t0);
			SamplerComparisonState g_ShadowSampler : register(s0);

			struct WORLD_OUTPUT
			{
				float4 Position : SV_POSITION;
				float3 WorldPos : TEXCOORD0;
			};

			struct WORLD_GS_OUTPUT
			{
				float4 Position : SV_POSITION;
				float3 WorldPos : TEXCOORD0;
				float3 Barycentric : TEXCOORD1;
			};

			WORLD_OUTPUT VS_World(float3 position : POSITION)
			{
				WORLD_OUTPUT output;
				output.Position = mul(g_ViewProjection, float4(position, 1.0f));
				output.WorldPos = position;
				return output;
			}

			[maxvertexcount(3)]
			void GS_World(triangle WORLD_OUTPUT input[3], inout TriangleStream<WORLD_GS_OUTPUT> stream)
			{
				[unroll]
				for (uint i = 0; i < 3; ++i)
				{
					WORLD_GS_OUTPUT output;
					output.Position = input[i].Position;
					output.WorldPos = input[i].WorldPos;
					output.Barycentric = float3(i == 0, i == 1, i == 2);
					stream.Append(output);
				}
			}

			// How much of the view to `p` runs through a smoke: 1 up to the sphere's silhouette, then soft over the next
			// 10% of the radius (outside it, so the zone is never smaller than the smoke) instead of a hard pixel edge.
			float SmokeCoverage(float3 p)
			{
				// The clouds are ellipsoids (wider than high): squashing z by radius / height makes them spheres.
				float3 squash = float3(1.0f, 1.0f, g_LineParams.z);
				float3 eye = g_EyeAndDistance.xyz * squash;
				p *= squash;
				float3 ray = p - eye;
				float len = length(ray);
				ray /= max(len, 0.001f);
				float coverage = 0.0f;
				[loop]
				for (uint i = 0; i < min(g_SmokeCount, 8u); ++i)
				{
					float3 center = g_SmokeSpheres[i].xyz * squash;
					float radius = g_SmokeSpheres[i].w;
					float t = clamp(dot(center - eye, ray), 0.0f, len);
					float gap = distance(eye + ray * t, center);
					coverage = max(coverage, saturate((radius * 1.1f - gap) / (radius * 0.1f)));
				}
				return coverage;
			}

			float SunShadow(float3 p, float3 n)
			{
				float3 offset = p + n * 3.0f + g_SunDirection.xyz * 2.0f;
				float4 light = mul(g_LightViewProjection, float4(offset, 1.0f));
				float2 uv = float2(light.x * 0.5f + 0.5f, 0.5f - light.y * 0.5f);
				if (any(uv < 0.0f) || any(uv > 1.0f) || light.z >= 1.0f)
					return 1.0f;
				float texel = g_SunDirection.w;
				float depth = light.z - 0.0006f;
				float sum = 0.0f;
				[unroll]
				for (int y = -1; y <= 1; ++y)
				{
					[unroll]
					for (int x = -1; x <= 1; ++x)
						sum += g_ShadowMap.SampleCmpLevelZero(g_ShadowSampler, uv + float2(x, y) * texel, depth);
				}
				return sum / 9.0f;
			}

			float4 PS_World(WORLD_GS_OUTPUT input) : SV_TARGET
			{
				// Derivatives first, while every pixel of the quad is still running.
				float3 p = input.WorldPos;
				float3 normal = cross(ddx(p), ddy(p));
				float3 edgeWidth = fwidth(input.Barycentric);

				float3 toEye = g_EyeAndDistance.xyz - p;
				float dist = length(toEye);
				float visibility = g_LineParams.y;
				if (g_EyeAndDistance.w > 0.0f)
					visibility *= 1.0f - smoothstep(g_EyeAndDistance.w * 0.85f, g_EyeAndDistance.w, dist);
				if (g_ClipToSmoke != 0)
					visibility *= SmokeCoverage(p);

				float3 view = toEye / max(dist, 0.001f);
				float normalLength = length(normal);
				normal = normalLength > 1e-8f ? normal / normalLength : view;
				if (dot(normal, view) < 0.0f)
					normal = -normal;

				float3 surface = 0.0f;
				float surfaceAlpha = 0.0f;
				if (g_Style != 2)
				{
					float lambert = saturate(dot(normal, g_SunDirection.xyz));
					float shadow = g_Shadows != 0 && lambert > 0.0f ? SunShadow(p, normal) : 1.0f;
					float ambient = lerp(0.30f, 0.52f, normal.z * 0.5f + 0.5f);
					float lit = ambient + 0.16f * saturate(dot(normal, view)) + 0.62f * lambert * shadow;
					surface = g_SurfaceColor.rgb * lit;
					surface = lerp(surface, g_SurfaceColor.rgb * 0.5f, saturate(dist / 5000.0f) * 0.35f);
					surfaceAlpha = g_SurfaceColor.a;
				}

				float lineAlpha = 0.0f;
				if (g_Style != 0)
				{
					// Distance to the nearest edge in pixels; each of the two triangles at an edge draws half the width.
					float3 pixels = input.Barycentric / max(edgeWidth, 1e-6f);
					float nearest = min(pixels.x, min(pixels.y, pixels.z));
					lineAlpha = saturate(g_LineParams.x * 0.5f + 0.5f - nearest) * g_LineColor.a;
				}

				float alpha = lineAlpha + surfaceAlpha * (1.0f - lineAlpha);
				if (alpha * visibility <= 0.002f)
					discard;
				float3 color = (g_LineColor.rgb * lineAlpha + surface * surfaceAlpha * (1.0f - lineAlpha)) / alpha;
				return float4(color, alpha * visibility);
			}
		)";

constexpr const char* k_resolve_shader_source = R"(
			Texture2DMS<float4> g_MsaaColor : register(t0);

			float4 VS_Resolve(uint vertexId : SV_VertexID) : SV_POSITION
			{
				float2 position;
				position.x = vertexId == 2 ? 3.0f : -1.0f;
				position.y = vertexId == 1 ? 3.0f : -1.0f;
				return float4(position, 0.0f, 1.0f);
			}

			float4 PS_Resolve(float4 position : SV_POSITION) : SV_TARGET
			{
				uint width, height, samples;
				g_MsaaColor.GetDimensions(width, height, samples);
				int2 pixel = clamp(int2(position.xy), int2(0, 0), int2(width - 1, height - 1));
				float4 color = float4(0, 0, 0, 0);
				[loop]
				for (uint sample = 0; sample < samples; ++sample)
					color += g_MsaaColor.Load(pixel, sample);
				color /= max(float(samples), 1.0f);
				if (color.a <= 0.0001f) discard;
				color.rgb /= color.a;
				return color;
			}
		)";

constexpr const char* k_bloom_shader_source = R"(
			Texture2D<float4> g_Source : register(t0);
			Texture2D<float4> g_Original : register(t1);
			Texture2D<float4> g_Inner : register(t2);
			SamplerState g_Linear : register(s0);
			cbuffer CBBloom : register(b0)
			{
				float2 g_Texel;
				float2 g_Direction;
				float g_Strength;
				float2 g_TargetSize;
				float g_Padding;
			};
			struct FullscreenOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
			FullscreenOut VS_Bloom(uint id : SV_VertexID)
			{
				FullscreenOut output;
				float2 position = float2(id == 2 ? 3.0f : -1.0f,
					id == 1 ? 3.0f : -1.0f);
				output.position = float4(position, 0.0f, 1.0f);
				output.uv = float2(position.x * 0.5f + 0.5f,
					0.5f - position.y * 0.5f);
				return output;
			}
				float4 PS_Blur(FullscreenOut input) : SV_TARGET
				{
					// g_Direction carries the requested radius in output pixels. The previous
					// five-tap approximation multiplied its already separated tap offsets by
					// that radius. On a distant silhouette those taps no longer overlapped and
					// appeared as a grid of complete model copies. Sample every neighbouring
					// pixel instead, then normalize the real Gaussian kernel.
					float radius = clamp(max(abs(g_Direction.x), abs(g_Direction.y)), 1.0f, 16.0f);
					float2 axis = abs(g_Direction.x) >= abs(g_Direction.y)
						? float2(1.0f, 0.0f) : float2(0.0f, 1.0f);
					int kernelRadius = (int)ceil(radius);
					float sigma = max(radius * 0.50f, 0.75f);
					float inverseTwoSigmaSquared = 0.5f / (sigma * sigma);
					float4 color = 0.0f;
					float totalWeight = 0.0f;
					[unroll]
					for (int offset = -16; offset <= 16; ++offset)
					{
						if (abs(offset) > kernelRadius) continue;
						float distance = (float)offset;
						float weight = exp(-distance * distance * inverseTwoSigmaSquared);
						color += g_Source.Sample(g_Linear,
							input.uv + axis * g_Texel * distance) * weight;
						totalWeight += weight;
					}
					return color / max(totalWeight, 0.0001f);
				}
				float4 PS_Composite(FullscreenOut input) : SV_TARGET
				{
					float4 outer = g_Source.Sample(g_Linear, input.uv);
					float4 inner = g_Inner.Sample(g_Linear, input.uv);
					float sourceCoverage = g_Original.Sample(g_Linear, input.uv).a;
					if (g_Padding > 0.5f)
					{
						// Model mode: g_Inner is the perspective-expanded silhouette and
						// g_Original is the real silhouette. Their difference is the solid,
						// distance-correct rim; the narrow outer blur only softens its edge.
						float originalSoft = sourceCoverage * 0.40f;
						originalSoft += g_Original.Sample(g_Linear,
							input.uv + float2(g_Texel.x * 0.5f, 0.0f)).a * 0.15f;
						originalSoft += g_Original.Sample(g_Linear,
							input.uv - float2(g_Texel.x * 0.5f, 0.0f)).a * 0.15f;
						originalSoft += g_Original.Sample(g_Linear,
							input.uv + float2(0.0f, g_Texel.y * 0.5f)).a * 0.15f;
						originalSoft += g_Original.Sample(g_Linear,
							input.uv - float2(0.0f, g_Texel.y * 0.5f)).a * 0.15f;
						float expandedSoft = inner.a;
						float shell = max(inner.a - originalSoft, 0.0f);
						float falloff = outer.a * (1.0f
							- smoothstep(0.12f, 0.82f, expandedSoft));
						float halo = shell * 0.90f + falloff * 1.20f;
						float coverage = smoothstep(0.003f, 0.72f, halo)
							* saturate(g_Strength * 1.45f);
						float colorWeight = inner.a + outer.a;
						float3 modelChroma = colorWeight > 0.0005f
							? (inner.rgb + outer.rgb) / colorWeight : 0.0f;
						return float4(saturate(modelChroma), coverage);
					}
					float blurCoverage = inner.a * 0.75f + outer.a * 0.55f;
				float3 weighted = inner.rgb * 0.75f + outer.rgb * 0.55f;
				// Recover the emissive hue from coverage. This prevents RGB from being
				// multiplied by alpha both during filtering and again during composition.
				float3 chroma = blurCoverage > 0.0005f
					? weighted / blurCoverage : float3(0.0f, 0.0f, 0.0f);
				// True screen-space outer glow. Subtracting the linearly filtered source
				// silhouette from both lobes removes every foggy model copy. The tight lobe
				// forms a soft coloured rim, the wide lobe only its falloff.
					// Do not cut the halo with the raw, single-sample triangle edge: that
					// reproduces its staircase exactly. A sub-pixel inner lobe supplies a
					// smooth silhouette exclusion while the original mask only prevents light
					// from painting the middle of large models.
					float sourceSoft = sourceCoverage * 0.40f;
					sourceSoft += g_Original.Sample(g_Linear,
						input.uv + float2(g_Texel.x * 0.5f, 0.0f)).a * 0.15f;
					sourceSoft += g_Original.Sample(g_Linear,
						input.uv - float2(g_Texel.x * 0.5f, 0.0f)).a * 0.15f;
					sourceSoft += g_Original.Sample(g_Linear,
						input.uv + float2(0.0f, g_Texel.y * 0.5f)).a * 0.15f;
					sourceSoft += g_Original.Sample(g_Linear,
						input.uv - float2(0.0f, g_Texel.y * 0.5f)).a * 0.15f;
					float softInterior = smoothstep(0.18f, 0.78f,
						max(inner.a, sourceSoft * 0.82f));
					float tightHalo = inner.a * (1.0f - softInterior);
					float wideHalo = outer.a * (1.0f - softInterior);
					float halo = tightHalo * 1.25f + wideHalo * 0.85f;
					// Never amplify the near-zero tails. sqrt(halo) made the normally invisible
					// corners of the separable kernel visible when the projected model became
					// small, so the distant glow expanded into a rectangular fog patch. Remove
					// only that numerical tail and shape the remaining coverage monotonically.
					float compactHalo = smoothstep(0.004f, 0.22f, max(halo, 0.0f));
					// Keep the sharp source itself emissive as well. UI bloom is composited
					// after ImGui, so this coloured core illuminates the arrow/trajectory line
					// instead of only brightening the background outside its black outline.
					float emissiveCore = sourceCoverage * 0.62f;
					float glow = max(compactHalo, emissiveCore)
						* saturate(g_Strength * 1.45f);
					return float4(saturate(chroma), glow);
			}
			struct Bloom2DIn { float2 position : POSITION; float4 color : COLOR0; };
			struct Bloom2DOut { float4 position : SV_POSITION; float4 color : COLOR0; };
			Bloom2DOut VS_Bloom2D(Bloom2DIn input)
			{
				Bloom2DOut output;
				output.position = float4(input.position.x / g_TargetSize.x * 2.0f - 1.0f,
					1.0f - input.position.y / g_TargetSize.y * 2.0f, 0.0f, 1.0f);
				output.color = input.color;
				return output;
			}
			float4 PS_Bloom2D(Bloom2DOut input) : SV_TARGET
			{
				// Preserve real coverage. The old constant alpha=1 turned minimum-intensity
				// UI bloom into an opaque grey patch and made the background, rather than
				// the arrow itself, appear illuminated.
				return float4(input.color.rgb * input.color.a, input.color.a);
			}
		)";

// A bone's world transform as a matrix: rotation, uniform scale (1 except a big head) and position.
[[nodiscard]] bone_matrix compose_world_bone(const foundation::vec3& pos, const foundation::rotation& rot,
                                             const float scale = 1.0f)
{
    bone_matrix out{};
    const auto x = rot.x, y = rot.y, z = rot.z, w = rot.w;
    const auto xx = x * x, yy = y * y, zz = z * z;
    const auto xy = x * y, xz = x * z, yz = y * z;
    const auto wx = w * x, wy = w * y, wz = w * z;

    out.m[0][0] = 1.0f - 2.0f * (yy + zz);
    out.m[0][1] = 2.0f * (xy - wz);
    out.m[0][2] = 2.0f * (xz + wy);
    out.m[0][3] = pos.x;
    out.m[1][0] = 2.0f * (xy + wz);
    out.m[1][1] = 1.0f - 2.0f * (xx + zz);
    out.m[1][2] = 2.0f * (yz - wx);
    out.m[1][3] = pos.y;
    out.m[2][0] = 2.0f * (xz - wy);
    out.m[2][1] = 2.0f * (yz + wx);
    out.m[2][2] = 1.0f - 2.0f * (xx + yy);
    out.m[2][3] = pos.z;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            out.m[row][column] *= scale;
    return out;
}

void write_matrix4(float dst[4][4], const bone_matrix& src)
{
    for (int r = 0; r < 3; ++r)
    {
        for (int c = 0; c < 4; ++c)
            dst[r][c] = src.m[r][c];
    }
    dst[3][0] = 0.0f;
    dst[3][1] = 0.0f;
    dst[3][2] = 0.0f;
    dst[3][3] = 1.0f;
}

} // namespace

struct renderer::frame_cache
{
    struct smoke_sphere
    {
        foundation::vec3 center{};
        float radius{};
    };
    struct draw_range
    {
        std::uint32_t first_index{};
        std::uint32_t index_count{};
    };
    struct drawable
    {
        const gpu_mesh* gpu{};
        const skinned_mesh* mesh{};
        const game::player_snapshot* current{};
        const game::skeleton_reader::data* bones{};
        ID3D11Buffer* bone_buffer{};
        std::vector<bone_matrix>* skin_matrices{};
        world_bounds bounds{};
    };
    struct drawable_identity
    {
        std::uintptr_t pawn{};
        std::uintptr_t bone_cache{};
        std::string model_path{};
    };

    // Player chams: the pose frame and roster the draw list was built for, and what was counted then.
    std::vector<drawable> drawables{};
    std::vector<std::vector<bone_matrix>> skin_scratch{};
    std::shared_ptr<const game::player_pose_frame> current_frame{};
    std::shared_ptr<const std::vector<game::player_snapshot>> roster_world{};
    std::vector<drawable_identity> roster_identity{};
    std::vector<drawable_identity> next_identity{};
    std::vector<std::uintptr_t> enemy_pawns{};
    std::array<int, 5> counts{};
    std::string model_path{};
    std::vector<screen_volume> occlusion_volumes{};
    std::vector<screen_volume> resolve_volumes{};

    // World effects.
    std::vector<smoke_sphere> smoke_spheres{};
    std::vector<draw_range> static_chunks{};
    std::vector<draw_range> dynamic_chunks{};
};

renderer::renderer()
    : m_frame_cache(std::make_unique<frame_cache>())
{
}

renderer::~renderer() = default;

bool renderer::initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_device = device;
    m_context = context;

    if (!create_shaders())
    {
        m_diag.init_error = "shader compile/creation failed";
        game::bound_memory().log.warning("[chams] {}", m_diag.init_error);
        m_diag.renderer_ready = false;
        return false;
    }

    if (!create_constant_buffers() || !create_pipeline_states())
    {
        m_diag.init_error = "constant buffer / pipeline state creation failed";
        game::bound_memory().log.warning("[chams] {}", m_diag.init_error);
        m_diag.renderer_ready = false;
        return false;
    }

    m_ready = true;
    m_diag.renderer_ready = true;
    return true;
}

bool renderer::ensure_vpk()
{
    if (m_vpk_ready)
    {
        return true;
    }
    if (!m_vpk_load_started)
    {
        m_vpk_load_started = true;
        m_vpk_future =
            std::async(std::launch::async,
                       []
                       {
                           const auto background_mode =
                               ::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN) != FALSE;
                           if (!background_mode)
                               ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

                           assets::vpk_archive archive{};
                           if (const auto path = assets::vpk_archive::locate_cs2_pak(); !path.empty())
                               static_cast<void>(archive.open(path));

                           if (background_mode)
                               ::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
                           return archive;
                       });
        return false;
    }
    if (!m_vpk_future.valid() || m_vpk_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        return false;
    }

    m_vpk = m_vpk_future.get();
    m_vpk_ready = m_vpk.is_open();
    m_diag.vpk_ready = m_vpk_ready;
    if (m_vpk_ready)
    {
        m_diag.init_error.clear();
    }
    else
    {
        m_diag.init_error = "CS2 pak not found or failed to open";
        game::bound_memory().log.warning("[chams] {}", m_diag.init_error);
    }
    return m_vpk_ready;
}

void renderer::drain_background() noexcept
{
    g_mesh_cache.drain_background();
    m_vpk_future = {};
}

void renderer::shutdown()
{
    drain_background();
    release_gpu_meshes();

    if (m_vertex_shader)
        m_vertex_shader->Release();
    if (m_pixel_shader)
        m_pixel_shader->Release();
    if (m_bloom_mask_shader)
        m_bloom_mask_shader->Release();
    if (m_death_geometry_shader)
        m_death_geometry_shader->Release();
    if (m_input_layout)
        m_input_layout->Release();
    if (m_resolve_vertex_shader)
        m_resolve_vertex_shader->Release();
    if (m_resolve_pixel_shader)
        m_resolve_pixel_shader->Release();
    if (m_bloom_vertex_shader)
        m_bloom_vertex_shader->Release();
    if (m_bloom_blur_shader)
        m_bloom_blur_shader->Release();
    if (m_bloom_composite_shader)
        m_bloom_composite_shader->Release();
    if (m_bloom_2d_vertex_shader)
        m_bloom_2d_vertex_shader->Release();
    if (m_bloom_2d_pixel_shader)
        m_bloom_2d_pixel_shader->Release();
    if (m_bloom_2d_input_layout)
        m_bloom_2d_input_layout->Release();
    if (m_bloom_2d_vertex_buffer)
        m_bloom_2d_vertex_buffer->Release();
    if (m_cb_view_projection)
        m_cb_view_projection->Release();
    if (m_cb_bones)
        m_cb_bones->Release();
    if (m_cb_material)
        m_cb_material->Release();
    if (m_cb_bloom)
        m_cb_bloom->Release();
    if (m_cb_world_effect)
        m_cb_world_effect->Release();
    for (auto* buffer : m_frame_bone_buffers)
    {
        if (buffer)
            buffer->Release();
    }
    m_frame_bone_buffers.clear();
    if (m_rs_solid)
        m_rs_solid->Release();
    if (m_rs_wireframe)
        m_rs_wireframe->Release();
    if (m_rs_wireframe_scissor)
        m_rs_wireframe_scissor->Release();
    if (m_rs_world_scissor)
        m_rs_world_scissor->Release();
    if (m_blend_state)
        m_blend_state->Release();
    if (m_bloom_blend_state)
        m_bloom_blend_state->Release();
    if (m_blend_disabled)
        m_blend_disabled->Release();
    if (m_bloom_sampler)
        m_bloom_sampler->Release();
    if (m_depth_state)
        m_depth_state->Release();
    if (m_depth_state_read_only)
        m_depth_state_read_only->Release();
    if (m_depth_state_disabled)
        m_depth_state_disabled->Release();
    if (m_world_depth_state)
        m_world_depth_state->Release();
    if (m_depth_state_equal)
        m_depth_state_equal->Release();
    if (m_world_vertex_shader)
        m_world_vertex_shader->Release();
    if (m_world_pixel_shader)
        m_world_pixel_shader->Release();
    if (m_world_geometry_shader)
        m_world_geometry_shader->Release();
    if (m_world_input_layout)
        m_world_input_layout->Release();
    if (m_rs_shadow)
        m_rs_shadow->Release();
    if (m_shadow_sampler)
        m_shadow_sampler->Release();
    release_world_shadow_map();
    release_world_geometry();
    release_depth_buffer();
    release_world_depth_buffer();
    release_msaa_targets();
    release_bloom_targets();

    m_ready = false;
}

void renderer::release_gpu_meshes()
{
    for (auto& [path, mesh] : m_gpu_meshes)
    {
        if (mesh.vertex_buffer)
            mesh.vertex_buffer->Release();
        if (mesh.index_buffer)
            mesh.index_buffer->Release();
    }
    m_gpu_meshes.clear();
}

bool renderer::create_shaders()
{
    ID3DBlob* vs_blob{};
    ID3DBlob* ps_blob{};
    ID3DBlob* bloom_mask_blob{};
    ID3DBlob* gs_blob{};
    ID3DBlob* error_blob{};

    auto hr = D3DCompile(k_shader_source, std::strlen(k_shader_source), nullptr, nullptr, nullptr, "VS_Main", "vs_5_0",
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs_blob, &error_blob);
    if (FAILED(hr))
    {
        if (error_blob)
        {
            game::bound_memory().log.warning("[chams] VS compile error: {}",
                                             static_cast<const char*>(error_blob->GetBufferPointer()));
            error_blob->Release();
        }
        return false;
    }

    hr = D3DCompile(k_shader_source, std::strlen(k_shader_source), nullptr, nullptr, nullptr, "PS_Main", "ps_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps_blob, &error_blob);
    if (FAILED(hr))
    {
        if (error_blob)
        {
            game::bound_memory().log.warning("[chams] PS compile error: {}",
                                             static_cast<const char*>(error_blob->GetBufferPointer()));
            error_blob->Release();
        }
        vs_blob->Release();
        return false;
    }
    hr = D3DCompile(k_shader_source, std::strlen(k_shader_source), nullptr, nullptr, nullptr, "PS_BloomMask", "ps_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bloom_mask_blob, &error_blob);
    if (FAILED(hr))
    {
        if (error_blob)
            error_blob->Release();
        vs_blob->Release();
        ps_blob->Release();
        return false;
    }

    m_device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &m_vertex_shader);
    m_device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &m_pixel_shader);
    m_device->CreatePixelShader(bloom_mask_blob->GetBufferPointer(), bloom_mask_blob->GetBufferSize(), nullptr,
                                &m_bloom_mask_shader);
    bloom_mask_blob->Release();
    hr = D3DCompile(k_shader_source, std::strlen(k_shader_source), nullptr, nullptr, nullptr, "GS_Death", "gs_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &gs_blob, &error_blob);
    if (FAILED(hr))
    {
        if (error_blob)
            error_blob->Release();
        vs_blob->Release();
        ps_blob->Release();
        return false;
    }
    m_device->CreateGeometryShader(gs_blob->GetBufferPointer(), gs_blob->GetBufferSize(), nullptr,
                                   &m_death_geometry_shader);
    gs_blob->Release();

    const D3D11_INPUT_ELEMENT_DESC layout[]{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    hr = m_device->CreateInputLayout(layout, 6, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &m_input_layout);

    vs_blob->Release();
    ps_blob->Release();

    if (FAILED(hr))
    {
        return false;
    }

    ID3DBlob* world_blob{};
    ID3DBlob* world_pixel_blob{};
    hr = D3DCompile(k_world_shader_source, std::strlen(k_world_shader_source), nullptr, nullptr, nullptr, "VS_World",
                    "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &world_blob, &error_blob);
    if (FAILED(hr))
    {
        if (error_blob)
        {
            game::bound_memory().log.warning("[chams] world VS compile error: {}",
                                             static_cast<const char*>(error_blob->GetBufferPointer()));
            error_blob->Release();
        }
        return false;
    }

    m_device->CreateVertexShader(world_blob->GetBufferPointer(), world_blob->GetBufferSize(), nullptr,
                                 &m_world_vertex_shader);

    const D3D11_INPUT_ELEMENT_DESC world_layout[]{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    hr = m_device->CreateInputLayout(world_layout, 1, world_blob->GetBufferPointer(), world_blob->GetBufferSize(),
                                     &m_world_input_layout);
    world_blob->Release();
    if (FAILED(hr))
        return false;

    hr = D3DCompile(k_world_shader_source, std::strlen(k_world_shader_source), nullptr, nullptr, nullptr, "PS_World",
                    "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &world_pixel_blob, &error_blob);
    if (FAILED(hr))
    {
        if (error_blob)
        {
            game::bound_memory().log.warning("[chams] world PS compile error: {}",
                                             static_cast<const char*>(error_blob->GetBufferPointer()));
            error_blob->Release();
        }
        return false;
    }
    hr = m_device->CreatePixelShader(world_pixel_blob->GetBufferPointer(), world_pixel_blob->GetBufferSize(), nullptr,
                                     &m_world_pixel_shader);
    world_pixel_blob->Release();
    if (FAILED(hr))
        return false;

    ID3DBlob* world_geometry_blob{};
    hr = D3DCompile(k_world_shader_source, std::strlen(k_world_shader_source), nullptr, nullptr, nullptr, "GS_World",
                    "gs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &world_geometry_blob, &error_blob);
    if (FAILED(hr))
    {
        if (error_blob)
        {
            game::bound_memory().log.warning("[chams] world GS compile error: {}",
                                             static_cast<const char*>(error_blob->GetBufferPointer()));
            error_blob->Release();
        }
        return false;
    }
    hr = m_device->CreateGeometryShader(world_geometry_blob->GetBufferPointer(), world_geometry_blob->GetBufferSize(),
                                        nullptr, &m_world_geometry_shader);
    world_geometry_blob->Release();
    if (FAILED(hr))
        return false;

    ID3DBlob* resolve_vs_blob{};
    ID3DBlob* resolve_ps_blob{};
    hr = D3DCompile(k_resolve_shader_source, std::strlen(k_resolve_shader_source), nullptr, nullptr, nullptr,
                    "VS_Resolve", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &resolve_vs_blob, &error_blob);
    if (FAILED(hr))
    {
        if (error_blob)
        {
            game::bound_memory().log.warning("[chams] resolve VS compile error: {}",
                                             static_cast<const char*>(error_blob->GetBufferPointer()));
            error_blob->Release();
        }
        return false;
    }
    hr = D3DCompile(k_resolve_shader_source, std::strlen(k_resolve_shader_source), nullptr, nullptr, nullptr,
                    "PS_Resolve", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &resolve_ps_blob, &error_blob);
    if (FAILED(hr))
    {
        if (error_blob)
        {
            game::bound_memory().log.warning("[chams] resolve PS compile error: {}",
                                             static_cast<const char*>(error_blob->GetBufferPointer()));
            error_blob->Release();
        }
        resolve_vs_blob->Release();
        return false;
    }

    const auto resolve_vs_result = m_device->CreateVertexShader(
        resolve_vs_blob->GetBufferPointer(), resolve_vs_blob->GetBufferSize(), nullptr, &m_resolve_vertex_shader);
    const auto resolve_ps_result = m_device->CreatePixelShader(
        resolve_ps_blob->GetBufferPointer(), resolve_ps_blob->GetBufferSize(), nullptr, &m_resolve_pixel_shader);
    resolve_vs_blob->Release();
    resolve_ps_blob->Release();

    ID3DBlob* bloom_vs_blob{};
    ID3DBlob* bloom_blur_blob{};
    ID3DBlob* bloom_composite_blob{};
    ID3DBlob* bloom_2d_vs_blob{};
    ID3DBlob* bloom_2d_ps_blob{};
    hr = D3DCompile(k_bloom_shader_source, std::strlen(k_bloom_shader_source), nullptr, nullptr, nullptr, "VS_Bloom",
                    "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bloom_vs_blob, &error_blob);
    if (SUCCEEDED(hr))
        hr = D3DCompile(k_bloom_shader_source, std::strlen(k_bloom_shader_source), nullptr, nullptr, nullptr, "PS_Blur",
                        "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bloom_blur_blob, &error_blob);
    if (SUCCEEDED(hr))
        hr =
            D3DCompile(k_bloom_shader_source, std::strlen(k_bloom_shader_source), nullptr, nullptr, nullptr,
                       "PS_Composite", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bloom_composite_blob, &error_blob);
    if (SUCCEEDED(hr))
        hr = D3DCompile(k_bloom_shader_source, std::strlen(k_bloom_shader_source), nullptr, nullptr, nullptr,
                        "VS_Bloom2D", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bloom_2d_vs_blob, &error_blob);
    if (SUCCEEDED(hr))
        hr = D3DCompile(k_bloom_shader_source, std::strlen(k_bloom_shader_source), nullptr, nullptr, nullptr,
                        "PS_Bloom2D", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bloom_2d_ps_blob, &error_blob);
    if (FAILED(hr))
    {
        if (error_blob)
            error_blob->Release();
        if (bloom_vs_blob)
            bloom_vs_blob->Release();
        if (bloom_blur_blob)
            bloom_blur_blob->Release();
        if (bloom_composite_blob)
            bloom_composite_blob->Release();
        if (bloom_2d_vs_blob)
            bloom_2d_vs_blob->Release();
        if (bloom_2d_ps_blob)
            bloom_2d_ps_blob->Release();
        return false;
    }
    const auto bloom_vs_result = m_device->CreateVertexShader(
        bloom_vs_blob->GetBufferPointer(), bloom_vs_blob->GetBufferSize(), nullptr, &m_bloom_vertex_shader);
    const auto bloom_blur_result = m_device->CreatePixelShader(
        bloom_blur_blob->GetBufferPointer(), bloom_blur_blob->GetBufferSize(), nullptr, &m_bloom_blur_shader);
    const auto bloom_composite_result =
        m_device->CreatePixelShader(bloom_composite_blob->GetBufferPointer(), bloom_composite_blob->GetBufferSize(),
                                    nullptr, &m_bloom_composite_shader);
    const auto bloom_2d_vs_result = m_device->CreateVertexShader(
        bloom_2d_vs_blob->GetBufferPointer(), bloom_2d_vs_blob->GetBufferSize(), nullptr, &m_bloom_2d_vertex_shader);
    const auto bloom_2d_ps_result = m_device->CreatePixelShader(
        bloom_2d_ps_blob->GetBufferPointer(), bloom_2d_ps_blob->GetBufferSize(), nullptr, &m_bloom_2d_pixel_shader);
    const D3D11_INPUT_ELEMENT_DESC bloom_2d_layout[]{
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    const auto bloom_2d_layout_result =
        m_device->CreateInputLayout(bloom_2d_layout, 2, bloom_2d_vs_blob->GetBufferPointer(),
                                    bloom_2d_vs_blob->GetBufferSize(), &m_bloom_2d_input_layout);
    bloom_vs_blob->Release();
    bloom_blur_blob->Release();
    bloom_composite_blob->Release();
    bloom_2d_vs_blob->Release();
    bloom_2d_ps_blob->Release();

    return SUCCEEDED(resolve_vs_result) && SUCCEEDED(resolve_ps_result) && SUCCEEDED(bloom_vs_result) &&
           SUCCEEDED(bloom_blur_result) && SUCCEEDED(bloom_composite_result) && SUCCEEDED(bloom_2d_vs_result) &&
           SUCCEEDED(bloom_2d_ps_result) && SUCCEEDED(bloom_2d_layout_result) && m_vertex_shader && m_pixel_shader &&
           m_bloom_mask_shader && m_death_geometry_shader && m_world_vertex_shader && m_world_pixel_shader &&
           m_world_geometry_shader && m_world_input_layout;
}

bool renderer::create_constant_buffers()
{
    D3D11_BUFFER_DESC desc{};
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    desc.ByteWidth = sizeof(cb_view_projection);
    if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_cb_view_projection)))
        return false;

    desc.ByteWidth = sizeof(cb_bones);
    if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_cb_bones)))
        return false;

    desc.ByteWidth = sizeof(cb_material);
    if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_cb_material)))
        return false;

    desc.ByteWidth = sizeof(cb_bloom);
    if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_cb_bloom)))
        return false;

    desc.ByteWidth = sizeof(cb_world_effect);
    if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_cb_world_effect)))
        return false;

    return true;
}

bool renderer::create_pipeline_states()
{
    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;

    rs.CullMode = D3D11_CULL_NONE;
    rs.DepthClipEnable = TRUE;
    rs.MultisampleEnable = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rs, &m_rs_solid)))
        return false;

    rs.FillMode = D3D11_FILL_WIREFRAME;
    if (FAILED(m_device->CreateRasterizerState(&rs, &m_rs_wireframe)))
        return false;
    rs.ScissorEnable = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rs, &m_rs_wireframe_scissor)))
        return false;

    rs.FillMode = D3D11_FILL_SOLID;
    if (FAILED(m_device->CreateRasterizerState(&rs, &m_rs_world_scissor)))
        return false;

    // The sun's shadow map of the map: biased away from the light so lit surfaces do not shadow themselves.
    D3D11_RASTERIZER_DESC shadow_rs{};
    shadow_rs.FillMode = D3D11_FILL_SOLID;
    shadow_rs.CullMode = D3D11_CULL_NONE;
    shadow_rs.DepthClipEnable = TRUE;
    shadow_rs.DepthBias = 24;
    shadow_rs.SlopeScaledDepthBias = 2.0f;
    shadow_rs.DepthBiasClamp = 0.01f;
    if (FAILED(m_device->CreateRasterizerState(&shadow_rs, &m_rs_shadow)))
        return false;
    D3D11_SAMPLER_DESC shadow_sampler{};
    shadow_sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    shadow_sampler.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    shadow_sampler.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    shadow_sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    shadow_sampler.BorderColor[0] = shadow_sampler.BorderColor[1] = shadow_sampler.BorderColor[2] =
        shadow_sampler.BorderColor[3] = 1.0f;
    shadow_sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    shadow_sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(m_device->CreateSamplerState(&shadow_sampler, &m_shadow_sampler)))
        return false;

    D3D11_BLEND_DESC bs{};
    bs.RenderTarget[0].BlendEnable = TRUE;
    bs.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bs.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bs.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bs.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bs.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bs.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bs.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_device->CreateBlendState(&bs, &m_blend_state)))
        return false;

    D3D11_BLEND_DESC bloom_bs = bs;
    bloom_bs.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    if (FAILED(m_device->CreateBlendState(&bloom_bs, &m_bloom_blend_state)))
        return false;

    D3D11_BLEND_DESC opaque{};
    opaque.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_device->CreateBlendState(&opaque, &m_blend_disabled)))
        return false;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(m_device->CreateSamplerState(&sampler, &m_bloom_sampler)))
        return false;

    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    ds.StencilEnable = FALSE;
    if (FAILED(m_device->CreateDepthStencilState(&ds, &m_depth_state)))
        return false;

    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(m_device->CreateDepthStencilState(&ds, &m_depth_state_equal)))
        return false;

    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(m_device->CreateDepthStencilState(&ds, &m_depth_state_read_only)))
        return false;

    ds.DepthEnable = FALSE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(m_device->CreateDepthStencilState(&ds, &m_depth_state_disabled)))
        return false;

    D3D11_DEPTH_STENCIL_DESC world{};
    world.DepthEnable = TRUE;
    world.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    world.DepthFunc = D3D11_COMPARISON_LESS;
    world.StencilEnable = FALSE;
    if (FAILED(m_device->CreateDepthStencilState(&world, &m_world_depth_state)))
        return false;

    return true;
}

bool renderer::ensure_depth_buffer(const UINT width, const UINT height)
{
    if (width == 0 || height == 0)
    {
        return false;
    }

    if (m_dsv && width == m_depth_width && height == m_depth_height)
    {
        return true;
    }

    release_depth_buffer();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_depth_texture)))
    {
        return false;
    }

    if (FAILED(m_device->CreateDepthStencilView(m_depth_texture, nullptr, &m_dsv)))
    {
        release_depth_buffer();
        return false;
    }

    m_depth_width = width;
    m_depth_height = height;
    return true;
}

bool renderer::ensure_world_depth_buffer(const UINT width, const UINT height)
{
    if (width == 0 || height == 0)
        return false;

    if (m_world_dsv && m_world_depth_srv && width == m_world_depth_width && height == m_world_depth_height)
    {
        return true;
    }

    release_world_depth_buffer();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_world_depth_texture)))
    {
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(m_device->CreateDepthStencilView(m_world_depth_texture, &dsv_desc, &m_world_dsv)))
    {
        release_world_depth_buffer();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    if (FAILED(m_device->CreateShaderResourceView(m_world_depth_texture, &srv_desc, &m_world_depth_srv)))
    {
        release_world_depth_buffer();
        return false;
    }

    m_world_depth_width = width;
    m_world_depth_height = height;
    return true;
}

bool renderer::ensure_msaa_targets(const UINT width, const UINT height)
{
    if (width == 0 || height == 0)
        return false;

    if (width == m_msaa_width && height == m_msaa_height && m_msaa_samples != 0)
    {
        return m_msaa_samples > 1 && m_msaa_rtv && m_msaa_srv && m_msaa_dsv;
    }

    release_msaa_targets();

    // Four samples remain inexpensive at the common game resolutions. At 4K,
    // two samples avoid doubling the already sizeable transient color/depth pair.
    const auto pixels = static_cast<std::uint64_t>(width) * height;
    const std::array candidates = pixels <= 2560ull * 1440ull ? std::array<UINT, 2>{4, 2} : std::array<UINT, 2>{2, 4};
    UINT samples{};
    for (const auto candidate : candidates)
    {
        UINT color_quality{};
        UINT depth_quality{};
        if (SUCCEEDED(m_device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, candidate, &color_quality)) &&
            SUCCEEDED(m_device->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT, candidate, &depth_quality)) &&
            color_quality != 0 && depth_quality != 0)
        {
            samples = candidate;
            break;
        }
    }

    // Cache unsupported dimensions too, so the capability probe is not repeated
    // on every frame. A one-sample value means that the normal path is used.
    m_msaa_width = width;
    m_msaa_height = height;
    m_msaa_samples = samples ? samples : 1;
    if (samples == 0)
        return false;

    D3D11_TEXTURE2D_DESC color_desc{};
    color_desc.Width = width;
    color_desc.Height = height;
    color_desc.MipLevels = 1;
    color_desc.ArraySize = 1;
    color_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    color_desc.SampleDesc.Count = samples;
    color_desc.Usage = D3D11_USAGE_DEFAULT;
    color_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_device->CreateTexture2D(&color_desc, nullptr, &m_msaa_color)) ||
        FAILED(m_device->CreateRenderTargetView(m_msaa_color, nullptr, &m_msaa_rtv)))
    {
        release_msaa_targets();
        m_msaa_width = width;
        m_msaa_height = height;
        m_msaa_samples = 1;
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = color_desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
    if (FAILED(m_device->CreateShaderResourceView(m_msaa_color, &srv_desc, &m_msaa_srv)))
    {
        release_msaa_targets();
        m_msaa_width = width;
        m_msaa_height = height;
        m_msaa_samples = 1;
        return false;
    }

    D3D11_TEXTURE2D_DESC depth_desc{};
    depth_desc.Width = width;
    depth_desc.Height = height;
    depth_desc.MipLevels = 1;
    depth_desc.ArraySize = 1;
    depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_desc.SampleDesc.Count = samples;
    depth_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(m_device->CreateTexture2D(&depth_desc, nullptr, &m_msaa_depth)) ||
        FAILED(m_device->CreateDepthStencilView(m_msaa_depth, nullptr, &m_msaa_dsv)))
    {
        release_msaa_targets();
        m_msaa_width = width;
        m_msaa_height = height;
        m_msaa_samples = 1;
        return false;
    }

    return true;
}

void renderer::resolve_msaa(ID3D11RenderTargetView* backbuffer_rtv, const std::vector<screen_volume>& volumes)
{
    if (!backbuffer_rtv || !m_msaa_srv || !m_resolve_vertex_shader || !m_resolve_pixel_shader || volumes.empty())
    {
        return;
    }

    float union_min_x{1.0f}, union_min_y{1.0f};
    float union_max_x{-1.0f}, union_max_y{-1.0f};
    for (const auto& volume : volumes)
    {
        union_min_x = std::min(union_min_x, volume.min_x);
        union_min_y = std::min(union_min_y, volume.min_y);
        union_max_x = std::max(union_max_x, volume.max_x);
        union_max_y = std::max(union_max_y, volume.max_y);
    }

    const auto width = static_cast<LONG>(m_msaa_width);
    const auto height = static_cast<LONG>(m_msaa_height);
    D3D11_RECT scissor{
        .left = static_cast<LONG>(std::floor((std::clamp(union_min_x, -1.0f, 1.0f) * 0.5f + 0.5f) * width)),
        .top = static_cast<LONG>(std::floor((0.5f - std::clamp(union_max_y, -1.0f, 1.0f) * 0.5f) * height)),
        .right = static_cast<LONG>(std::ceil((std::clamp(union_max_x, -1.0f, 1.0f) * 0.5f + 0.5f) * width)),
        .bottom = static_cast<LONG>(std::ceil((0.5f - std::clamp(union_min_y, -1.0f, 1.0f) * 0.5f) * height))};
    // Cover all partially occupied edge samples, including the interpolation
    // margin that was added while projecting each player's animated bounds.
    scissor.left = std::clamp<LONG>(scissor.left - 2, 0, width);
    scissor.top = std::clamp<LONG>(scissor.top - 2, 0, height);
    scissor.right = std::clamp<LONG>(scissor.right + 2, 0, width);
    scissor.bottom = std::clamp<LONG>(scissor.bottom + 2, 0, height);
    if (scissor.left >= scissor.right || scissor.top >= scissor.bottom)
        return;

    m_context->OMSetRenderTargets(1, &backbuffer_rtv, nullptr);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* null_buffer{};
    const UINT zero{};
    m_context->IASetVertexBuffers(0, 1, &null_buffer, &zero, &zero);
    m_context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    m_context->VSSetShader(m_resolve_vertex_shader, nullptr, 0);
    m_context->PSSetShader(m_resolve_pixel_shader, nullptr, 0);
    m_context->PSSetShaderResources(0, 1, &m_msaa_srv);
    m_context->RSSetState(m_rs_world_scissor);
    m_context->RSSetScissorRects(1, &scissor);
    m_context->OMSetDepthStencilState(nullptr, 0);
    const float blend_factor[4]{};
    m_context->OMSetBlendState(m_blend_state, blend_factor, 0xFFFFFFFF);
    m_context->Draw(3, 0);

    ID3D11ShaderResourceView* null_srv{};
    m_context->PSSetShaderResources(0, 1, &null_srv);
}

bool renderer::ensure_world_geometry()
{
    VESTA_PERF_SCOPE(chams_geometry);
    const auto geometry = simulation::collision().render_geometry();
    static_assert(sizeof(foundation::vec3) == 12);
    static_assert(std::is_trivially_copyable_v<foundation::vec3>);

    const auto release = [](world_geometry& target)
    {
        if (target.vertex_buffer)
            target.vertex_buffer->Release();
        if (target.index_buffer)
            target.index_buffer->Release();
        target = {};
    };
    const auto upload = [&](const std::shared_ptr<const simulation::collision_world::render_snapshot::mesh>& source,
                            const std::uint64_t revision, world_geometry& target)
    {
        if (target.revision == revision && target.vertex_buffer && target.index_buffer)
            return true;
        if (!source || source->vertices.empty() || source->indices.empty())
        {
            release(target);
            target.revision = revision;
            return true;
        }

        world_geometry fresh{};
        D3D11_BUFFER_DESC vertex_desc{};
        vertex_desc.ByteWidth = static_cast<UINT>(source->vertices.size() * sizeof(foundation::vec3));
        vertex_desc.Usage = D3D11_USAGE_IMMUTABLE;
        vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertex_data{source->vertices.data()};
        if (FAILED(m_device->CreateBuffer(&vertex_desc, &vertex_data, &fresh.vertex_buffer)))
            return false;

        D3D11_BUFFER_DESC index_desc{};
        index_desc.ByteWidth = static_cast<UINT>(source->indices.size() * sizeof(std::uint32_t));
        index_desc.Usage = D3D11_USAGE_IMMUTABLE;
        index_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA index_data{source->indices.data()};
        if (FAILED(m_device->CreateBuffer(&index_desc, &index_data, &fresh.index_buffer)))
        {
            fresh.vertex_buffer->Release();
            return false;
        }

        fresh.index_count = static_cast<std::uint32_t>(source->indices.size());
        fresh.revision = revision;
        fresh.chunks.reserve(source->batches.size());
        for (const auto& batch : source->batches)
        {
            if (batch.index_count == 0 || batch.first_index >= fresh.index_count ||
                batch.index_count > fresh.index_count - batch.first_index)
                continue;
            fresh.chunks.push_back({.bounds = {batch.mins, batch.maxs},
                                    .first_index = batch.first_index,
                                    .index_count = batch.index_count});
        }

        release(target);
        target = std::move(fresh);
        return true;
    };

    return upload(geometry.world, geometry.world_revision, m_static_world) &&
           upload(geometry.entities, geometry.entity_revision, m_dynamic_world) &&
           (m_static_world.index_count || m_dynamic_world.index_count);
}

void renderer::draw_world_depth(ID3D11DepthStencilView* dsv, const std::vector<screen_volume>& occlusion_volumes,
                                const foundation::matrix4& view_projection)
{
    VESTA_PERF_SCOPE(chams_world_depth);
    m_context->OMSetRenderTargets(0, nullptr, dsv);
    m_context->IASetInputLayout(m_world_input_layout);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_world_vertex_shader, nullptr, 0);
    m_context->PSSetShader(nullptr, nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, &m_cb_view_projection);
    m_context->OMSetDepthStencilState(m_world_depth_state, 0);
    m_context->RSSetState(m_rs_world_scissor);

    float union_min_x{1.0f}, union_min_y{1.0f};
    float union_max_x{-1.0f}, union_max_y{-1.0f};
    for (const auto& volume : occlusion_volumes)
    {
        union_min_x = std::min(union_min_x, volume.min_x);
        union_min_y = std::min(union_min_y, volume.min_y);
        union_max_x = std::max(union_max_x, volume.max_x);
        union_max_y = std::max(union_max_y, volume.max_y);
    }
    D3D11_RECT scissor{
        .left =
            static_cast<LONG>(std::floor((std::clamp(union_min_x, -1.0f, 1.0f) * 0.5f + 0.5f) * m_world_depth_width)),
        .top =
            static_cast<LONG>(std::floor((0.5f - std::clamp(union_max_y, -1.0f, 1.0f) * 0.5f) * m_world_depth_height)),
        .right =
            static_cast<LONG>(std::ceil((std::clamp(union_max_x, -1.0f, 1.0f) * 0.5f + 0.5f) * m_world_depth_width)),
        .bottom =
            static_cast<LONG>(std::ceil((0.5f - std::clamp(union_min_y, -1.0f, 1.0f) * 0.5f) * m_world_depth_height))};
    const auto width = static_cast<LONG>(m_world_depth_width);
    const auto height = static_cast<LONG>(m_world_depth_height);
    scissor.left = std::clamp<LONG>(scissor.left - 2, 0, width);
    scissor.top = std::clamp<LONG>(scissor.top - 2, 0, height);
    scissor.right = std::clamp<LONG>(scissor.right + 2, 0, width);
    scissor.bottom = std::clamp<LONG>(scissor.bottom + 2, 0, height);
    m_context->RSSetScissorRects(1, &scissor);

    const auto draw_geometry = [&](world_geometry& geometry)
    {
        if (!geometry.vertex_buffer || !geometry.index_buffer || !geometry.index_count)
            return;
        const UINT stride = sizeof(foundation::vec3);
        const UINT offset = 0;
        m_context->IASetVertexBuffers(0, 1, &geometry.vertex_buffer, &stride, &offset);
        m_context->IASetIndexBuffer(geometry.index_buffer, DXGI_FORMAT_R32_UINT, 0);

        render::index_run run;
        const auto emit = [&](std::uint32_t first, std::uint32_t count) { m_context->DrawIndexed(count, first, 0); };
        geometry.depth_projection.update(view_projection, geometry.chunks);
        geometry.depth_projection.select(occlusion_volumes,
                                         [&](std::size_t index)
                                         {
                                             const auto& chunk = geometry.chunks[index];
                                             run.append(chunk.first_index, chunk.index_count, emit);
                                         });
        run.flush(emit);
    };

    draw_geometry(m_static_world);
    if (config::visual_settings.m_chams.occlude_dynamic_doors)
        draw_geometry(m_dynamic_world);
}

void renderer::release_world_geometry()
{
    const auto release = [](world_geometry& geometry)
    {
        if (geometry.vertex_buffer)
            geometry.vertex_buffer->Release();
        if (geometry.index_buffer)
            geometry.index_buffer->Release();
        geometry = {};
    };
    release(m_static_world);
    release(m_dynamic_world);
}

void renderer::release_depth_buffer()
{
    if (m_dsv)
    {
        m_dsv->Release();
        m_dsv = nullptr;
    }
    if (m_depth_texture)
    {
        m_depth_texture->Release();
        m_depth_texture = nullptr;
    }
    m_depth_width = 0;
    m_depth_height = 0;
}

void renderer::release_world_depth_buffer()
{
    if (m_world_depth_srv)
    {
        m_world_depth_srv->Release();
        m_world_depth_srv = nullptr;
    }
    if (m_world_dsv)
    {
        m_world_dsv->Release();
        m_world_dsv = nullptr;
    }
    if (m_world_depth_texture)
    {
        m_world_depth_texture->Release();
        m_world_depth_texture = nullptr;
    }
    m_world_depth_width = 0;
    m_world_depth_height = 0;
}

void renderer::release_msaa_targets()
{
    if (m_msaa_dsv)
    {
        m_msaa_dsv->Release();
        m_msaa_dsv = nullptr;
    }
    if (m_msaa_depth)
    {
        m_msaa_depth->Release();
        m_msaa_depth = nullptr;
    }
    if (m_msaa_srv)
    {
        m_msaa_srv->Release();
        m_msaa_srv = nullptr;
    }
    if (m_msaa_rtv)
    {
        m_msaa_rtv->Release();
        m_msaa_rtv = nullptr;
    }
    if (m_msaa_color)
    {
        m_msaa_color->Release();
        m_msaa_color = nullptr;
    }
    m_msaa_width = 0;
    m_msaa_height = 0;
    m_msaa_samples = 0;
}

bool renderer::ensure_bloom_targets(const UINT width, const UINT height)
{
    const auto bloom_width = std::max<UINT>(1, width);
    const auto bloom_height = std::max<UINT>(1, height);
    if (m_bloom_a_rtv && m_bloom_a_srv && m_bloom_b_rtv && m_bloom_b_srv && m_bloom_source_rtv && m_bloom_source_srv &&
        m_bloom_inner_rtv && m_bloom_inner_srv && m_bloom_dsv && m_bloom_width == bloom_width &&
        m_bloom_height == bloom_height)
        return true;

    release_bloom_targets();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = bloom_width;
    desc.Height = bloom_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    const auto create = [&](ID3D11Texture2D** texture, ID3D11RenderTargetView** rtv, ID3D11ShaderResourceView** srv)
    {
        return SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, texture)) &&
               SUCCEEDED(m_device->CreateRenderTargetView(*texture, nullptr, rtv)) &&
               SUCCEEDED(m_device->CreateShaderResourceView(*texture, nullptr, srv));
    };
    if (!create(&m_bloom_a, &m_bloom_a_rtv, &m_bloom_a_srv) || !create(&m_bloom_b, &m_bloom_b_rtv, &m_bloom_b_srv) ||
        !create(&m_bloom_source, &m_bloom_source_rtv, &m_bloom_source_srv) ||
        !create(&m_bloom_inner, &m_bloom_inner_rtv, &m_bloom_inner_srv))
    {
        release_bloom_targets();
        return false;
    }
    D3D11_TEXTURE2D_DESC depth_desc{};
    depth_desc.Width = bloom_width;
    depth_desc.Height = bloom_height;
    depth_desc.MipLevels = 1;
    depth_desc.ArraySize = 1;
    depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_desc.SampleDesc.Count = 1;
    depth_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(m_device->CreateTexture2D(&depth_desc, nullptr, &m_bloom_depth)) ||
        FAILED(m_device->CreateDepthStencilView(m_bloom_depth, nullptr, &m_bloom_dsv)))
    {
        release_bloom_targets();
        return false;
    }
    m_bloom_width = bloom_width;
    m_bloom_height = bloom_height;
    return true;
}

void renderer::release_bloom_targets()
{
    if (m_bloom_dsv)
    {
        m_bloom_dsv->Release();
        m_bloom_dsv = nullptr;
    }
    if (m_bloom_depth)
    {
        m_bloom_depth->Release();
        m_bloom_depth = nullptr;
    }
    if (m_bloom_a_srv)
    {
        m_bloom_a_srv->Release();
        m_bloom_a_srv = nullptr;
    }
    if (m_bloom_a_rtv)
    {
        m_bloom_a_rtv->Release();
        m_bloom_a_rtv = nullptr;
    }
    if (m_bloom_a)
    {
        m_bloom_a->Release();
        m_bloom_a = nullptr;
    }
    if (m_bloom_b_srv)
    {
        m_bloom_b_srv->Release();
        m_bloom_b_srv = nullptr;
    }
    if (m_bloom_b_rtv)
    {
        m_bloom_b_rtv->Release();
        m_bloom_b_rtv = nullptr;
    }
    if (m_bloom_b)
    {
        m_bloom_b->Release();
        m_bloom_b = nullptr;
    }
    if (m_bloom_source_srv)
    {
        m_bloom_source_srv->Release();
        m_bloom_source_srv = nullptr;
    }
    if (m_bloom_source_rtv)
    {
        m_bloom_source_rtv->Release();
        m_bloom_source_rtv = nullptr;
    }
    if (m_bloom_source)
    {
        m_bloom_source->Release();
        m_bloom_source = nullptr;
    }
    if (m_bloom_inner_srv)
    {
        m_bloom_inner_srv->Release();
        m_bloom_inner_srv = nullptr;
    }
    if (m_bloom_inner_rtv)
    {
        m_bloom_inner_rtv->Release();
        m_bloom_inner_rtv = nullptr;
    }
    if (m_bloom_inner)
    {
        m_bloom_inner->Release();
        m_bloom_inner = nullptr;
    }
    m_bloom_width = m_bloom_height = 0;
}

void renderer::begin_2d_bloom_frame(const bool enabled)
{
    m_bloom_2d_enabled = enabled && m_ready;
    m_bloom_2d_vertices.clear();
    m_bloom_2d_radius = 0.0f;
}

void renderer::add_2d_bloom_segment(const float x0, const float y0, const float x1, const float y1,
                                    const float thickness, const float radius, const render::draw::rgba color)
{
    if (!m_bloom_2d_enabled || color.a == 0 || radius <= 0.0f)
        return;
    const auto dx = x1 - x0;
    const auto dy = y1 - y0;
    const auto length = std::sqrt(dx * dx + dy * dy);
    if (!std::isfinite(length) || length < 0.001f)
        return;
    const auto ux = dx / length;
    const auto uy = dy / length;
    const auto half = std::max(thickness, 0.5f) * 0.5f;
    const auto inner_nx = -uy * half;
    const auto inner_ny = ux * half;
    const std::array<std::array<float, 2>, 4> inner{
        std::array{x0 + inner_nx, y0 + inner_ny}, std::array{x1 + inner_nx, y1 + inner_ny},
        std::array{x1 - inner_nx, y1 - inner_ny}, std::array{x0 - inner_nx, y0 - inner_ny}};
    const float rgba[4]{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
    const auto push = [&](const std::array<float, 2>& point, const float alpha)
    {
        bloom_2d_vertex vertex{};
        vertex.position[0] = point[0];
        vertex.position[1] = point[1];
        std::memcpy(vertex.color, rgba, sizeof(rgba));
        vertex.color[3] *= alpha;
        m_bloom_2d_vertices.push_back(vertex);
    };
    push(inner[0], 1.0f);
    push(inner[1], 1.0f);
    push(inner[2], 1.0f);
    push(inner[0], 1.0f);
    push(inner[2], 1.0f);
    push(inner[3], 1.0f);
    // The source contains only the real element. Radius is produced exclusively
    // by the Gaussian pass, not by stacked/feathered copies of its geometry.
    m_bloom_2d_radius = std::max(m_bloom_2d_radius, radius);
}

void renderer::add_2d_bloom_triangle(const float x0, const float y0, const float x1, const float y1, const float x2,
                                     const float y2, const float radius, const render::draw::rgba color)
{
    if (!m_bloom_2d_enabled || color.a == 0 || radius <= 0.0f || !std::isfinite(x0) || !std::isfinite(y0) ||
        !std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2) || !std::isfinite(y2))
        return;

    const float rgba[4]{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
    const auto push = [&](const float x, const float y)
    {
        bloom_2d_vertex vertex{};
        vertex.position[0] = x;
        vertex.position[1] = y;
        std::memcpy(vertex.color, rgba, sizeof(rgba));
        m_bloom_2d_vertices.push_back(vertex);
    };

    push(x0, y0);
    push(x1, y1);
    push(x2, y2);
    m_bloom_2d_radius = std::max(m_bloom_2d_radius, radius);
}

void renderer::render_2d_bloom(ID3D11RenderTargetView* backbuffer_rtv, const UINT target_width,
                               const UINT target_height)
{
    VESTA_PERF_SCOPE(chams_world_effects);
    if (!m_bloom_2d_enabled || !m_ready || !backbuffer_rtv || m_bloom_2d_vertices.empty() ||
        !ensure_bloom_targets(target_width, target_height))
        return;

    const auto required = m_bloom_2d_vertices.size();
    if (!m_bloom_2d_vertex_buffer || required > m_bloom_2d_vertex_capacity)
    {
        if (m_bloom_2d_vertex_buffer)
        {
            m_bloom_2d_vertex_buffer->Release();
            m_bloom_2d_vertex_buffer = nullptr;
        }
        m_bloom_2d_vertex_capacity = std::bit_ceil(std::max<std::size_t>(required, 256));
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(m_bloom_2d_vertex_capacity * sizeof(bloom_2d_vertex));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_bloom_2d_vertex_buffer)))
            return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_bloom_2d_vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    std::memcpy(mapped.pData, m_bloom_2d_vertices.data(), required * sizeof(bloom_2d_vertex));
    m_context->Unmap(m_bloom_2d_vertex_buffer, 0);

    const auto set_constants = [&](const float x, const float y, const float strength)
    {
        D3D11_MAPPED_SUBRESOURCE constants_mapped{};
        if (SUCCEEDED(m_context->Map(m_cb_bloom, 0, D3D11_MAP_WRITE_DISCARD, 0, &constants_mapped)))
        {
            auto* value = static_cast<cb_bloom*>(constants_mapped.pData);
            value->texel[0] = 1.0f / static_cast<float>(m_bloom_width);
            value->texel[1] = 1.0f / static_cast<float>(m_bloom_height);
            value->direction[0] = x;
            value->direction[1] = y;
            value->strength = strength;
            value->target_size[0] = static_cast<float>(target_width);
            value->target_size[1] = static_cast<float>(target_height);
            value->padding = 0.0f;
            m_context->Unmap(m_cb_bloom, 0);
        }
    };
    constexpr float transparent[4]{};
    m_context->ClearRenderTargetView(m_bloom_source_rtv, transparent);
    D3D11_VIEWPORT bloom_viewport{0.0f, 0.0f, static_cast<float>(m_bloom_width), static_cast<float>(m_bloom_height),
                                  0.0f, 1.0f};
    m_context->RSSetViewports(1, &bloom_viewport);
    m_context->OMSetRenderTargets(1, &m_bloom_source_rtv, nullptr);
    m_context->OMSetDepthStencilState(m_depth_state_disabled, 0);
    const float blend_factor[4]{};

    m_context->OMSetBlendState(m_blend_disabled, blend_factor, 0xffffffff);
    m_context->RSSetState(m_rs_solid);
    m_context->IASetInputLayout(m_bloom_2d_input_layout);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const UINT stride = sizeof(bloom_2d_vertex);
    const UINT offset{};
    m_context->IASetVertexBuffers(0, 1, &m_bloom_2d_vertex_buffer, &stride, &offset);
    m_context->VSSetShader(m_bloom_2d_vertex_shader, nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, &m_cb_bloom);
    m_context->GSSetShader(nullptr, nullptr, 0);
    m_context->PSSetShader(m_bloom_2d_pixel_shader, nullptr, 0);
    set_constants(0.0f, 0.0f, 1.0f);
    m_context->Draw(static_cast<UINT>(required), 0);

    m_context->IASetInputLayout(nullptr);
    m_context->VSSetShader(m_bloom_vertex_shader, nullptr, 0);
    m_context->PSSetShader(m_bloom_blur_shader, nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, &m_cb_bloom);
    m_context->PSSetSamplers(0, 1, &m_bloom_sampler);
    m_context->OMSetBlendState(m_blend_disabled, blend_factor, 0xffffffff);
    ID3D11ShaderResourceView* null_srv{};
    const auto blur_lobe = [&](const float spread)
    {
        set_constants(spread, 0.0f, 1.0f);
        m_context->OMSetRenderTargets(1, &m_bloom_b_rtv, nullptr);
        m_context->PSSetShaderResources(0, 1, &m_bloom_source_srv);
        m_context->Draw(3, 0);
        m_context->PSSetShaderResources(0, 1, &null_srv);
        set_constants(0.0f, spread, 1.0f);
        m_context->OMSetRenderTargets(1, &m_bloom_a_rtv, nullptr);
        m_context->PSSetShaderResources(0, 1, &m_bloom_b_srv);
        m_context->Draw(3, 0);
        m_context->PSSetShaderResources(0, 1, &null_srv);
    };
    blur_lobe(std::max(0.65f, m_bloom_2d_radius * 0.28f));
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_context->CopyResource(m_bloom_inner, m_bloom_a);
    blur_lobe(std::max(1.4f, m_bloom_2d_radius * 0.96f));

    D3D11_VIEWPORT full_viewport{0.0f, 0.0f, static_cast<float>(target_width), static_cast<float>(target_height),
                                 0.0f, 1.0f};
    m_context->RSSetViewports(1, &full_viewport);
    m_context->OMSetRenderTargets(1, &backbuffer_rtv, nullptr);
    m_context->PSSetShader(m_bloom_composite_shader, nullptr, 0);
    set_constants(0.0f, 0.0f, 1.0f);
    ID3D11ShaderResourceView* bloom_srvs[]{m_bloom_a_srv, m_bloom_source_srv, m_bloom_inner_srv};
    m_context->PSSetShaderResources(0, 3, bloom_srvs);
    m_context->OMSetBlendState(m_bloom_blend_state, blend_factor, 0xffffffff);
    m_context->Draw(3, 0);
    ID3D11ShaderResourceView* null_bloom_srvs[3]{};
    m_context->PSSetShaderResources(0, 3, null_bloom_srvs);
}

const renderer::gpu_mesh* renderer::get_or_upload(const std::string& model_path, const skinned_mesh& mesh)
{
    if (const auto it = m_gpu_meshes.find(model_path); it != m_gpu_meshes.end())
    {
        return &it->second;
    }

    gpu_mesh gm{};
    gm.vertex_count = static_cast<std::uint32_t>(mesh.vertices.size());
    gm.index_count = static_cast<std::uint32_t>(mesh.indices.size());

    D3D11_BUFFER_DESC vb_desc{};
    vb_desc.ByteWidth = static_cast<UINT>(mesh.vertices.size() * sizeof(skinned_vertex));
    vb_desc.Usage = D3D11_USAGE_IMMUTABLE;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vb_data{mesh.vertices.data()};
    if (FAILED(m_device->CreateBuffer(&vb_desc, &vb_data, &gm.vertex_buffer)))
    {
        return nullptr;
    }

    D3D11_BUFFER_DESC ib_desc{};
    ib_desc.ByteWidth = static_cast<UINT>(mesh.indices.size() * sizeof(std::uint32_t));
    ib_desc.Usage = D3D11_USAGE_IMMUTABLE;
    ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ib_data{mesh.indices.data()};
    if (FAILED(m_device->CreateBuffer(&ib_desc, &ib_data, &gm.index_buffer)))
    {
        gm.vertex_buffer->Release();
        return nullptr;
    }

    return &m_gpu_meshes.emplace(model_path, gm).first->second;
}

void renderer::update_view_projection(const foundation::matrix4& matrix, const foundation::vec3& eye)
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_cb_view_projection, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        return;
    }

    auto* data = static_cast<cb_view_projection*>(mapped.pData);
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
            data->matrix[r][c] = matrix[r][c];
    }

    // The light lives at the eye, so the view origin doubles as the light
    // position and every enemy stays lit from the front wherever they stand.
    data->eye[0] = eye.x;
    data->eye[1] = eye.y;
    data->eye[2] = eye.z;

    // Wrapped to an hour: a raw epoch count loses sub-second resolution in a
    // float, which would make the animated materials stutter then freeze.
    data->time = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count() %
                                    3600000) *
                 0.001f;

    m_context->Unmap(m_cb_view_projection, 0);
}

void renderer::update_bones(const std::vector<bone_matrix>& skin_matrices)
{
    update_bones(m_cb_bones, skin_matrices);
}

void renderer::update_bones(ID3D11Buffer* buffer, const std::vector<bone_matrix>& skin_matrices)
{
    if (!buffer)
    {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        return;
    }

    auto* data = static_cast<cb_bones*>(mapped.pData);
    const auto count = std::min<std::size_t>(skin_matrices.size(), k_max_bones);
    for (std::size_t i = 0; i < count; ++i)
    {
        write_matrix4(data->matrix[i], skin_matrices[i]);
    }

    m_context->Unmap(buffer, 0);
}

bool renderer::ensure_frame_bone_buffers(std::size_t count)
{
    while (m_frame_bone_buffers.size() < count)
    {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.ByteWidth = sizeof(cb_bones);

        ID3D11Buffer* buffer{};
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, &buffer)))
        {
            return false;
        }
        m_frame_bone_buffers.push_back(buffer);
    }
    return true;
}

void renderer::update_material(const config::visual_profile::chams::material& material, const float shell_expand,
                               const float effect_progress, const float effect_seed)
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_cb_material, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        return;
    }

    auto* data = static_cast<cb_material*>(mapped.pData);
    write_gpu_material(data->visible, material);
    data->invisible = data->visible;
    data->split_by_world_depth = 0;
    data->visible_enabled = 1;
    data->invisible_enabled = 0;
    data->layer = 0;
    data->antialias_split = 0;
    data->shell_expand = shell_expand;
    data->effect_progress = effect_progress;
    data->effect_seed = effect_seed;

    m_context->Unmap(m_cb_material, 0);
}

void renderer::update_material_pair(const config::visual_profile::chams::material& visible,
                                    const config::visual_profile::chams::material& invisible, int layer,
                                    const float shell_expand)
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_cb_material, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        return;
    }

    auto* data = static_cast<cb_material*>(mapped.pData);
    write_gpu_material(data->visible, visible);
    write_gpu_material(data->invisible, invisible);
    data->split_by_world_depth = 1;
    data->visible_enabled = visible.enabled ? 1 : 0;
    data->invisible_enabled = invisible.enabled ? 1 : 0;
    data->layer = layer;
    data->antialias_split = config::visual_settings.m_chams.antialiasing ? 1 : 0;
    data->shell_expand = shell_expand;
    data->effect_progress = 0.0f;
    data->effect_seed = 0.0f;
    m_context->Unmap(m_cb_material, 0);
}

void renderer::draw_external(const std::string& model_path, const skinned_mesh& mesh,
                             const config::visual_profile::chams::material& material,
                             const std::vector<bone_matrix>& skin_matrices, const float view_projection[4][4],
                             const foundation::vec3& eye)
{
    if (!m_ready || !material.enabled)
    {
        return;
    }

    const auto* gpu = get_or_upload(model_path, mesh);
    if (!gpu)
    {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(m_context->Map(m_cb_view_projection, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        auto* data = static_cast<cb_view_projection*>(mapped.pData);
        std::memcpy(data->matrix, view_projection, sizeof(data->matrix));
        data->eye[0] = eye.x;
        data->eye[1] = eye.y;
        data->eye[2] = eye.z;
        data->time = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now().time_since_epoch())
                                            .count() %
                                        3600000) *
                     0.001f;
        m_context->Unmap(m_cb_view_projection, 0);
    }

    // The caller supplies the pose, so chams lands on the same skeleton the
    // model was drawn with instead of snapping back to the bind pose.
    update_bones(skin_matrices);
    update_material(material);

    m_context->IASetInputLayout(m_input_layout);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vertex_shader, nullptr, 0);
    m_context->PSSetShader(m_pixel_shader, nullptr, 0);

    ID3D11Buffer* vs_cbs[]{m_cb_view_projection, m_cb_bones, m_cb_material};
    m_context->VSSetConstantBuffers(0, 3, vs_cbs);
    m_context->PSSetConstantBuffers(0, 1, &m_cb_view_projection);
    m_context->PSSetConstantBuffers(2, 1, &m_cb_material);

    const float blend_factor[4]{};
    m_context->OMSetBlendState(m_blend_state, blend_factor, 0xFFFFFFFF);

    m_context->OMSetDepthStencilState(m_depth_state_equal, 0);
    m_context->RSSetState(material.wireframe ? m_rs_wireframe : m_rs_solid);

    const UINT stride = sizeof(skinned_vertex);
    const UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, &gpu->vertex_buffer, &stride, &offset);
    m_context->IASetIndexBuffer(gpu->index_buffer, DXGI_FORMAT_R32_UINT, 0);

    m_context->DrawIndexed(gpu->index_count, 0, 0);
}

namespace
{
// The sun of the No Flash / No Smoke view: high (about 58 degrees) and from one side, so walls and floors read apart.
constexpr foundation::vec3 k_world_sun{0.434f, 0.304f, 0.848f};
constexpr UINT k_shadow_map_size = 4096;
} // namespace

void renderer::release_world_shadow_map()
{
    if (m_shadow_srv)
        m_shadow_srv->Release();
    if (m_shadow_dsv)
        m_shadow_dsv->Release();
    if (m_shadow_texture)
        m_shadow_texture->Release();
    m_shadow_srv = nullptr;
    m_shadow_dsv = nullptr;
    m_shadow_texture = nullptr;
    m_shadow_revision = ~0ull;
}

bool renderer::ensure_world_shadow_map()
{
    if (!m_static_world.vertex_buffer || !m_static_world.index_buffer || m_static_world.chunks.empty())
        return false;
    if (m_shadow_srv && m_shadow_revision == m_static_world.revision)
        return true;

    if (!m_shadow_texture)
    {
        // 16-bit depth: 32 MB for a 4096 map, enough precision over a map's height.
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = k_shadow_map_size;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16_TYPELESS;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D16_UNORM;
        dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R16_UNORM;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_shadow_texture)) ||
            FAILED(m_device->CreateDepthStencilView(m_shadow_texture, &dsv, &m_shadow_dsv)) ||
            FAILED(m_device->CreateShaderResourceView(m_shadow_texture, &srv, &m_shadow_srv)))
        {
            release_world_shadow_map();
            return false;
        }
    }

    // An orthographic box around the whole static map, seen along the sun.
    foundation::vec3 mins{1e30f, 1e30f, 1e30f};
    foundation::vec3 maxs{-1e30f, -1e30f, -1e30f};
    for (const auto& chunk : m_static_world.chunks)
    {
        mins = {std::min(mins.x, chunk.bounds.mins.x), std::min(mins.y, chunk.bounds.mins.y),
                std::min(mins.z, chunk.bounds.mins.z)};
        maxs = {std::max(maxs.x, chunk.bounds.maxs.x), std::max(maxs.y, chunk.bounds.maxs.y),
                std::max(maxs.z, chunk.bounds.maxs.z)};
    }
    const auto forward = k_world_sun * -1.0f;
    auto right = forward.cross(foundation::vec3{0.0f, 0.0f, 1.0f});
    right.normalize();
    const auto up = right.cross(forward);
    float low[3]{1e30f, 1e30f, 1e30f};
    float high[3]{-1e30f, -1e30f, -1e30f};
    for (int corner = 0; corner < 8; ++corner)
    {
        const foundation::vec3 point{(corner & 1) ? maxs.x : mins.x, (corner & 2) ? maxs.y : mins.y,
                                     (corner & 4) ? maxs.z : mins.z};
        const float projected[3]{point.dot(right), point.dot(up), point.dot(forward)};
        for (int axis = 0; axis < 3; ++axis)
        {
            low[axis] = std::min(low[axis], projected[axis]);
            high[axis] = std::max(high[axis], projected[axis]);
        }
    }
    const float half_x = std::max((high[0] - low[0]) * 0.5f, 1.0f);
    const float half_y = std::max((high[1] - low[1]) * 0.5f, 1.0f);
    const float center_x = (high[0] + low[0]) * 0.5f;
    const float center_y = (high[1] + low[1]) * 0.5f;
    const float depth = std::max(high[2] - low[2] + 2.0f, 1.0f);
    const float near_plane = low[2] - 1.0f;
    const float rows[3][4]{{right.x / half_x, right.y / half_x, right.z / half_x, -center_x / half_x},
                           {up.x / half_y, up.y / half_y, up.z / half_y, -center_y / half_y},
                           {forward.x / depth, forward.y / depth, forward.z / depth, -near_plane / depth}};
    foundation::matrix4 light{};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column)
        {
            light[row][column] = rows[row][column];
            m_shadow_matrix[row][column] = rows[row][column];
        }
    light[3][0] = light[3][1] = light[3][2] = 0.0f;
    light[3][3] = 1.0f;
    m_shadow_matrix[3][0] = m_shadow_matrix[3][1] = m_shadow_matrix[3][2] = 0.0f;
    m_shadow_matrix[3][3] = 1.0f;

    update_view_projection(light, k_world_sun);
    ID3D11ShaderResourceView* null_srv{};
    m_context->PSSetShaderResources(0, 1, &null_srv);
    m_context->OMSetRenderTargets(0, nullptr, m_shadow_dsv);
    m_context->ClearDepthStencilView(m_shadow_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    D3D11_VIEWPORT viewport{};
    viewport.Width = viewport.Height = static_cast<float>(k_shadow_map_size);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    m_context->IASetInputLayout(m_world_input_layout);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_world_vertex_shader, nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, &m_cb_view_projection);
    m_context->GSSetShader(nullptr, nullptr, 0);
    m_context->PSSetShader(nullptr, nullptr, 0);
    m_context->RSSetState(m_rs_shadow);
    m_context->OMSetDepthStencilState(m_world_depth_state, 0);
    const UINT stride = sizeof(foundation::vec3);
    const UINT offset{};
    m_context->IASetVertexBuffers(0, 1, &m_static_world.vertex_buffer, &stride, &offset);
    m_context->IASetIndexBuffer(m_static_world.index_buffer, DXGI_FORMAT_R32_UINT, 0);
    m_context->DrawIndexed(m_static_world.index_count, 0, 0);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_shadow_revision = m_static_world.revision;
    return true;
}

void renderer::render_world_effects(ID3D11RenderTargetView* backbuffer_rtv, const UINT target_width,
                                    const UINT target_height)
{
    if (!m_ready || !backbuffer_rtv || target_width == 0 || target_height == 0 || !game::local_player().valid())
        return;

    const auto& flash_cfg = config::visual_settings.m_no_flash;
    const auto& smoke_cfg = config::visual_settings.m_no_smoke;
    const auto raw_flash = game::local_player().flash_alpha();
    const auto blindness = std::clamp(raw_flash > 1.5f ? raw_flash / 255.0f : raw_flash, 0.0f, 1.0f);
    const bool dim_flash = flash_cfg.enabled && blindness >= 0.01f;
    const bool draw_flash_world = flash_cfg.enabled && blindness >= 0.5f;

    using smoke_sphere = frame_cache::smoke_sphere;
    auto& smoke_spheres = m_frame_cache->smoke_spheres;
    smoke_spheres.clear();
    const auto eye = game::camera().origin();
    if (smoke_cfg.enabled)
    {
        if (const auto projectiles = game::world().projectiles())
        {
            for (const auto& projectile : *projectiles)
            {
                if (projectile.subtype != game::projectile_kind::smoke_grenade || !projectile.smoke_active)
                    continue;
                // The same smoke zone the aim and visibility checks use.
                const auto zone = game::smoke_zone_of(projectile);
                if (!std::isfinite(zone.center.x) || !std::isfinite(zone.center.y) || !std::isfinite(zone.center.z))
                    continue;
                smoke_spheres.push_back({zone.center, zone.radius});
            }
        }
        std::ranges::sort(smoke_spheres, [&](const smoke_sphere& lhs, const smoke_sphere& rhs)
                          { return eye.distance_sqr(lhs.center) < eye.distance_sqr(rhs.center); });
        if (smoke_spheres.size() > 8)
            smoke_spheres.resize(8);
    }

    if (!dim_flash && !draw_flash_world && smoke_spheres.empty())
        return;

    if (dim_flash)
    {
        const auto alpha = blindness * (flash_cfg.background_color.a / 255.0f);
        const float background[4]{(flash_cfg.background_color.r / 255.0f) * alpha,
                                  (flash_cfg.background_color.g / 255.0f) * alpha,
                                  (flash_cfg.background_color.b / 255.0f) * alpha, alpha};
        m_context->ClearRenderTargetView(backbuffer_rtv, background);
    }
    if (!draw_flash_world && smoke_spheres.empty())
        return;
    if (!ensure_world_geometry())
        return;

    // Flashed: the map around the eye. Otherwise: the surfaces the smokes hide (the view to them passes through one).
    const bool clip_to_smoke = !draw_flash_world;
    const auto style = std::clamp(clip_to_smoke ? smoke_cfg.style : flash_cfg.style, 0,
                                  static_cast<int>(config::visual_profile::world_style_count) - 1);
    const auto max_distance = std::max(clip_to_smoke ? smoke_cfg.max_distance : flash_cfg.max_distance, 1.0f);
    const bool shadows = (clip_to_smoke ? smoke_cfg.shadows : flash_cfg.shadows) &&
                         style != config::visual_profile::world_lines && ensure_world_shadow_map();

    const auto view_projection = game::camera().matrix();
    const auto max_distance_sqr = max_distance * max_distance;

    // Inside a smoke every view crosses it, so the whole screen is affected; otherwise only the smokes' screen area.
    bool eye_in_smoke{};
    for (const auto& sphere : smoke_spheres)
        eye_in_smoke |=
            game::smoke_zone{sphere.center, sphere.radius * 1.1f, game::k_smoke_zone_height * 1.1f}.contains(eye);
    D3D11_RECT effect_scissor{0, 0, static_cast<LONG>(target_width), static_cast<LONG>(target_height)};
    bool smoke_scissor = clip_to_smoke && !eye_in_smoke;
    if (smoke_scissor)
    {
        float min_x = static_cast<float>(target_width);
        float min_y = static_cast<float>(target_height);
        float max_x{};
        float max_y{};
        bool valid{};
        bool crosses_near{};
        for (const auto& sphere : smoke_spheres)
        {
            const auto reach = sphere.radius * 1.1f; // the soft rim of the shader
            const auto rise = game::k_smoke_zone_height * 1.1f;
            for (int corner = 0; corner < 8; ++corner)
            {
                const foundation::vec3 point{sphere.center.x + ((corner & 1) ? reach : -reach),
                                             sphere.center.y + ((corner & 2) ? reach : -reach),
                                             sphere.center.z + ((corner & 4) ? rise : -rise)};
                const auto x = view_projection[0][0] * point.x + view_projection[0][1] * point.y +
                               view_projection[0][2] * point.z + view_projection[0][3];
                const auto y = view_projection[1][0] * point.x + view_projection[1][1] * point.y +
                               view_projection[1][2] * point.z + view_projection[1][3];
                const auto w = view_projection[3][0] * point.x + view_projection[3][1] * point.y +
                               view_projection[3][2] * point.z + view_projection[3][3];
                if (w <= 0.001f)
                {
                    crosses_near = true;
                    continue;
                }
                const auto screen_x = (x / w * 0.5f + 0.5f) * target_width;
                const auto screen_y = (0.5f - y / w * 0.5f) * target_height;
                min_x = std::min(min_x, screen_x);
                min_y = std::min(min_y, screen_y);
                max_x = std::max(max_x, screen_x);
                max_y = std::max(max_y, screen_y);
                valid = true;
            }
        }
        if (!valid)
            return; // every smoke is behind the eye
        if (crosses_near)
            smoke_scissor = false;
        else
        {
            effect_scissor.left = std::clamp<LONG>(static_cast<LONG>(std::floor(min_x)) - 2, 0, target_width);
            effect_scissor.top = std::clamp<LONG>(static_cast<LONG>(std::floor(min_y)) - 2, 0, target_height);
            effect_scissor.right = std::clamp<LONG>(static_cast<LONG>(std::ceil(max_x)) + 2, 0, target_width);
            effect_scissor.bottom = std::clamp<LONG>(static_cast<LONG>(std::ceil(max_y)) + 2, 0, target_height);
            if (effect_scissor.left >= effect_scissor.right || effect_scissor.top >= effect_scissor.bottom)
                return;
        }
    }

    const auto aabb_distance_sqr = [&](const world_bounds& bounds)
    {
        float result{};
        for (int axis = 0; axis < 3; ++axis)
        {
            const float value = axis == 0 ? eye.x : axis == 1 ? eye.y : eye.z;
            const float low = axis == 0 ? bounds.mins.x : axis == 1 ? bounds.mins.y : bounds.mins.z;
            const float high = axis == 0 ? bounds.maxs.x : axis == 1 ? bounds.maxs.y : bounds.maxs.z;
            const float delta = value < low ? low - value : value > high ? value - high : 0.0f;
            result += delta * delta;
        }
        return result;
    };
    // In the frustum, and (with a smoke scissor) on screen inside it: a chunk far behind a smoke still counts.
    const auto visible_on_screen = [&](const world_bounds& bounds)
    {
        bool left{true}, right{true}, bottom{true}, top{true};
        bool near_plane{true}, far_plane{true};
        bool behind{};
        float min_x{1e30f}, min_y{1e30f}, max_x{-1e30f}, max_y{-1e30f};
        for (int corner = 0; corner < 8; ++corner)
        {
            const foundation::vec3 point{(corner & 1) ? bounds.maxs.x : bounds.mins.x,
                                         (corner & 2) ? bounds.maxs.y : bounds.mins.y,
                                         (corner & 4) ? bounds.maxs.z : bounds.mins.z};
            const auto x = view_projection[0][0] * point.x + view_projection[0][1] * point.y +
                           view_projection[0][2] * point.z + view_projection[0][3];
            const auto y = view_projection[1][0] * point.x + view_projection[1][1] * point.y +
                           view_projection[1][2] * point.z + view_projection[1][3];
            const auto z = view_projection[2][0] * point.x + view_projection[2][1] * point.y +
                           view_projection[2][2] * point.z + view_projection[2][3];
            const auto w = view_projection[3][0] * point.x + view_projection[3][1] * point.y +
                           view_projection[3][2] * point.z + view_projection[3][3];
            left &= x < -w;
            right &= x > w;
            bottom &= y < -w;
            top &= y > w;
            near_plane &= z < 0.0f;
            far_plane &= z > w;
            if (w <= 0.001f)
            {
                behind = true;
                continue;
            }
            const auto screen_x = (x / w * 0.5f + 0.5f) * target_width;
            const auto screen_y = (0.5f - y / w * 0.5f) * target_height;
            min_x = std::min(min_x, screen_x);
            min_y = std::min(min_y, screen_y);
            max_x = std::max(max_x, screen_x);
            max_y = std::max(max_y, screen_y);
        }
        if (left || right || bottom || top || near_plane || far_plane)
            return false;
        if (!smoke_scissor || behind)
            return true;
        return max_x >= static_cast<float>(effect_scissor.left) && min_x <= static_cast<float>(effect_scissor.right) &&
               max_y >= static_cast<float>(effect_scissor.top) && min_y <= static_cast<float>(effect_scissor.bottom);
    };

    using draw_range = frame_cache::draw_range;
    auto& static_chunks = m_frame_cache->static_chunks;
    auto& dynamic_chunks = m_frame_cache->dynamic_chunks;
    const auto select_chunks = [&](const world_geometry& geometry, std::vector<draw_range>& selected)
    {
        selected.clear();
        if (selected.capacity() < geometry.chunks.size())
            selected.reserve(geometry.chunks.size());
        for (const auto& chunk : geometry.chunks)
        {
            if (aabb_distance_sqr(chunk.bounds) > max_distance_sqr || !visible_on_screen(chunk.bounds))
                continue;
            if (!selected.empty() && selected.back().first_index + selected.back().index_count == chunk.first_index)
                selected.back().index_count += chunk.index_count;
            else
                selected.push_back({chunk.first_index, chunk.index_count});
        }
    };
    select_chunks(m_static_world, static_chunks);
    select_chunks(m_dynamic_world, dynamic_chunks);
    if (static_chunks.empty() && dynamic_chunks.empty())
        return;

    const auto bind_geometry = [&](const world_geometry& geometry)
    {
        if (!geometry.vertex_buffer || !geometry.index_buffer || !geometry.index_count)
            return false;
        const UINT stride = sizeof(foundation::vec3);
        const UINT offset{};
        m_context->IASetVertexBuffers(0, 1, &geometry.vertex_buffer, &stride, &offset);
        m_context->IASetIndexBuffer(geometry.index_buffer, DXGI_FORMAT_R32_UINT, 0);
        return true;
    };
    const auto draw_selected = [&](const world_geometry& geometry, const std::vector<draw_range>& selected)
    {
        if (!bind_geometry(geometry))
            return;
        for (const auto& chunk : selected)
            m_context->DrawIndexed(chunk.index_count, chunk.first_index, 0);
    };

    // Anti-aliased through the multisampled targets when the device has them, resolved onto the overlay at the end.
    const bool use_msaa = ensure_msaa_targets(target_width, target_height);
    if (!use_msaa && !ensure_depth_buffer(target_width, target_height))
        return;
    ID3D11RenderTargetView* const color_target = use_msaa ? m_msaa_rtv : backbuffer_rtv;
    ID3D11DepthStencilView* const depth_target = use_msaa ? m_msaa_dsv : m_dsv;
    if (use_msaa)
    {
        constexpr float transparent[4]{};
        m_context->ClearRenderTargetView(m_msaa_rtv, transparent);
    }
    m_context->ClearDepthStencilView(depth_target, D3D11_CLEAR_DEPTH, 1.0f, 0);
    update_view_projection(view_projection, eye);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(target_width);
    viewport.Height = static_cast<float>(target_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    m_context->IASetInputLayout(m_world_input_layout);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_world_vertex_shader, nullptr, 0);
    m_context->GSSetShader(nullptr, nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, &m_cb_view_projection);
    m_context->RSSetState(m_rs_world_scissor);
    m_context->RSSetScissorRects(1, &effect_scissor);

    // Depth of the nearest surface first, then the colour pass shades only that surface (no layers blended twice).
    m_context->PSSetShader(nullptr, nullptr, 0);
    m_context->OMSetRenderTargets(0, nullptr, depth_target);
    m_context->OMSetDepthStencilState(m_world_depth_state, 0);
    draw_selected(m_static_world, static_chunks);
    draw_selected(m_dynamic_world, dynamic_chunks);

    const auto& surface = clip_to_smoke ? smoke_cfg.surface_color : flash_cfg.surface_color;
    const auto& line = clip_to_smoke ? smoke_cfg.wireframe_color : flash_cfg.wireframe_color;
    cb_world_effect constants{};
    constants.surface_color[0] = surface.r / 255.0f;
    constants.surface_color[1] = surface.g / 255.0f;
    constants.surface_color[2] = surface.b / 255.0f;
    constants.surface_color[3] = surface.a / 255.0f;
    constants.line_color[0] = line.r / 255.0f;
    constants.line_color[1] = line.g / 255.0f;
    constants.line_color[2] = line.b / 255.0f;
    constants.line_color[3] = line.a / 255.0f;
    constants.eye_and_distance[0] = eye.x;
    constants.eye_and_distance[1] = eye.y;
    constants.eye_and_distance[2] = eye.z;
    constants.eye_and_distance[3] = max_distance;
    constants.smoke_count = clip_to_smoke ? static_cast<std::uint32_t>(smoke_spheres.size()) : 0u;
    constants.clip_to_smoke = clip_to_smoke ? 1u : 0u;
    constants.style = static_cast<std::uint32_t>(style);
    constants.shadows = shadows ? 1u : 0u;
    for (std::size_t i = 0; i < smoke_spheres.size(); ++i)
    {
        constants.smoke_spheres[i][0] = smoke_spheres[i].center.x;
        constants.smoke_spheres[i][1] = smoke_spheres[i].center.y;
        constants.smoke_spheres[i][2] = smoke_spheres[i].center.z;
        constants.smoke_spheres[i][3] = smoke_spheres[i].radius;
    }
    constants.sun_direction[0] = k_world_sun.x;
    constants.sun_direction[1] = k_world_sun.y;
    constants.sun_direction[2] = k_world_sun.z;
    constants.sun_direction[3] = 1.0f / static_cast<float>(k_shadow_map_size);
    constants.line_params[0] = std::clamp(clip_to_smoke ? smoke_cfg.line_width : flash_cfg.line_width, 0.5f, 4.0f);
    constants.line_params[1] = clip_to_smoke ? 1.0f : blindness;
    constants.line_params[2] = game::k_smoke_zone_radius / game::k_smoke_zone_height;
    std::memcpy(constants.light_view_projection, m_shadow_matrix, sizeof(m_shadow_matrix));
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_cb_world_effect, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    m_context->Unmap(m_cb_world_effect, 0);

    m_context->OMSetRenderTargets(1, &color_target, depth_target);
    m_context->GSSetShader(m_world_geometry_shader, nullptr, 0);
    m_context->PSSetShader(m_world_pixel_shader, nullptr, 0);
    m_context->PSSetConstantBuffers(1, 1, &m_cb_world_effect);
    ID3D11ShaderResourceView* shadow_view = shadows ? m_shadow_srv : nullptr;
    m_context->PSSetShaderResources(0, 1, &shadow_view);
    m_context->PSSetSamplers(0, 1, &m_shadow_sampler);
    m_context->OMSetDepthStencilState(m_depth_state_read_only, 0);
    const float blend_factor[4]{};
    m_context->OMSetBlendState(m_blend_state, blend_factor, 0xFFFFFFFF);
    draw_selected(m_static_world, static_chunks);
    draw_selected(m_dynamic_world, dynamic_chunks);

    m_context->GSSetShader(nullptr, nullptr, 0);
    ID3D11ShaderResourceView* null_srv{};
    m_context->PSSetShaderResources(0, 1, &null_srv);
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    if (use_msaa)
    {
        const auto ndc_x = [&](const LONG pixel) { return static_cast<float>(pixel) / target_width * 2.0f - 1.0f; };
        const auto ndc_y = [&](const LONG pixel) { return 1.0f - static_cast<float>(pixel) / target_height * 2.0f; };
        std::vector<screen_volume> volumes{{ndc_x(effect_scissor.left), ndc_y(effect_scissor.bottom),
                                            ndc_x(effect_scissor.right), ndc_y(effect_scissor.top), 1.0f}};
        resolve_msaa(backbuffer_rtv, volumes);
    }
}

void renderer::render_frame(ID3D11RenderTargetView* backbuffer_rtv, const UINT target_width, const UINT target_height,
                            const std::shared_ptr<const game::player_pose_frame>& frame)
{
    const auto& cfg = config::visual_settings.m_chams;

    m_diag.players_seen = 0;
    m_diag.players_enemy = 0;
    m_diag.players_model_resolved = 0;
    m_diag.players_mesh_valid = 0;
    m_diag.players_drawn = 0;

    if (!backbuffer_rtv || !frame || !frame->world || !m_ready || !config::visual_settings.m_player.active() ||
        !cfg.enabled || !game::local_player().valid())
    {
        return;
    }
    if (config::visual_settings.m_player.spectator_sync && game::world().local_spectated())
        return;
    if (!ensure_vpk())
    {
        return;
    }

    if (!cfg.visible.enabled && !cfg.invisible.enabled)
    {
        return;
    }

    if (!ensure_depth_buffer(target_width, target_height))
    {
        return;
    }

    using drawable = frame_cache::drawable;
    using drawable_identity = frame_cache::drawable_identity;
    auto& cache = *m_frame_cache;
    auto& drawables = cache.drawables;
    auto& skin_scratch = cache.skin_scratch;
    auto& current_frame = cache.current_frame;
    auto& roster_world = cache.roster_world;
    auto& roster_identity = cache.roster_identity;
    auto& cached_counts = cache.counts;
    auto& cached_model_path = cache.model_path;

    bool rebuild_drawables{};
    if (frame->world != roster_world)
    {
        roster_world = frame->world;
        auto& next_identity = cache.next_identity;
        next_identity.clear();
        if (next_identity.capacity() < frame->players.size())
            next_identity.reserve(frame->players.size());
        for (const auto& pose : frame->players)
        {
            if (pose.source_index >= frame->world->size())
                continue;
            const auto& player = (*frame->world)[pose.source_index];
            if (player.health <= 0 || !pose.bones.is_valid() || !game::local_player().is_enemy(player.team) ||
                !player.legit_visible || player.model_path.empty())
            {
                continue;
            }
            next_identity.push_back({player.pawn, player.bone_cache, player.model_path});
        }
        const auto identity_less = [](const drawable_identity& left, const drawable_identity& right)
        {
            if (left.pawn != right.pawn)
                return left.pawn < right.pawn;
            if (left.bone_cache != right.bone_cache)
                return left.bone_cache < right.bone_cache;
            return left.model_path < right.model_path;
        };
        std::ranges::sort(next_identity, identity_less);
        const auto same_identity = roster_identity.size() == next_identity.size() &&
                                   std::ranges::equal(roster_identity, next_identity,
                                                      [](const drawable_identity& left, const drawable_identity& right)
                                                      {
                                                          return left.pawn == right.pawn &&
                                                                 left.bone_cache == right.bone_cache &&
                                                                 left.model_path == right.model_path;
                                                      });

        rebuild_drawables = !same_identity || drawables.size() != next_identity.size();
        if (rebuild_drawables)
            roster_identity = next_identity;
    }
    if (rebuild_drawables)
    {
        drawables.clear();
        drawables.reserve(frame->players.size());
        if (skin_scratch.size() < frame->players.size())
        {
            skin_scratch.resize(frame->players.size());
        }

        m_diag.players_seen = static_cast<int>(frame->world->size());
        for (const auto& pose : frame->players)
        {
            if (pose.source_index >= frame->world->size())
                continue;
            const auto& player = (*frame->world)[pose.source_index];

            if (player.health <= 0 || !pose.bones.is_valid())
            {
                continue;
            }

            if (!game::local_player().is_enemy(player.team))
            {
                continue;
            }
            if (!player.legit_visible)
                continue;
            ++m_diag.players_enemy;

            const auto& model_path = player.model_path;
            if (model_path.empty())
            {
                continue;
            }
            ++m_diag.players_model_resolved;
            m_diag.last_model_path = model_path;

            const auto& mesh = features::chams::g_mesh_cache.get_or_build(m_vpk, model_path);
            if (!mesh.valid)
            {
                continue;
            }
            ++m_diag.players_mesh_valid;

            const auto* gpu = get_or_upload(model_path, mesh);
            if (!gpu)
            {
                continue;
            }

            drawable d{};
            d.gpu = gpu;
            d.mesh = &mesh;
            d.current = &player;
            d.bones = &pose.bones;
            auto& skin_matrices = skin_scratch[drawables.size()];
            skin_matrices.clear();
            skin_matrices.reserve(mesh.bones.size());
            d.skin_matrices = &skin_matrices;

            drawables.push_back(d);
        }

        if (!drawables.empty())
        {
            if (!ensure_frame_bone_buffers(drawables.size()))
            {
                return;
            }
            for (std::size_t i = 0; i < drawables.size(); ++i)
                drawables[i].bone_buffer = m_frame_bone_buffers[i];
        }

        cached_counts = {m_diag.players_seen, m_diag.players_enemy, m_diag.players_model_resolved,
                         m_diag.players_mesh_valid, m_diag.players_drawn};
        cached_model_path = m_diag.last_model_path;
    }
    else
    {
        m_diag.players_seen = cached_counts[0];
        m_diag.players_enemy = cached_counts[1];
        m_diag.players_model_resolved = cached_counts[2];
        m_diag.players_mesh_valid = cached_counts[3];
        m_diag.players_drawn = cached_counts[4];
        m_diag.last_model_path = cached_model_path;
    }

    if (frame != current_frame)
    {
        for (auto& d : drawables)
            d.bones = nullptr;
        for (const auto& pose : frame->players)
        {
            if (pose.source_index >= frame->world->size())
                continue;
            const auto& player = (*frame->world)[pose.source_index];
            const auto match = std::ranges::find_if(drawables,
                                                    [&](const drawable& d)
                                                    {
                                                        return d.current && d.current->pawn == pose.pawn &&
                                                               d.current->bone_cache == pose.bone_cache &&
                                                               d.current->model_path == pose.model_path;
                                                    });
            if (match == drawables.end())
                continue;
            match->current = &player;
            match->bones = &pose.bones;
        }
        std::erase_if(drawables, [](const drawable& d) { return d.bones == nullptr; });
        current_frame = frame;

        const auto pose_now = std::chrono::steady_clock::now();
        auto& current_enemy_pawns = cache.enemy_pawns;
        current_enemy_pawns.clear();
        if (current_enemy_pawns.capacity() < frame->world->size())
            current_enemy_pawns.reserve(frame->world->size());
        for (const auto& player : *frame->world)
            if (player.health > 0 && game::local_player().is_enemy(player.team))
                current_enemy_pawns.push_back(player.pawn);
        std::ranges::sort(current_enemy_pawns);
        constexpr auto effect_pose_grace = std::chrono::milliseconds(1200);
        const auto confirmed_hits = features::visuals::bullet_impacts().confirmed_hits_since(
            std::min(m_last_hit_sequence, m_last_kill_sequence));

        for (const auto& hit : confirmed_hits)
        {
            if (hit.sequence <= m_last_hit_sequence)
                continue;
            const auto cached_pose = m_last_death_poses.find(hit.pawn);
            const auto has_fresh_pose =
                cached_pose != m_last_death_poses.end() && pose_now - cached_pose->second.seen < effect_pose_grace;
            bool consumed = !cfg.on_shot.enabled;
            if (cfg.on_shot.enabled && has_fresh_pose)
            {
                m_shot_records.push_back(
                    {cached_pose->second.model_path, cached_pose->second.bones, pose_now, cached_pose->second.bounds});
                consumed = true;
            }
            else if (hit.timestamp.time_since_epoch().count() != 0 && pose_now - hit.timestamp >= effect_pose_grace)
            {
                consumed = true;
            }
            if (!consumed)
                break;
            m_last_hit_sequence = hit.sequence;
        }

        for (const auto& hit : confirmed_hits)
        {
            if (hit.sequence <= m_last_kill_sequence)
                continue;
            if (!hit.killed)
            {
                m_last_kill_sequence = hit.sequence;
                continue;
            }
            const auto cached_pose = m_last_death_poses.find(hit.pawn);
            const auto has_fresh_pose =
                cached_pose != m_last_death_poses.end() && pose_now - cached_pose->second.seen < effect_pose_grace;
            bool consumed = !cfg.kill_effect.enabled;
            if (cfg.kill_effect.enabled && has_fresh_pose)
            {
                death_record record{};
                static_cast<death_pose&>(record) = cached_pose->second;
                record.spawn = pose_now;
                record.seed = static_cast<float>((m_death_records.size() * 37u + 11u) % 997u);
                m_death_records.push_back(std::move(record));
                consumed = true;
            }
            else if (hit.timestamp.time_since_epoch().count() != 0 && pose_now - hit.timestamp >= effect_pose_grace)
            {
                consumed = true;
            }
            if (!consumed)
                break;
            m_last_kill_sequence = hit.sequence;
        }
        std::erase_if(m_last_death_poses,
                      [&](const auto& item)
                      {
                          return !std::ranges::binary_search(current_enemy_pawns, item.first) &&
                                 pose_now - item.second.seen > effect_pose_grace;
                      });
        if (!cfg.kill_effect.enabled)
        {
            m_death_records.clear();
        }

        for (auto& d : drawables)
        {
            auto& skin_matrices = *d.skin_matrices;
            skin_matrices.clear();
            d.bounds.mins = {1e12f, 1e12f, 1e12f};
            d.bounds.maxs = {-1e12f, -1e12f, -1e12f};
            for (std::size_t id = 0; id < d.mesh->bones.size(); ++id)
            {
                const auto position = d.bones->get_position(static_cast<std::uint32_t>(id));
                const auto rotation = d.bones->get_rotation(static_cast<std::uint32_t>(id));
                const auto scale = d.bones->get_scale(static_cast<std::uint32_t>(id));
                skin_matrices.push_back(compose_world_bone(position, rotation, scale) * d.mesh->bones[id].inverse_bind);
                d.bounds.mins.x = std::min(d.bounds.mins.x, position.x);
                d.bounds.mins.y = std::min(d.bounds.mins.y, position.y);
                d.bounds.mins.z = std::min(d.bounds.mins.z, position.z);
                d.bounds.maxs.x = std::max(d.bounds.maxs.x, position.x);
                d.bounds.maxs.y = std::max(d.bounds.maxs.y, position.y);
                d.bounds.maxs.z = std::max(d.bounds.maxs.z, position.z);
            }
            constexpr float k_model_margin{40.0f};
            d.bounds.mins -= {k_model_margin, k_model_margin, k_model_margin};
            d.bounds.maxs += {k_model_margin, k_model_margin, k_model_margin};
            update_bones(d.bone_buffer, skin_matrices);
            if (d.current)
                m_last_death_poses[d.current->pawn] = {d.current->model_path, skin_matrices, pose_now, d.bounds};
        }
    }

    const auto effect_now = std::chrono::steady_clock::now();
    const auto death_duration = std::max(cfg.kill_effect.duration, 0.2f);
    std::erase_if(m_shot_records,
                  [&](const shot_record& shot)
                  {
                      return !cfg.on_shot.enabled ||
                             std::chrono::duration<float>(effect_now - shot.spawn).count() >= cfg.on_shot.duration;
                  });
    std::erase_if(m_death_records,
                  [&](const death_record& death)
                  {
                      return !cfg.kill_effect.enabled ||
                             std::chrono::duration<float>(effect_now - death.spawn).count() >= death_duration;
                  });
    if (drawables.empty() && m_shot_records.empty() && m_death_records.empty())
    {
        return;
    }

    const auto view_projection = game::camera().matrix();
    const auto eye = game::camera().origin();
    const auto projectiles = game::world().projectiles();
    const auto smoke_occluded = [&](const game::player_snapshot* player)
    {
        if (!cfg.occlude_smoke || !player || !projectiles)
            return false;
        const auto ray = player->collision_center - eye;
        const auto length_sqr = ray.dot(ray);
        if (length_sqr <= 0.001f)
            return false;
        for (const auto& projectile : *projectiles)
        {
            if (projectile.subtype != game::projectile_kind::smoke_grenade || !projectile.smoke_active)
                continue;
            if (game::smoke_zone_of(projectile).crosses(eye, player->collision_center))
                return true;
        }
        return false;
    };
    update_view_projection(view_projection, eye);
    const auto bind_player_pipeline = [&](ID3D11PixelShader* pixel_shader)
    {
        m_context->IASetInputLayout(m_input_layout);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vertex_shader, nullptr, 0);
        m_context->GSSetShader(nullptr, nullptr, 0);
        m_context->PSSetShader(pixel_shader, nullptr, 0);
        ID3D11Buffer* vs_cbs[]{m_cb_view_projection, m_cb_bones, m_cb_material};
        m_context->VSSetConstantBuffers(0, 3, vs_cbs);
    };

    const auto draw_players = [&](bool update_player_material, int layer,
                                  const config::visual_profile::chams::material* override_material = nullptr,
                                  const float override_shell_expand = 0.0f)
    {
        for (const auto& d : drawables)
        {
            const auto opacity = d.current ? std::clamp(d.current->legit_opacity, 0.0f, 1.0f) : 1.0f;
            const auto scale_alpha = [opacity](auto& material)
            {
                material.color.a =
                    static_cast<std::uint8_t>(std::lround(static_cast<float>(material.color.a) * opacity));
                material.tint.a = static_cast<std::uint8_t>(std::lround(static_cast<float>(material.tint.a) * opacity));
            };
            if (override_material)
            {
                auto material = *override_material;
                scale_alpha(material);
                update_material(material, override_shell_expand);
            }
            else if (update_player_material)
            {
                auto visible = cfg.visible;
                auto invisible = cfg.invisible;
                if (d.current && d.current->invulnerable)
                {
                    visible.color = {
                        176, 176, 184,
                        static_cast<std::uint8_t>(std::lround(static_cast<float>(cfg.visible.color.a) * 0.45f))};
                    invisible.color = {
                        112, 116, 128,
                        static_cast<std::uint8_t>(std::lround(static_cast<float>(cfg.invisible.color.a) * 0.45f))};
                    visible.tint = {176, 176, 184, visible.color.a};
                    invisible.tint = {112, 116, 128, invisible.color.a};
                }
                else if (smoke_occluded(d.current))
                {
                    visible = invisible;
                }
                scale_alpha(visible);
                scale_alpha(invisible);
                update_material_pair(visible, invisible, layer);
            }
            m_context->VSSetConstantBuffers(1, 1, &d.bone_buffer);
            const UINT stride = sizeof(skinned_vertex);
            const UINT offset = 0;
            m_context->IASetVertexBuffers(0, 1, &d.gpu->vertex_buffer, &stride, &offset);
            m_context->IASetIndexBuffer(d.gpu->index_buffer, DXGI_FORMAT_R32_UINT, 0);
            m_context->DrawIndexed(d.gpu->index_count, 0, 0);
        }
    };

    ID3D11ShaderResourceView* null_srvs[2]{};
    m_context->PSSetShaderResources(0, 2, null_srvs);

    const bool world_ready = simulation::collision().valid() && ensure_world_geometry();
    if (!ensure_world_depth_buffer(target_width, target_height))
        return;
    // Without a BVH, clear depth to the far plane and draw only the visible
    // material. Occluded colouring resumes when map geometry is published.
    m_context->ClearDepthStencilView(m_world_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

    auto& occlusion_volumes = cache.occlusion_volumes;
    occlusion_volumes.clear();
    occlusion_volumes.reserve(drawables.size());
    const auto ndc_margin_x = 4.0f / static_cast<float>(std::max<UINT>(m_world_depth_width, 1));
    const auto ndc_margin_y = 4.0f / static_cast<float>(std::max<UINT>(m_world_depth_height, 1));
    for (const auto& d : drawables)
    {
        screen_volume volume{};
        volume.min_x = volume.min_y = 1e12f;
        volume.max_x = volume.max_y = -1e12f;
        volume.max_depth = 0.0f;
        bool valid{};
        bool crosses_near{};
        for (int corner = 0; corner < 8; ++corner)
        {
            const foundation::vec3 point{(corner & 1) ? d.bounds.maxs.x : d.bounds.mins.x,
                                         (corner & 2) ? d.bounds.maxs.y : d.bounds.mins.y,
                                         (corner & 4) ? d.bounds.maxs.z : d.bounds.mins.z};
            const auto clip_x = view_projection[0][0] * point.x + view_projection[0][1] * point.y +
                                view_projection[0][2] * point.z + view_projection[0][3];
            const auto clip_y = view_projection[1][0] * point.x + view_projection[1][1] * point.y +
                                view_projection[1][2] * point.z + view_projection[1][3];
            const auto clip_z = view_projection[2][0] * point.x + view_projection[2][1] * point.y +
                                view_projection[2][2] * point.z + view_projection[2][3];
            const auto clip_w = view_projection[3][0] * point.x + view_projection[3][1] * point.y +
                                view_projection[3][2] * point.z + view_projection[3][3];
            if (clip_w <= 0.001f)
            {
                crosses_near = true;
                continue;
            }
            const auto inverse_w = 1.0f / clip_w;
            volume.min_x = std::min(volume.min_x, clip_x * inverse_w);
            volume.min_y = std::min(volume.min_y, clip_y * inverse_w);
            volume.max_x = std::max(volume.max_x, clip_x * inverse_w);
            volume.max_y = std::max(volume.max_y, clip_y * inverse_w);
            volume.max_depth = std::max(volume.max_depth, clip_z * inverse_w);
            valid = true;
        }
        if (!valid)
            continue;
        if (crosses_near)
        {
            volume.min_x = volume.min_y = -1.0f;
            volume.max_x = volume.max_y = 1.0f;
            volume.max_depth = 1.0f;
        }
        else
        {
            volume.min_x -= ndc_margin_x;
            volume.min_y -= ndc_margin_y;
            volume.max_x += ndc_margin_x;
            volume.max_y += ndc_margin_y;
        }
        occlusion_volumes.push_back(volume);
    }
    if (world_ready && !occlusion_volumes.empty())
        draw_world_depth(m_world_dsv, occlusion_volumes, view_projection);
    // Resolve bounds include transient geometry, not just currently living players.
    auto& resolve_volumes = cache.resolve_volumes;
    resolve_volumes.assign(occlusion_volumes.begin(), occlusion_volumes.end());
    const auto include_effect = [&](const world_bounds& bounds)
    {
        const auto projected = render::project_depth_bounds(bounds, view_projection);
        if (!projected.valid)
            return;
        resolve_volumes.push_back({projected.min_x - ndc_margin_x, projected.min_y - ndc_margin_y,
                                   projected.max_x + ndc_margin_x, projected.max_y + ndc_margin_y, 1.0f});
    };
    for (const auto& shot : m_shot_records)
        include_effect(shot.bounds);
    for (const auto& death : m_death_records)
    {
        const auto progress = std::chrono::duration<float>(effect_now - death.spawn).count() / death_duration;
        include_effect(detail::death_effect_bounds(death.bounds, progress));
    }
    if (resolve_volumes.empty())
        return;
    m_context->OMSetRenderTargets(0, nullptr, nullptr);

    const bool use_msaa = cfg.antialiasing && ensure_msaa_targets(target_width, target_height);
    ID3D11RenderTargetView* const active_rtv = use_msaa ? m_msaa_rtv : backbuffer_rtv;
    ID3D11DepthStencilView* const active_dsv = use_msaa ? m_msaa_dsv : m_dsv;
    if (use_msaa)
    {
        constexpr float transparent[4]{};
        m_context->ClearRenderTargetView(m_msaa_rtv, transparent);
    }

    const float blend_factor[4]{};
    m_context->OMSetBlendState(m_blend_state, blend_factor, 0xFFFFFFFF);

    m_context->ClearDepthStencilView(active_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    m_context->OMSetRenderTargets(0, nullptr, active_dsv);
    bind_player_pipeline(nullptr);
    m_context->OMSetDepthStencilState(m_depth_state, 0);
    m_context->RSSetState(m_rs_solid);
    draw_players(false, 0);

    m_context->OMSetRenderTargets(1, &active_rtv, active_dsv);
    bind_player_pipeline(m_pixel_shader);
    m_context->PSSetConstantBuffers(0, 1, &m_cb_view_projection);
    m_context->PSSetConstantBuffers(2, 1, &m_cb_material);
    ID3D11ShaderResourceView* world_srvs[2]{};
    world_srvs[0] = m_world_depth_srv;
    m_context->PSSetShaderResources(0, 2, world_srvs);
    m_context->OMSetDepthStencilState(m_depth_state_equal, 0);

    D3D11_RECT bloom_scissor{0, 0, static_cast<LONG>(target_width), static_cast<LONG>(target_height)};
    bool bloom_ready = !occlusion_volumes.empty() && cfg.glow_effect.enabled && cfg.glow_effect.strength > 0.0f &&
                       ensure_bloom_targets(target_width, target_height);
    if (bloom_ready)
    {
        float union_min_x{1.0f}, union_min_y{1.0f};
        float union_max_x{-1.0f}, union_max_y{-1.0f};
        for (const auto& volume : occlusion_volumes)
        {
            union_min_x = std::min(union_min_x, volume.min_x);
            union_min_y = std::min(union_min_y, volume.min_y);
            union_max_x = std::max(union_max_x, volume.max_x);
            union_max_y = std::max(union_max_y, volume.max_y);
        }
        const auto bloom_margin = static_cast<LONG>(std::ceil(cfg.glow_effect.radius * 4.0f + 8.0f));
        bloom_scissor = {
            static_cast<LONG>(std::floor((std::clamp(union_min_x, -1.0f, 1.0f) * 0.5f + 0.5f) * target_width)) -
                bloom_margin,
            static_cast<LONG>(std::floor((0.5f - std::clamp(union_max_y, -1.0f, 1.0f) * 0.5f) * target_height)) -
                bloom_margin,
            static_cast<LONG>(std::ceil((std::clamp(union_max_x, -1.0f, 1.0f) * 0.5f + 0.5f) * target_width)) +
                bloom_margin,
            static_cast<LONG>(std::ceil((0.5f - std::clamp(union_min_y, -1.0f, 1.0f) * 0.5f) * target_height)) +
                bloom_margin};
        bloom_scissor.left = std::clamp<LONG>(bloom_scissor.left, 0, target_width);
        bloom_scissor.top = std::clamp<LONG>(bloom_scissor.top, 0, target_height);
        bloom_scissor.right = std::clamp<LONG>(bloom_scissor.right, 0, target_width);
        bloom_scissor.bottom = std::clamp<LONG>(bloom_scissor.bottom, 0, target_height);

        auto glow = cfg.visible;
        glow.enabled = true;
        glow.type = config::visual_profile::chams::solid;
        glow.wireframe = false;
        glow.color = cfg.glow_effect.color;
        glow.color.a = 52;

        const auto glow_shell_expand = std::clamp(cfg.glow_effect.radius * 0.10f, 0.05f, 1.60f);
        constexpr float transparent[4]{};
        m_context->ClearRenderTargetView(m_bloom_source_rtv, transparent);
        m_context->ClearRenderTargetView(m_bloom_a_rtv, transparent);
        m_context->ClearRenderTargetView(m_bloom_b_rtv, transparent);
        m_context->ClearRenderTargetView(m_bloom_inner_rtv, transparent);
        D3D11_VIEWPORT bloom_viewport{0.0f, 0.0f, static_cast<float>(m_bloom_width), static_cast<float>(m_bloom_height),
                                      0.0f, 1.0f};
        m_context->RSSetViewports(1, &bloom_viewport);

        m_context->ClearDepthStencilView(m_bloom_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        m_context->OMSetRenderTargets(0, nullptr, m_bloom_dsv);
        bind_player_pipeline(nullptr);
        update_material(glow, glow_shell_expand);
        m_context->OMSetDepthStencilState(m_depth_state, 0);
        m_context->RSSetState(m_rs_world_scissor);
        m_context->RSSetScissorRects(1, &bloom_scissor);
        draw_players(false, 0, &glow, glow_shell_expand);

        m_context->OMSetRenderTargets(1, &m_bloom_source_rtv, m_bloom_dsv);
        bind_player_pipeline(m_bloom_mask_shader);
        m_context->PSSetConstantBuffers(0, 1, &m_cb_view_projection);
        m_context->PSSetConstantBuffers(2, 1, &m_cb_material);
        m_context->OMSetDepthStencilState(m_depth_state_read_only, 0);
        m_context->OMSetBlendState(m_blend_disabled, blend_factor, 0xFFFFFFFF);
        m_context->RSSetState(m_rs_world_scissor);
        m_context->RSSetScissorRects(1, &bloom_scissor);
        draw_players(false, 0, &glow, glow_shell_expand);

        const auto write_shell_layer = [&](const float fraction, const std::uint8_t alpha)
        {
            glow.color.a = alpha;
            m_context->ClearDepthStencilView(m_bloom_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
            m_context->OMSetRenderTargets(0, nullptr, m_bloom_dsv);
            bind_player_pipeline(nullptr);
            update_material(glow, glow_shell_expand * fraction);
            m_context->OMSetDepthStencilState(m_depth_state, 0);
            m_context->RSSetState(m_rs_world_scissor);
            m_context->RSSetScissorRects(1, &bloom_scissor);
            draw_players(false, 0, &glow, glow_shell_expand * fraction);
            m_context->OMSetRenderTargets(1, &m_bloom_source_rtv, m_bloom_dsv);
            bind_player_pipeline(m_bloom_mask_shader);
            m_context->PSSetConstantBuffers(0, 1, &m_cb_view_projection);
            m_context->PSSetConstantBuffers(2, 1, &m_cb_material);
            m_context->OMSetDepthStencilState(m_depth_state_read_only, 0);
            m_context->OMSetBlendState(m_blend_disabled, blend_factor, 0xFFFFFFFF);
            draw_players(false, 0, &glow, glow_shell_expand * fraction);
        };
        write_shell_layer(0.66f, 112);
        write_shell_layer(0.33f, 196);

        m_context->ClearDepthStencilView(m_bloom_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        m_context->OMSetRenderTargets(0, nullptr, m_bloom_dsv);
        bind_player_pipeline(nullptr);
        glow.color.a = 255;
        update_material(glow, 0.0f);
        m_context->OMSetDepthStencilState(m_depth_state, 0);
        m_context->RSSetState(m_rs_world_scissor);
        m_context->RSSetScissorRects(1, &bloom_scissor);
        draw_players(false, 0, &glow, 0.0f);
        m_context->OMSetRenderTargets(1, &m_bloom_inner_rtv, m_bloom_dsv);
        bind_player_pipeline(m_bloom_mask_shader);
        m_context->PSSetConstantBuffers(0, 1, &m_cb_view_projection);
        m_context->PSSetConstantBuffers(2, 1, &m_cb_material);
        m_context->OMSetDepthStencilState(m_depth_state_read_only, 0);
        m_context->OMSetBlendState(m_blend_disabled, blend_factor, 0xFFFFFFFF);
        draw_players(false, 0, &glow, 0.0f);

        const auto set_bloom_constants = [&](const float x, const float y, const float strength)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(m_context->Map(m_cb_bloom, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                auto* constants = static_cast<cb_bloom*>(mapped.pData);
                constants->texel[0] = 1.0f / static_cast<float>(m_bloom_width);
                constants->texel[1] = 1.0f / static_cast<float>(m_bloom_height);
                constants->direction[0] = x;
                constants->direction[1] = y;
                constants->strength = strength;
                constants->target_size[0] = static_cast<float>(target_width);
                constants->target_size[1] = static_cast<float>(target_height);
                constants->padding = 1.0f;
                m_context->Unmap(m_cb_bloom, 0);
            }
        };
        m_context->IASetInputLayout(nullptr);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_bloom_vertex_shader, nullptr, 0);
        m_context->GSSetShader(nullptr, nullptr, 0);
        m_context->PSSetShader(m_bloom_blur_shader, nullptr, 0);
        m_context->PSSetConstantBuffers(0, 1, &m_cb_bloom);
        m_context->PSSetSamplers(0, 1, &m_bloom_sampler);
        m_context->OMSetBlendState(m_blend_disabled, blend_factor, 0xFFFFFFFF);
        ID3D11ShaderResourceView* null_bloom_srv{};
        const auto blur_lobe = [&](const float spread)
        {
            set_bloom_constants(spread, 0.0f, 1.0f);
            m_context->OMSetRenderTargets(1, &m_bloom_b_rtv, nullptr);
            m_context->PSSetShaderResources(0, 1, &m_bloom_source_srv);
            m_context->Draw(3, 0);
            m_context->PSSetShaderResources(0, 1, &null_bloom_srv);
            set_bloom_constants(0.0f, spread, 1.0f);
            m_context->OMSetRenderTargets(1, &m_bloom_a_rtv, nullptr);
            m_context->PSSetShaderResources(0, 1, &m_bloom_b_srv);
            m_context->Draw(3, 0);
            m_context->PSSetShaderResources(0, 1, &null_bloom_srv);
        };
        blur_lobe(1.40f);

        D3D11_VIEWPORT full_viewport{0.0f, 0.0f, static_cast<float>(target_width), static_cast<float>(target_height),
                                     0.0f, 1.0f};
        m_context->RSSetViewports(1, &full_viewport);
        m_context->OMSetRenderTargets(1, &active_rtv, active_dsv);
        m_context->OMSetBlendState(m_blend_state, blend_factor, 0xFFFFFFFF);
        bind_player_pipeline(m_pixel_shader);
        m_context->PSSetConstantBuffers(0, 1, &m_cb_view_projection);
        m_context->PSSetConstantBuffers(2, 1, &m_cb_material);
        m_context->PSSetShaderResources(0, 2, world_srvs);
        m_context->OMSetDepthStencilState(m_depth_state_equal, 0);
    }

    const auto draw_geometry_layer = [&](int layer, bool wireframe)
    {
        m_context->RSSetState(wireframe ? m_rs_wireframe : m_rs_solid);
        draw_players(true, layer);
    };

    if (cfg.visible.enabled && cfg.invisible.enabled && cfg.visible.wireframe == cfg.invisible.wireframe)
    {
        draw_geometry_layer(0, cfg.visible.wireframe);
    }
    else
    {
        if (cfg.invisible.enabled)
            draw_geometry_layer(2, cfg.invisible.wireframe);
        if (cfg.visible.enabled)
            draw_geometry_layer(1, cfg.visible.wireframe);
    }

    const auto shot_now = effect_now;
    if (cfg.on_shot.enabled && !m_shot_records.empty())
    {
        m_context->OMSetDepthStencilState(m_depth_state_disabled, 0);
        m_context->RSSetState(cfg.on_shot.appearance.wireframe ? m_rs_wireframe : m_rs_solid);
        for (const auto& shot : m_shot_records)
        {
            const auto gpu = m_gpu_meshes.find(shot.model_path);
            if (gpu == m_gpu_meshes.end() || shot.bones.empty())
                continue;
            auto material = cfg.on_shot.appearance;
            const auto age = std::chrono::duration<float>(shot_now - shot.spawn).count();
            material.color.a = static_cast<std::uint8_t>(
                std::clamp(static_cast<float>(material.color.a) * (1.0f - age / std::max(cfg.on_shot.duration, 0.01f)),
                           0.0f, 255.0f));
            update_bones(shot.bones);
            update_material(material);
            m_context->VSSetConstantBuffers(1, 1, &m_cb_bones);
            const UINT stride = sizeof(skinned_vertex);
            const UINT offset = 0;
            m_context->IASetVertexBuffers(0, 1, &gpu->second.vertex_buffer, &stride, &offset);
            m_context->IASetIndexBuffer(gpu->second.index_buffer, DXGI_FORMAT_R32_UINT, 0);
            m_context->DrawIndexed(gpu->second.index_count, 0, 0);
        }
    }

    const auto death_now = effect_now;
    if (cfg.kill_effect.enabled && !m_death_records.empty())
    {
        m_context->OMSetDepthStencilState(m_depth_state_disabled, 0);
        m_context->RSSetState(m_rs_solid);
        m_context->GSSetShader(m_death_geometry_shader, nullptr, 0);
        ID3D11Buffer* gs_cbs[]{m_cb_view_projection, m_cb_bones, m_cb_material};
        m_context->GSSetConstantBuffers(0, 3, gs_cbs);
        for (const auto& death : m_death_records)
        {
            const auto gpu = m_gpu_meshes.find(death.model_path);
            if (gpu == m_gpu_meshes.end() || death.bones.empty())
                continue;
            const auto age = std::chrono::duration<float>(death_now - death.spawn).count();
            const auto progress = std::clamp(age / death_duration, 0.0f, 1.0f);
            auto material = cfg.visible;
            material.enabled = true;
            material.type = config::visual_profile::chams::solid;
            material.wireframe = false;
            material.color = cfg.kill_effect.color;
            material.color.a = static_cast<std::uint8_t>(
                std::clamp(static_cast<float>(material.color.a) * (1.0f - progress), 0.0f, 255.0f));
            update_bones(death.bones);
            update_material(material, 0.0f, progress, death.seed);
            m_context->VSSetConstantBuffers(1, 1, &m_cb_bones);
            const UINT stride = sizeof(skinned_vertex);
            const UINT offset = 0;
            m_context->IASetVertexBuffers(0, 1, &gpu->second.vertex_buffer, &stride, &offset);
            m_context->IASetIndexBuffer(gpu->second.index_buffer, DXGI_FORMAT_R32_UINT, 0);
            m_context->DrawIndexed(gpu->second.index_count, 0, 0);
        }
        m_context->GSSetShader(nullptr, nullptr, 0);
    }
    m_context->PSSetShaderResources(0, 2, null_srvs);
    if (use_msaa)
    {
        // A multisampled texture cannot be an RTV and SRV simultaneously. Detach
        // it before the bounded player-and-effect resolve samples it.
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        resolve_msaa(backbuffer_rtv, resolve_volumes);
    }
    if (bloom_ready)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_context->Map(m_cb_bloom, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            auto* constants = static_cast<cb_bloom*>(mapped.pData);
            constants->texel[0] = 1.0f / static_cast<float>(m_bloom_width);
            constants->texel[1] = 1.0f / static_cast<float>(m_bloom_height);
            constants->direction[0] = constants->direction[1] = 0.0f;
            constants->strength =
                std::clamp(cfg.glow_effect.strength * static_cast<float>(cfg.glow_effect.color.a) / 255.0f, 0.0f, 1.0f);
            constants->target_size[0] = static_cast<float>(target_width);
            constants->target_size[1] = static_cast<float>(target_height);
            constants->padding = 1.0f;
            m_context->Unmap(m_cb_bloom, 0);
        }
        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(target_width), static_cast<float>(target_height),
                                0.0f, 1.0f};
        m_context->RSSetViewports(1, &viewport);
        m_context->OMSetRenderTargets(1, &backbuffer_rtv, nullptr);
        m_context->IASetInputLayout(nullptr);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_bloom_vertex_shader, nullptr, 0);
        m_context->GSSetShader(nullptr, nullptr, 0);
        m_context->PSSetShader(m_bloom_composite_shader, nullptr, 0);
        m_context->PSSetConstantBuffers(0, 1, &m_cb_bloom);
        m_context->PSSetSamplers(0, 1, &m_bloom_sampler);
        ID3D11ShaderResourceView* bloom_srvs[]{m_bloom_a_srv, m_bloom_inner_srv, m_bloom_source_srv};
        m_context->PSSetShaderResources(0, 3, bloom_srvs);
        m_context->RSSetState(m_rs_world_scissor);
        m_context->RSSetScissorRects(1, &bloom_scissor);
        m_context->OMSetDepthStencilState(m_depth_state_disabled, 0);
        m_context->OMSetBlendState(m_bloom_blend_state, blend_factor, 0xFFFFFFFF);
        m_context->Draw(3, 0);
        ID3D11ShaderResourceView* null_bloom_srvs[3]{};
        m_context->PSSetShaderResources(0, 3, null_bloom_srvs);
    }

    m_diag.players_drawn = static_cast<int>(drawables.size());
}

} // namespace features::chams
