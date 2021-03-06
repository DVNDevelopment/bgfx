/*
 * Copyright 2011-2012 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "glsl_optimizer.h"

#define MAX_TAGS 256
extern "C" {
#	include <fpp.h>
} // extern "C"

#if 0
#	define BX_TRACE(_format, ...) fprintf(stderr, "" _format "\n", ##__VA_ARGS__)
#endif // DEBUG

#define BX_NAMESPACE 1
#include <bx/bx.h>

#if BX_PLATFORM_LINUX
#	define _stricmp strcasecmp
#endif // BX_PLATFORM_LINUX

#include <bx/commandline.h>
#include <bx/countof.h>
#include <bx/endian.h>
#include <bx/uint32_t.h>

#if BX_PLATFORM_WINDOWS
#	include <d3dx9.h>
#endif // BX_PLATFORM_WINDOWS

long int fsize(FILE* _file)
{
	long int pos = ftell(_file);
	fseek(_file, 0L, SEEK_END);
	long int size = ftell(_file);
	fseek(_file, pos, SEEK_SET);
	return size;
}

struct ConstantType
{
	enum Enum
	{
		Uniform1i,
		Uniform1f,
		End,

		Uniform1iv,
		Uniform1fv,
		Uniform2fv,
		Uniform3fv,
		Uniform4fv,
		Uniform3x3fv,
		Uniform4x4fv,

		Count,
		TypeMask = 0x7f,
		FragmentBit = 0x80
	};
};

static const char* s_constantTypeName[ConstantType::Count] =
{
	"int",
	"float",
	NULL,
	"int",
	"float",
	"float2",
	"float3",
	"float4",
	"float3x3",
	"float4x4",
};

struct Uniform
{
	std::string name;
	ConstantType::Enum type;
	uint8_t num;
	uint16_t regIndex;
	uint16_t regCount;
};
typedef std::vector<Uniform> UniformArray;

#if BX_PLATFORM_WINDOWS
struct ConstRemap
{
	ConstantType::Enum id;
	D3DXPARAMETER_CLASS paramClass;
	D3DXPARAMETER_TYPE paramType;
	uint32_t paramBytes;
};

static const ConstRemap s_constRemap[7] =
{
	{ ConstantType::Uniform1iv, D3DXPC_SCALAR, D3DXPT_INT, 4 },
	{ ConstantType::Uniform1fv, D3DXPC_SCALAR, D3DXPT_FLOAT, 4 },
	{ ConstantType::Uniform2fv, D3DXPC_VECTOR, D3DXPT_FLOAT, 8 },
	{ ConstantType::Uniform3fv, D3DXPC_VECTOR, D3DXPT_FLOAT, 12 },
	{ ConstantType::Uniform4fv, D3DXPC_VECTOR, D3DXPT_FLOAT, 16 },
	{ ConstantType::Uniform3x3fv, D3DXPC_MATRIX_COLUMNS, D3DXPT_FLOAT, 36 },
	{ ConstantType::Uniform4x4fv, D3DXPC_MATRIX_COLUMNS, D3DXPT_FLOAT, 64 },
};

ConstantType::Enum findConstantType(const D3DXCONSTANT_DESC& constDesc)
{
	uint32_t count = sizeof(s_constRemap)/sizeof(ConstRemap);
	for (uint32_t ii = 0; ii < count; ++ii)
	{
		const ConstRemap& remap = s_constRemap[ii];

		if (remap.paramClass == constDesc.Class
		&&  remap.paramType == constDesc.Type
		&&  (constDesc.Bytes%remap.paramBytes) == 0)
		{
			return remap.id;
		}
	}

	return ConstantType::Count;
}

static uint32_t s_optimizationLevel[4] =
{
	D3DXSHADER_OPTIMIZATION_LEVEL0,
	D3DXSHADER_OPTIMIZATION_LEVEL1,
	D3DXSHADER_OPTIMIZATION_LEVEL2,
	D3DXSHADER_OPTIMIZATION_LEVEL3,
};
#endif // BX_PLATFORM_WINDOWS

class Stream
{
public:
	Stream(FILE* _file, bool _bigEndian = false)
		: m_file(_file)
		, m_bigEndian(_bigEndian)
	{
	}

	~Stream()
	{
	}

	void close()
	{
		m_file = NULL;
	}

	void writef(const char* _format, ...)
	{
		if (NULL != m_file)
		{
			va_list argList;
			va_start(argList, _format);

			char temp[2048];
			int len = vsnprintf(temp, sizeof(temp), _format, argList);
			fwrite(temp, len, 1, m_file);

			va_end(argList);
		}
	}

	void write(const char* _str)
	{
		if (NULL != m_file)
		{
			fwrite(_str, strlen(_str), 1, m_file);
		}
	}

	void write(const void* _data, size_t _size)
	{
		if (NULL != m_file)
		{
			fwrite(_data, _size, 1, m_file);
		}
	}

	template<typename Ty>
	void write(Ty _value)
	{
		Ty temp = m_bigEndian ? bx::bigEndian<Ty>(_value) : _value;
		write(&temp, sizeof(Ty) );
	}

private:
	FILE* m_file;
	bool m_bigEndian;
};

class LineReader
{
public:
	LineReader(const char* _str)
		: m_str(_str)
		, m_pos(0)
		, m_size( (uint32_t)strlen(_str) )
	{
	}

	std::string getLine()
	{
		const char* str = &m_str[m_pos];
		skipLine();

		const char* eol = &m_str[m_pos];

		std::string tmp;
		tmp.assign(str, eol-str);
		return tmp;
	}

	bool isEof() const
	{
		return m_str[m_pos] == '\0';
	}

private:
	void skipLine()
	{
		const char* str = &m_str[m_pos];
		const char* eol = strstr(str, "\n\r");
		if (NULL != eol)
		{
			m_pos += eol-str+2;
			return;
		}

		eol = strstr(str, "\n");
		if (NULL != eol)
		{
			m_pos += eol-str+1;
			return;
		}

		m_pos += (uint32_t)strlen(str);
	}

	const char* m_str;
	uint32_t m_pos;
	uint32_t m_size;
};

void printCode(const char* _code)
{
	fprintf(stderr, "Code:\n---\n");

	LineReader lr(_code);
	for (uint32_t line =  1; !lr.isEof(); ++line)
	{
		fprintf(stderr, "%3d: %s", line, lr.getLine().c_str() );
	}

	fprintf(stderr, "---\n");
}

bool compileGLSLShader(CommandLine& _cmdLine, const std::string& _code, const char* _outFilePath)
{
	const glslopt_shader_type type = tolower(_cmdLine.findOption('\0', "type")[0]) == 'f' ? kGlslOptShaderFragment : kGlslOptShaderVertex;

	glslopt_ctx* ctx = glslopt_initialize(false);

	glslopt_shader* shader = glslopt_optimize(ctx, type, _code.c_str(), 0); 

	if( !glslopt_get_status(shader) )
	{
		printCode(_code.c_str() );
		fprintf(stderr, "Error: %s\n", glslopt_get_log(shader) );
		glslopt_cleanup(ctx);
		return false;
	}

	const char* optimizedShader = glslopt_get_output(shader);

	FILE* out = fopen(_outFilePath, "wb");
	if (NULL == out)
	{
		fprintf(stderr, "Unable to open output file '%s'.", _outFilePath);
		glslopt_cleanup(ctx);
		return false;
	}

	Stream stream(out);

	const char* profile = _cmdLine.findOption('p');
	if (NULL == profile)
	{
		stream.write("#ifdef GL_ES\n");
		stream.write("precision highp float;\n");
		stream.write("#endif // GL_ES\n\n");
	}
	else
	{
		stream.writef("#version %s\n\n", profile);
	}

	stream.write(optimizedShader, strlen(optimizedShader) );
	uint8_t nul = 0;
	stream.write(nul);
	stream.close();
	fclose(out);

	glslopt_cleanup(ctx);

	return true;
}

bool compileHLSLShader(CommandLine& _cmdLine, const std::string& _code, const char* _outFilePath)
{
#if BX_PLATFORM_WINDOWS
	const char* profile = _cmdLine.findOption('p');
	if (NULL == profile)
	{
		printf("Shader profile must be specified.\n");
		return false;
	}

	bool bigEndian = _cmdLine.hasArg('\0', "xbox360");

	uint32_t flags = 0;
	flags |= _cmdLine.hasArg('\0', "debug") ? D3DXSHADER_DEBUG : 0;
	flags |= _cmdLine.hasArg('\0', "avoid-flow-control") ? D3DXSHADER_AVOID_FLOW_CONTROL : 0;
	flags |= _cmdLine.hasArg('\0', "no-preshader") ? D3DXSHADER_NO_PRESHADER : 0;
	flags |= _cmdLine.hasArg('\0', "partial-precision") ? D3DXSHADER_PARTIALPRECISION : 0;
	flags |= _cmdLine.hasArg('\0', "prefer-flow-control") ? D3DXSHADER_PREFER_FLOW_CONTROL : 0;
	flags |= _cmdLine.hasArg('\0', "backwards-compatibility") ? D3DXSHADER_ENABLE_BACKWARDS_COMPATIBILITY : 0;
	
	bool werror = _cmdLine.hasArg('\0', "Werror");

	uint32_t optimization = 3;
	if (_cmdLine.hasArg(optimization, 'O') )
	{
		optimization = bx::uint32_min(optimization, countof(s_optimizationLevel)-1);
		flags |= s_optimizationLevel[optimization];
	}
	else
	{
		flags |= D3DXSHADER_SKIPOPTIMIZATION;
	}

	BX_TRACE("Profile: %s", profile);
	BX_TRACE("Flags: 0x%08x", flags);
	BX_TRACE("Big Endian: %s", bigEndian?"true":"false");

	LPD3DXBUFFER code;
	LPD3DXBUFFER errorMsg;
	LPD3DXCONSTANTTABLE constantTable;

	HRESULT hr = D3DXCompileShader(_code.c_str()
		, _code.size()
		, NULL
		, NULL
		, "main"
		, profile
		, flags
		, &code
		, &errorMsg
		, &constantTable
		);
	if (FAILED(hr)
	||  werror && NULL != errorMsg)
	{
		printCode(_code.c_str() );
		fprintf(stderr, "Error: 0x%08x %s\n", hr, errorMsg->GetBufferPointer() );
		return false;
	}

	D3DXCONSTANTTABLE_DESC desc;
	hr = constantTable->GetDesc(&desc);
	if (FAILED(hr) )
	{
		fprintf(stderr, "Error 0x%08x\n", hr);
		return false;
	}

	BX_TRACE("Creator: %s 0x%08x", desc.Creator, desc.Version);
	BX_TRACE("Num constants: %d", desc.Constants);
	BX_TRACE("#   cl ty RxC   S  By Name");

	UniformArray uniforms;

	for (uint32_t ii = 0; ii < desc.Constants; ++ii)
	{
		D3DXHANDLE handle = constantTable->GetConstant(NULL, ii);
		D3DXCONSTANT_DESC constDesc;
		uint32_t count;
		constantTable->GetConstantDesc(handle, &constDesc, &count);
		BX_TRACE("%3d %2d %2d [%dx%d] %d %3d %s[%d] c%d (%d)"
			, ii
			, constDesc.Class
			, constDesc.Type
			, constDesc.Rows
			, constDesc.Columns
			, constDesc.StructMembers
			, constDesc.Bytes
			, constDesc.Name
			, constDesc.Elements
			, constDesc.RegisterIndex
			, constDesc.RegisterCount
			);

		ConstantType::Enum type = findConstantType(constDesc);
		if (ConstantType::Count != type)
		{
			Uniform un;
			un.name = '$' == constDesc.Name[0] ? constDesc.Name+1 : constDesc.Name;
			un.type = type;
			un.num = constDesc.Elements;
			un.regIndex = constDesc.RegisterIndex;
			un.regCount = constDesc.RegisterCount;
			uniforms.push_back(un);
		}
	}

	FILE* out = fopen(_outFilePath, "wb");
	if (NULL == out)
	{
		fprintf(stderr, "Unable to open output file '%s'.", _outFilePath);
		return false;
	}

	Stream stream(out, bigEndian);

	uint16_t count = (uint16_t)uniforms.size();
	stream.write(count);

	uint32_t fragmentBit = profile[0] == 'p' ? ConstantType::FragmentBit : 0;
	for (UniformArray::const_iterator it = uniforms.begin(); it != uniforms.end(); ++it)
	{
		const Uniform& un = *it;
		uint8_t nameSize = (uint8_t)un.name.size();
		stream.write(nameSize);
		stream.write(un.name.c_str(), nameSize);
		stream.write<uint8_t>(un.type|fragmentBit);
		stream.write(un.num);
		stream.write(un.regIndex);
		stream.write(un.regCount);

		BX_TRACE("%s, %s, %d, %d, %d"
			, un.name.c_str()
			, s_constantTypeName[un.type]
			, un.num
			, un.regIndex
			, un.regCount
			);
	}

	uint16_t shaderSize = (uint16_t)code->GetBufferSize();
	stream.write(shaderSize);
	stream.write(code->GetBufferPointer(), shaderSize);
	uint8_t nul = 0;
	stream.write(nul);
	stream.close();
	fclose(out);

	if (NULL != code)
	{
		code->Release();
	}

	if (NULL != errorMsg)
	{
		errorMsg->Release();
	}

	if (NULL != constantTable)
	{
		constantTable->Release();
	}

	return true;
#else
	fprintf(stderr, "HLSL compiler is not supported on this platform.\n");
	return false;
#endif // BX_PLATFORM_WINDOWS
}

struct Preprocessor
{
	Preprocessor(const char* _filePath)
		: m_tagptr(m_tags)
		, m_scratchPos(0)
		, m_fgetsPos(0)
	{
		m_filePath = scratch(_filePath);

		m_tagptr->tag = FPPTAG_USERDATA;
		m_tagptr->data = this;
		m_tagptr++;

		m_tagptr->tag = FPPTAG_DEPENDS;
		m_tagptr->data = (void*)fppDepends;
		m_tagptr++; 

		m_tagptr->tag = FPPTAG_INPUT;
		m_tagptr->data = (void*)fppInput;
		m_tagptr++; 

		m_tagptr->tag = FPPTAG_OUTPUT;
		m_tagptr->data = (void*)fppOutput;
		m_tagptr++;

		m_tagptr->tag = FPPTAG_ERROR;
		m_tagptr->data = (void*)fppError;
		m_tagptr++;

		m_tagptr->tag = FPPTAG_IGNOREVERSION;
		m_tagptr->data = (void*)0;
		m_tagptr++;

		m_tagptr->tag = FPPTAG_LINE;
		m_tagptr->data = (void*)0;
		m_tagptr++;

		m_tagptr->tag = FPPTAG_INPUT_NAME;
		m_tagptr->data = m_filePath;
		m_tagptr++;

		m_default = "#define lowp\n#define mediump\n#define highp\n";
	}

	void setDefine(const char* _define)
	{
		m_tagptr->tag = FPPTAG_DEFINE;
		m_tagptr->data = scratch(_define);
		m_tagptr++;
	}

	void setDefaultDefine(const char* _name)
	{
		char temp[1024];
		_snprintf(temp, countof(temp)
			, "#ifndef %s\n"
			  "#	define %s 0\n"
			  "#endif // %s\n"
			  "\n"
			, _name
			, _name
			, _name
			);

		m_default += temp;
	}

	void addDependency(const char* _fileName)
	{
		m_depends += " \\\n ";
		m_depends += _fileName;
	}

	bool run()
	{
		m_fgetsPos = 0;

		FILE* file = fopen(m_filePath, "r");
		if (NULL == file)
		{
			return false;
		}

		long int size = fsize(file);
		char* input = new char[size+1];
		size = fread(input, 1, size, file);
		input[size] = '\0';
		fclose(file);

		m_input = m_default;
		m_input += input;

		fppTag* tagptr = m_tagptr;

		tagptr->tag = FPPTAG_END;
		tagptr->data = 0;
		tagptr++;

		int result = fppPreProcess(m_tags);

		return 0 == result;
	}

	char* fgets(char* _buffer, int _size)
	{
		int ii = 0;
		for (char ch = m_input[m_fgetsPos]; m_fgetsPos < m_input.size() && ii < _size-1; ch = m_input[++m_fgetsPos])
		{
			_buffer[ii++] = ch;

			if (ch == '\n' || ii == _size)
			{
				_buffer[ii] = '\0';
				m_fgetsPos++;
				return _buffer;
			}
		}

		return NULL;
	}

	static void fppDepends(char* _fileName, void* _userData)
	{
		Preprocessor* thisClass = (Preprocessor*)_userData;
		thisClass->addDependency(_fileName);
	}

	static char* fppInput(char* _buffer, int _size, void* _userData)
	{
		Preprocessor* thisClass = (Preprocessor*)_userData;
		return thisClass->fgets(_buffer, _size);
	}

	static void fppOutput(int _ch, void* _userData)
	{
		Preprocessor* thisClass = (Preprocessor*)_userData;
		thisClass->m_preprocessed += _ch;
	}

	static void fppError(void* _userData, char* _format, va_list _vargs)
	{
		vfprintf(stderr, _format, _vargs);
	}

	char* scratch(const char* _str)
	{
		char* result = &m_scratch[m_scratchPos];
		strcpy(result, _str);
		m_scratchPos += strlen(_str)+1;

		return result;
	}

	fppTag m_tags[MAX_TAGS];
	fppTag* m_tagptr;

	char* m_filePath;
	std::string m_depends;
	std::string m_default;
	std::string m_input;
	std::string m_preprocessed;
	char m_scratch[16<<10];
	uint32_t m_scratchPos;
	uint32_t m_fgetsPos;
};

// OpenGL #version Features Direct3D Features Shader Model
// 2.1    120      vf       9.0      vf       2.0
// 3.0    130
// 3.1    140
// 3.2    150      vgf
// 3.3    330               10.0     vgf      4.0
// 4.0    400      vhdgf
// 4.1    410
// 4.2    420               11.0     vhdgf    5.0

int main(int _argc, const char* _argv[])
{
	CommandLine cmdLine(_argc, _argv);

	const char* filePath = cmdLine.findOption('f');
	if (NULL == filePath)
	{
		fprintf(stderr, "Shader file name must be specified.\n");
		return EXIT_FAILURE;
	}

	const char* outFilePath = cmdLine.findOption('o');
	if (NULL == outFilePath)
	{
		fprintf(stderr, "Output file name must be specified.\n");
		return EXIT_FAILURE;
	}

	const char* type = cmdLine.findOption('\0', "type");
	if (NULL == type)
	{
		fprintf(stderr, "Must specify shader type.");
		return EXIT_FAILURE;
	}

	const char* platform = cmdLine.findOption('\0', "platform");
	if (NULL == platform)
	{
		fprintf(stderr, "Must specify platform.\n");
		return EXIT_FAILURE;
	}

	bool depends = cmdLine.hasArg("depends");
	bool preprocessOnly = cmdLine.hasArg("preprocess");

	Preprocessor preprocessor(filePath);

	preprocessor.setDefaultDefine("BX_PLATFORM_ANDROID");
	preprocessor.setDefaultDefine("BX_PLATFORM_IOS");
	preprocessor.setDefaultDefine("BX_PLATFORM_LINUX");
	preprocessor.setDefaultDefine("BX_PLATFORM_NACL");
	preprocessor.setDefaultDefine("BX_PLATFORM_OSX");
	preprocessor.setDefaultDefine("BX_PLATFORM_WINDOWS");
	preprocessor.setDefaultDefine("BX_PLATFORM_XBOX360");
	preprocessor.setDefaultDefine("BGFX_SHADER_LANGUAGE_GLSL");
	preprocessor.setDefaultDefine("BGFX_SHADER_LANGUAGE_HLSL");
	preprocessor.setDefaultDefine("BGFX_SHADER_TYPE_FRAGMENT");
	preprocessor.setDefaultDefine("BGFX_SHADER_TYPE_VERTEX");

	bool glsl = false;

	if (0 == _stricmp(platform, "android") )
	{
		preprocessor.setDefine("BX_PLATFORM_ANDROID=1");
		preprocessor.setDefine("BGFX_SHADER_LANGUAGE_GLSL=1");
		glsl = true;
	}
	else if (0 == _stricmp(platform, "ios") )
	{
		preprocessor.setDefine("BX_PLATFORM_IOS=1");
		preprocessor.setDefine("BGFX_SHADER_LANGUAGE_GLSL=1");
		glsl = true;
	}
	else if (0 == _stricmp(platform, "linux") )
	{
		preprocessor.setDefine("BX_PLATFORM_IOS=1");
		preprocessor.setDefine("BGFX_SHADER_LANGUAGE_GLSL=1");
		glsl = true;
	}
	else if (0 == _stricmp(platform, "nacl") )
	{
		preprocessor.setDefine("BX_PLATFORM_NACL=1");
		preprocessor.setDefine("BGFX_SHADER_LANGUAGE_GLSL=1");
		glsl = true;
	}
	else if (0 == _stricmp(platform, "osx") )
	{
		preprocessor.setDefine("BX_PLATFORM_OSX=1");
		preprocessor.setDefine("BGFX_SHADER_LANGUAGE_GLSL=1");
		glsl = true;
	}
	else if (0 == _stricmp(platform, "windows") )
	{
		preprocessor.setDefine("BX_PLATFORM_WINDOWS=1");
		preprocessor.setDefine("BGFX_SHADER_LANGUAGE_HLSL=1");
	}
	else if (0 == _stricmp(platform, "xbox360") )
	{
		preprocessor.setDefine("BX_PLATFORM_XBOX360=1");
		preprocessor.setDefine("BGFX_SHADER_LANGUAGE_HLSL=1");
	}
	else
	{
		fprintf(stderr, "Unknown platform %s?!", platform);
		return EXIT_FAILURE;
	}

	switch (tolower(type[0]) )
	{
	case 'f':
		preprocessor.setDefine("BGFX_SHADER_TYPE_FRAGMENT=1");
		break;

	case 'v':
		preprocessor.setDefine("BGFX_SHADER_TYPE_VERTEX=1");
		break;

	default:
		fprintf(stderr, "Unknown type: %s?!", type);
		return EXIT_FAILURE;
	}

	if (preprocessor.run() )
	{
		BX_TRACE("Input file: %s", filePath);
		BX_TRACE("Output file: %s", outFilePath);

		if (preprocessOnly)
		{
			FILE* out = fopen(outFilePath, "wb");
			if (NULL == out)
			{
				fprintf(stderr, "Unable to open output file '%s'.", outFilePath);
				return false;
			}

			Stream stream(out);
			if (glsl)
			{
				const char* profile = cmdLine.findOption('p');
				if (NULL == profile)
				{
					stream.write("#ifdef GL_ES\n");
					stream.write("precision highp float;\n");
					stream.write("#endif // GL_ES\n\n");
				}
				else
				{
					stream.writef("#version %s\n\n", profile);
				}
			}
			stream.write(preprocessor.m_preprocessed.c_str(), preprocessor.m_preprocessed.size() );
			stream.close();
			fclose(out);

			return EXIT_SUCCESS;
		}

		bool compiled = false;

		if (glsl)
		{
			compiled = compileGLSLShader(cmdLine, preprocessor.m_preprocessed, outFilePath);
		}
		else
		{
			compiled = compileHLSLShader(cmdLine, preprocessor.m_preprocessed, outFilePath);
		}

		if (compiled
		&&  depends)
		{
			std::string ofp = outFilePath;
			ofp += ".d";
			FILE* out = fopen(ofp.c_str(), "wb");
			if (NULL != out)
			{
				Stream stream(out);
				stream.write(outFilePath);
				stream.write(":");
				stream.write(preprocessor.m_depends.c_str() );
				stream.write("\n");
				stream.close();
				fclose(out);
			}
			return EXIT_SUCCESS;
		}
	}

	fprintf(stderr, "Failed to build shader.\n");
	return EXIT_FAILURE;
}
