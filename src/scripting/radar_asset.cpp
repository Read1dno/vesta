#include <stdafx.hpp>

#include <scripting/radar_asset.hpp>

#include <core/assets/vpk.hpp>
#include <render/chams/texture.hpp>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <charconv>

namespace scripting::radar_asset {

	using Microsoft::WRL::ComPtr;

	namespace {

		[[nodiscard]] std::optional<float> overview_number(
			const std::string_view text, const std::string_view key,
			const std::size_t start = 0, const std::size_t end = std::string_view::npos )
		{
			const auto limit = std::min( end, text.size( ) );
			auto cursor = text.find( key, start );
			if ( cursor == std::string_view::npos || cursor >= limit ) return std::nullopt;
			cursor = text.find( '"', cursor + key.size( ) );
			if ( cursor == std::string_view::npos || cursor >= limit ) return std::nullopt;
			const auto finish = text.find( '"', cursor + 1 );
			if ( finish == std::string_view::npos || finish > limit ) return std::nullopt;
			float value{};
			const auto first = text.data( ) + cursor + 1;
			const auto last = text.data( ) + finish;
			const auto parsed = std::from_chars( first, last, value );
			if ( parsed.ec != std::errc{} || parsed.ptr != last || !std::isfinite( value ) )
				return std::nullopt;
			return value;
		}

		[[nodiscard]] bool encode_png( const std::filesystem::path& path,
			const std::uint32_t width, const std::uint32_t height,
			const std::uint8_t* pixels, const std::uint32_t pitch )
		{
			const auto com = ::CoInitializeEx( nullptr, COINIT_MULTITHREADED );
			const auto owns_com = SUCCEEDED( com );
			if ( FAILED( com ) && com != RPC_E_CHANGED_MODE ) return false;

			ComPtr<IWICImagingFactory> factory{};
			ComPtr<IWICStream> stream{};
			ComPtr<IWICBitmapEncoder> encoder{};
			ComPtr<IWICBitmapFrameEncode> frame{};
			ComPtr<IPropertyBag2> properties{};
			ComPtr<IWICBitmap> bitmap{};
			bool result{};
			do
			{
				if ( FAILED( ::CoCreateInstance( CLSID_WICImagingFactory2, nullptr,
					CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &factory ) ) ) ) break;
				if ( FAILED( factory->CreateBitmapFromMemory( width, height,
					GUID_WICPixelFormat32bppRGBA, pitch, pitch * height,
					const_cast<BYTE*>( pixels ), &bitmap ) ) ) break;
				if ( FAILED( factory->CreateStream( &stream ) )
					|| FAILED( stream->InitializeFromFilename( path.c_str( ), GENERIC_WRITE ) )
					|| FAILED( factory->CreateEncoder( GUID_ContainerFormatPng, nullptr, &encoder ) )
					|| FAILED( encoder->Initialize( stream.Get( ), WICBitmapEncoderNoCache ) )
					|| FAILED( encoder->CreateNewFrame( &frame, &properties ) )
					|| FAILED( frame->Initialize( properties.Get( ) ) )
					|| FAILED( frame->SetSize( width, height ) ) ) break;
				WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
				if ( FAILED( frame->SetPixelFormat( &format ) ) ) break;
				if ( FAILED( frame->WriteSource( bitmap.Get( ), nullptr ) )
					|| FAILED( frame->Commit( ) ) || FAILED( encoder->Commit( ) ) ) break;
				result = true;
			} while ( false );

			properties.Reset( );
			frame.Reset( );
			encoder.Reset( );
			stream.Reset( );
			bitmap.Reset( );
			factory.Reset( );
			if ( owns_com ) ::CoUninitialize( );
			return result;
		}

		[[nodiscard]] bool decode_texture_to_png( chams::vpk_archive& archive,
			const std::string& source, const std::filesystem::path& destination,
			std::uint32_t& output_width, std::uint32_t& output_height )
		{
			const auto texture = chams::load_texture( archive, source );
			if ( !texture.valid( ) || texture.mips.empty( ) ) return false;
			output_width = texture.width;
			output_height = texture.height;
			std::error_code cache_error{};
			if ( std::filesystem::exists( destination, cache_error ) && !cache_error
				&& std::filesystem::file_size( destination, cache_error ) > 0 && !cache_error )
				return true;

			ComPtr<ID3D11Device> device{};
			ComPtr<ID3D11DeviceContext> context{};
			D3D_FEATURE_LEVEL level{};
			if ( FAILED( ::D3D11CreateDevice( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
				&device, &level, &context ) ) ) return false;

			D3D11_TEXTURE2D_DESC source_desc{};
			source_desc.Width = texture.width;
			source_desc.Height = texture.height;
			source_desc.MipLevels = 1;
			source_desc.ArraySize = 1;
			source_desc.Format = static_cast<DXGI_FORMAT>( texture.dxgi_format );
			source_desc.SampleDesc.Count = 1;
			source_desc.Usage = D3D11_USAGE_IMMUTABLE;
			source_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA source_data{};
			source_data.pSysMem = texture.mips.front( ).data( );
			source_data.SysMemPitch = texture.block_size
				? std::max<std::uint32_t>( 1, ( texture.width + 3 ) / 4 ) * texture.block_size
				: texture.width * 4;
			ComPtr<ID3D11Texture2D> source_texture{};
			ComPtr<ID3D11ShaderResourceView> source_view{};
			if ( FAILED( device->CreateTexture2D( &source_desc, &source_data, &source_texture ) )
				|| FAILED( device->CreateShaderResourceView( source_texture.Get( ), nullptr,
					&source_view ) ) ) return false;

			D3D11_TEXTURE2D_DESC target_desc{};
			target_desc.Width = texture.width;
			target_desc.Height = texture.height;
			target_desc.MipLevels = 1;
			target_desc.ArraySize = 1;
			target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			target_desc.SampleDesc.Count = 1;
			target_desc.Usage = D3D11_USAGE_DEFAULT;
			target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
			ComPtr<ID3D11Texture2D> target{};
			ComPtr<ID3D11RenderTargetView> target_view{};
			if ( FAILED( device->CreateTexture2D( &target_desc, nullptr, &target ) )
				|| FAILED( device->CreateRenderTargetView( target.Get( ), nullptr,
					&target_view ) ) ) return false;

			static constexpr char shader[] = R"(
Texture2D image_texture : register(t0);
SamplerState image_sampler : register(s0);
struct vertex_out { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
vertex_out vs_main(uint id : SV_VertexID) {
    vertex_out value;
    value.uv = float2((id << 1) & 2, id & 2);
    value.position = float4(value.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return value;
}
float4 ps_main(vertex_out input) : SV_TARGET { return image_texture.Sample(image_sampler, input.uv); }
)";
			ComPtr<ID3DBlob> vertex_blob{}, pixel_blob{};
			if ( FAILED( ::D3DCompile( shader, sizeof( shader ) - 1, nullptr, nullptr, nullptr,
				"vs_main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vertex_blob, nullptr ) )
				|| FAILED( ::D3DCompile( shader, sizeof( shader ) - 1, nullptr, nullptr, nullptr,
					"ps_main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pixel_blob, nullptr ) ) )
				return false;
			ComPtr<ID3D11VertexShader> vertex_shader{};
			ComPtr<ID3D11PixelShader> pixel_shader{};
			if ( FAILED( device->CreateVertexShader( vertex_blob->GetBufferPointer( ),
				vertex_blob->GetBufferSize( ), nullptr, &vertex_shader ) )
				|| FAILED( device->CreatePixelShader( pixel_blob->GetBufferPointer( ),
					pixel_blob->GetBufferSize( ), nullptr, &pixel_shader ) ) ) return false;

			D3D11_SAMPLER_DESC sampler_desc{};
			sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW =
				D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
			ComPtr<ID3D11SamplerState> sampler{};
			if ( FAILED( device->CreateSamplerState( &sampler_desc, &sampler ) ) ) return false;

			const D3D11_VIEWPORT viewport{ 0.0f, 0.0f,
				static_cast<float>( texture.width ), static_cast<float>( texture.height ), 0.0f, 1.0f };
			auto* raw_target = target_view.Get( );
			auto* raw_source = source_view.Get( );
			auto* raw_sampler = sampler.Get( );
			context->OMSetRenderTargets( 1, &raw_target, nullptr );
			context->RSSetViewports( 1, &viewport );
			context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
			context->VSSetShader( vertex_shader.Get( ), nullptr, 0 );
			context->PSSetShader( pixel_shader.Get( ), nullptr, 0 );
			context->PSSetShaderResources( 0, 1, &raw_source );
			context->PSSetSamplers( 0, 1, &raw_sampler );
			context->Draw( 3, 0 );

			auto staging_desc = target_desc;
			staging_desc.Usage = D3D11_USAGE_STAGING;
			staging_desc.BindFlags = 0;
			staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ComPtr<ID3D11Texture2D> staging{};
			if ( FAILED( device->CreateTexture2D( &staging_desc, nullptr, &staging ) ) ) return false;
			context->CopyResource( staging.Get( ), target.Get( ) );
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if ( FAILED( context->Map( staging.Get( ), 0, D3D11_MAP_READ, 0, &mapped ) ) ) return false;
			auto temporary = destination;
			temporary += L".tmp";
			std::error_code file_error{};
			std::filesystem::remove( temporary, file_error );
			const auto encoded = encode_png( temporary, texture.width, texture.height,
				static_cast<const std::uint8_t*>( mapped.pData ), mapped.RowPitch );
			context->Unmap( staging.Get( ), 0 );
			if ( !encoded )
			{
				std::filesystem::remove( temporary, file_error );
				return false;
			}
			std::filesystem::remove( destination, file_error );
			file_error.clear( );
			std::filesystem::rename( temporary, destination, file_error );
			if ( file_error )
			{
				std::filesystem::remove( temporary, file_error );
				return false;
			}
			return true;
		}

	}

	overview export_overview( const std::string& map_name,
		const std::filesystem::path& output_directory, const std::string& output_stem )
	{
		overview result{};
		if ( map_name.empty( ) || output_stem.empty( ) )
		{
			result.error = "invalid map or output name";
			return result;
		}
		try
		{
			chams::vpk_archive archive{};
			const auto pak = chams::vpk_archive::locate_cs2_pak( );
			const auto overview_path = "resource/overviews/" + map_name + ".txt";
			const auto texture_base = "panorama/images/overheadmaps/" + map_name;
			if ( pak.empty( ) )
			{
				result.error = "CS2 pak01_dir.vpk was not found";
				return result;
			}
			if ( !archive.open_selected( pak, {
				overview_path, texture_base + "_radar_psd.vtex_c",
				texture_base + "_lower_radar_psd.vtex_c" } ) )
			{
				result.error = "requested overview assets were not found in the VPK";
				return result;
			}
			const auto* entry = archive.find( overview_path );
			if ( !entry )
			{
				result.error = "overview metadata is missing";
				return result;
			}
			const auto bytes = archive.read( *entry );
			const std::string text( bytes.begin( ), bytes.end( ) );
			const auto pos_x = overview_number( text, "\"pos_x\"" );
			const auto pos_y = overview_number( text, "\"pos_y\"" );
			const auto scale = overview_number( text, "\"scale\"" );
			if ( !pos_x || !pos_y || !scale || *scale <= 0.0f )
			{
				result.error = "overview metadata is invalid";
				return result;
			}
			result.pos_x = *pos_x;
			result.pos_y = *pos_y;
			result.scale = *scale;

			std::error_code error{};
			std::filesystem::create_directories( output_directory, error );
			if ( error )
			{
				result.error = "overview cache directory could not be created";
				return result;
			}
			result.primary_path = output_directory / ( output_stem + "-primary.png" );
			if ( !decode_texture_to_png( archive, texture_base + "_radar_psd.vtex_c",
				result.primary_path, result.width, result.height ) )
			{
				result.error = "official overview texture could not be decoded";
				return result;
			}

			const auto lower_marker = text.find( "\"lower\"" );
			if ( lower_marker != std::string::npos )
			{
				const auto next_section = text.find( '\n', lower_marker );
				if ( const auto altitude = overview_number( text, "\"AltitudeMax\"",
					lower_marker, next_section == std::string::npos ? text.size( ) : text.size( ) ) )
					result.lower_altitude_max = *altitude;
				result.lower_path = output_directory / ( output_stem + "-lower.png" );
				std::uint32_t lower_width{}, lower_height{};
				result.has_lower = decode_texture_to_png( archive,
					texture_base + "_lower_radar_psd.vtex_c", result.lower_path,
					lower_width, lower_height );
			}
			result.valid = true;
			return result;
		}
		catch ( const std::exception& exception )
		{
			result.error = exception.what( );
			return result;
		}
		catch ( ... )
		{
			result.error = "unknown overview export error";
			return result;
		}
	}

}
