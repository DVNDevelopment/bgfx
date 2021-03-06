/*
 * Copyright 2011-2012 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef __RENDERER_D3D9_H__
#define __RENDERER_D3D9_H__

#define BGFX_CONFIG_RENDERER_DIRECT3D_EX (BX_PLATFORM_WINDOWS && 0)

#if BX_PLATFORM_WINDOWS
#	if !BGFX_CONFIG_RENDERER_DIRECT3D_EX
#		define D3D_DISABLE_9EX
#	endif // !BGFX_CONFIG_RENDERER_DIRECT3D_EX
#	include <d3d9.h>

#	ifndef D3DFMT_NULL
#		define D3DFMT_NULL ( (D3DFORMAT)MAKEFOURCC('N','U','L','L') )
#	endif // D3DFMT_NULL

#	ifndef D3DFMT_DF16
#		define D3DFMT_DF16 ( (D3DFORMAT)MAKEFOURCC('D','F','1','6') )
#	endif // D3DFMT_DF16

#	ifndef D3DFMT_DF24
#		define D3DFMT_DF24 ( (D3DFORMAT)MAKEFOURCC('D','F','2','4') )
#	endif // D3DFMT_DF24

#	ifndef D3DFMT_INTZ
#		define D3DFMT_INTZ ( (D3DFORMAT)MAKEFOURCC('I','N','T','Z') )
#	endif // D3DFMT_INTZ

#	ifndef D3DFMT_RAWZ
#		define D3DFMT_RAWZ ( (D3DFORMAT)MAKEFOURCC('R','A','W','Z') )
#	endif // D3DFMT_RAWZ

#	if BGFX_CONFIG_RENDERER_DIRECT3D_EX
typedef HRESULT (WINAPI *Direct3DCreate9ExFunc)(UINT SDKVersion, IDirect3D9Ex**);
#	else
typedef IDirect3D9* (WINAPI *Direct3DCreate9Func)(UINT SDKVersion);
#	endif // BGFX_CONFIG_RENDERER_DIRECT3D_EX

typedef int (WINAPI *D3DPERF_BeginEventFunc)(D3DCOLOR col, LPCWSTR wszName);
typedef int (WINAPI *D3DPERF_EndEventFunc)();
typedef void (WINAPI *D3DPERF_SetMarkerFunc)(D3DCOLOR col, LPCWSTR wszName);
typedef void (WINAPI *D3DPERF_SetRegionFunc)(D3DCOLOR col, LPCWSTR wszName);
typedef BOOL (WINAPI *D3DPERF_QueryRepeatFrameFunc)();
typedef void (WINAPI *D3DPERF_SetOptionsFunc)(DWORD dwOptions);
typedef DWORD (WINAPI *D3DPERF_GetStatusFunc)();

#	define _PIX_SETMARKER(_col, _name) s_renderCtx.m_D3DPERF_SetMarker(_col, L#_name)
#	define _PIX_BEGINEVENT(_col, _name) s_renderCtx.m_D3DPERF_BeginEvent(_col, L#_name)
#	define _PIX_ENDEVENT() s_renderCtx.m_D3DPERF_EndEvent()

#elif BX_PLATFORM_XBOX360
#	include <xgraphics.h>
#	define D3DUSAGE_DYNAMIC 0 // not supported on X360
#	define D3DLOCK_DISCARD 0 // not supported on X360
#	define D3DERR_DEVICEHUNG D3DERR_DEVICELOST // not supported on X360
#	define D3DERR_DEVICEREMOVED D3DERR_DEVICELOST // not supported on X360
#	define D3DMULTISAMPLE_8_SAMPLES D3DMULTISAMPLE_4_SAMPLES
#	define D3DMULTISAMPLE_16_SAMPLES D3DMULTISAMPLE_4_SAMPLES

#	define D3DFMT_DF24 D3DFMT_D24FS8

#	define _PIX_SETMARKER(_col, _name) do {} while(0)
#	define _PIX_BEGINEVENT(_col, _name) do {} while(0)
#	define _PIX_ENDEVENT() do {} while(0)
#endif // BX_PLATFORM_

namespace bgfx
{
#	define _DX_CHECK(_call) \
				do { \
					HRESULT __hr__ = _call; \
					BX_CHECK(SUCCEEDED(__hr__), #_call " FAILED 0x%08x\n", (uint32_t)__hr__); \
				} while (0)

#if BGFX_CONFIG_DEBUG
#	define DX_CHECK(_call) _DX_CHECK(_call)
#else
#	define DX_CHECK(_call) _call
#endif // BGFX_CONFIG_DEBUG

#if BGFX_CONFIG_DEBUG_PIX
#	define PIX_SETMARKER(_col, _name) _PIX_SETMARKER(_col, _name)
#	define PIX_BEGINEVENT(_col, _name) _PIX_BEGINEVENT(_col, _name)
#	define PIX_ENDEVENT() _PIX_ENDEVENT()
#else
#	define PIX_SETMARKER(_col, _name)
#	define PIX_BEGINEVENT(_col, _name)
#	define PIX_ENDEVENT()
#endif // BGFX_CONFIG_DEBUG_PIX

#if BGFX_CONFIG_DEBUG
#	define DX_RELEASE(_ptr, _expected) \
					do { \
						if (NULL != _ptr) \
						{ \
							ULONG count = _ptr->Release(); \
							BX_CHECK(_expected == count, "RefCount is %d (expected %d).", count, _expected); \
							_ptr = NULL; \
						} \
					} while (0)
#else
#	define DX_RELEASE(_ptr, _expected) \
					do { \
						if (NULL != _ptr) \
						{ \
							_ptr->Release(); \
							_ptr = NULL; \
						} \
					} while (0)
#endif // BGFX_CONFIG_DEBUG

	struct Msaa
	{
		D3DMULTISAMPLE_TYPE m_type;
		DWORD m_quality;
	};

	struct IndexBuffer
	{
		IndexBuffer()
			: m_ptr(NULL)
			, m_dynamic(false)
		{
		}

		void create(uint32_t _size, void* _data);
		void update(uint32_t _offset, uint32_t _size, void* _data)
		{
			void* buffer;
			DX_CHECK(m_ptr->Lock(_offset
				, _size
				, &buffer
				, m_dynamic && 0 == _offset && m_size == _size ? D3DLOCK_DISCARD : 0
				) );

			memcpy(buffer, _data, _size);

			m_ptr->Unlock();
		}

		void destroy()
		{
			if (NULL != m_ptr)
			{
				DX_RELEASE(m_ptr, 0);
				m_dynamic = false;
			}
		}

		void preReset();
		void postReset();

		IDirect3DIndexBuffer9* m_ptr;
		uint32_t m_size;
		bool m_dynamic;
	};

	struct VertexBuffer
	{
		VertexBuffer()
			: m_ptr(NULL)
			, m_dynamic(false)
		{
		}

		void create(uint32_t _size, void* _data, VertexDeclHandle _declHandle);
		void update(uint32_t _offset, uint32_t _size, void* _data)
		{
			void* buffer;
			DX_CHECK(m_ptr->Lock(_offset
				, _size
				, &buffer
				, m_dynamic && 0 == _offset && m_size == _size ? D3DLOCK_DISCARD : 0
				) );

			memcpy(buffer, _data, _size);

			m_ptr->Unlock();
		}

		void destroy()
		{
			if (NULL != m_ptr)
			{
				DX_RELEASE(m_ptr, 0);
				m_dynamic = false;
			}
		}

		void preReset();
		void postReset();

		IDirect3DVertexBuffer9* m_ptr;
		uint32_t m_size;
		VertexDeclHandle m_decl;
		bool m_dynamic;
	};

	struct VertexDeclaration
	{
		void create(const VertexDecl& _decl);

		void destroy()
		{
			DX_RELEASE(m_ptr, 0);
		}

		IDirect3DVertexDeclaration9* m_ptr;
		VertexDecl m_decl;
	};

	struct Shader
	{
		Shader()
			: m_ptr(NULL)
		{
		}

		void create(bool _fragment, const Memory* _mem);
		DWORD* getShaderCode(uint8_t _fragmentBit, const Memory* _mem);

		void destroy()
		{
			ConstantBuffer::destroy(m_constantBuffer);
			m_constantBuffer = NULL;
			m_numPredefined = 0;

			DX_RELEASE(m_ptr, 0);
		}

		IUnknown* m_ptr;
		ConstantBuffer* m_constantBuffer;
		PredefinedUniform m_predefined[PredefinedUniform::Count];
		uint8_t m_numPredefined;
	};

	struct Material
	{
		void create(const Shader& _vsh, const Shader& _fsh)
		{
			BX_CHECK(NULL != _vsh.m_ptr, "Vertex shader doesn't exist.");
			m_vsh = &_vsh;

			BX_CHECK(NULL != _fsh.m_ptr, "Fragment shader doesn't exist.");
			m_fsh = &_fsh;

			memcpy(&m_predefined[0], _vsh.m_predefined, _vsh.m_numPredefined*sizeof(PredefinedUniform) );
			memcpy(&m_predefined[_vsh.m_numPredefined], _fsh.m_predefined, _fsh.m_numPredefined*sizeof(PredefinedUniform) );
			m_numPredefined = _vsh.m_numPredefined + _fsh.m_numPredefined;
		}

		void destroy()
		{
			m_numPredefined = 0;
			m_vsh = NULL;
			m_fsh = NULL;
		}

		const Shader* m_vsh;
		const Shader* m_fsh;

		PredefinedUniform m_predefined[PredefinedUniform::Count*2];
		uint8_t m_numPredefined;
	};

	struct Texture
	{
		enum Enum
		{
			Texture2D,
			Texture3D,
			TextureCube,
		};

		Texture()
			: m_ptr(NULL)
		{
		}

		void createTexture(uint32_t _width, uint32_t _height, uint8_t _numMips, D3DFORMAT _fmt);
		void createVolumeTexture(uint32_t _width, uint32_t _height, uint32_t _depth, uint32_t _numMips, D3DFORMAT _fmt);
		void createCubeTexture(uint32_t _edge, uint32_t _numMips, D3DFORMAT _fmt);

		uint8_t* lock(uint8_t _side, uint8_t _lod, uint32_t& _pitch, uint32_t& _slicePitch);
		void unlock(uint8_t _side, uint8_t _lod);

		void create(const Memory* _mem, uint32_t _flags);

		void destroy()
		{
			DX_RELEASE(m_ptr, 0);
		}

		void commit(uint8_t _stage);
	
		IDirect3DBaseTexture9* m_ptr;
		D3DTEXTUREFILTERTYPE m_minFilter;
		D3DTEXTUREFILTERTYPE m_magFilter;
		D3DTEXTUREFILTERTYPE m_mipFilter;
		D3DTEXTUREADDRESS m_tau;
		D3DTEXTUREADDRESS m_tav;
		D3DTEXTUREADDRESS m_taw;
		Enum m_type;
		bool m_srgb;
	};

	struct RenderTarget
	{
		RenderTarget()
			: m_rt(NULL)
			, m_colorTexture(NULL)
			, m_color(NULL)
			, m_depthTexture(NULL)
			, m_depth(NULL)
			, m_minFilter(D3DTEXF_LINEAR)
			, m_magFilter(D3DTEXF_LINEAR)
			, m_width(0)
			, m_height(0)
			, m_flags(0)
			, m_depthOnly(false)
		{
		}

		void create(uint16_t _width, uint16_t _height, uint32_t _flags, uint32_t _textureFlags);
		void createTextures();
		void destroyTextures();

		void destroy()
		{
			destroyTextures();
			m_flags = 0;
		}

		void preReset()
		{
			destroyTextures();
		}

		void postReset()
		{
			createTextures();
		}

		void commit(uint8_t _stage);
		void resolve();

		Msaa m_msaa;
		IDirect3DSurface9* m_rt;
		IDirect3DTexture9* m_colorTexture;
		IDirect3DSurface9* m_color;
		IDirect3DTexture9* m_depthTexture;
		IDirect3DSurface9* m_depth;
		D3DTEXTUREFILTERTYPE m_minFilter;
		D3DTEXTUREFILTERTYPE m_magFilter;
		uint16_t m_width;
		uint16_t m_height;
		uint32_t m_flags;
		bool m_depthOnly;
	};

} // namespace bgfx

#endif // __RENDERER_D3D9_H__
