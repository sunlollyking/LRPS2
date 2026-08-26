/* *	Copyright (C) 2011-2014 Gregory hainaut
 *	Copyright (C) 2007-2009 Gabest
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <unordered_set>

#include "../../stdafx.h"
#include "GLLoader.h"
#include "../../GS.h"
#include "options_tools.h"

#ifdef __unix__
PFNGLBLENDFUNCSEPARATEPROC             glBlendFuncSeparate                 = NULL;
#endif

#include "PFN_GLLOADER_CPP.h"

namespace GLExtension {

	static std::unordered_set<std::string> s_extensions;

	bool Has(const std::string& ext)
	{
		return !!s_extensions.count(ext);
	}

	void Set(const std::string& ext, bool v)
	{
		if (v)
			s_extensions.insert(ext);
		else
			s_extensions.erase(ext);
	}
}

namespace ReplaceGL
{
	void APIENTRY ScissorIndexed(GLuint index, GLint left, GLint bottom, GLsizei width, GLsizei height)
	{
		glScissor(left, bottom, width, height);
	}

	void APIENTRY ViewportIndexedf(GLuint index, GLfloat x, GLfloat y, GLfloat w, GLfloat h)
	{
		glViewport(GLint(x), GLint(y), GLsizei(w), GLsizei(h));
	}

	void APIENTRY TextureBarrier()
	{
	}

}

// Was Windows-only, for Intel iGPUs missing GL_ARB_direct_state_access. OpenGL
// ES has no DSA at all, so every target that lacks it now shares this path.
namespace Emulate_DSA
{
	// Texture entry point
	void APIENTRY BindTextureUnit(GLuint unit, GLuint texture) {
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, texture);
	}

	void APIENTRY CreateTexture(GLenum target, GLsizei n, GLuint *textures) {
		glGenTextures(1, textures);
	}

	void APIENTRY TextureStorage(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) {
		BindTextureUnit(7, texture);
		glTexStorage2D(GL_TEXTURE_2D, levels, internalformat, width, height);
	}

	void APIENTRY TextureSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) {
		BindTextureUnit(7, texture);
		glTexSubImage2D(GL_TEXTURE_2D, level, xoffset, yoffset, width, height, format, type, pixels);
	}

	void APIENTRY CopyTextureSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) {
		BindTextureUnit(7, texture);
		glCopyTexSubImage2D(GL_TEXTURE_2D, level, xoffset, yoffset, x, y, width, height);
	}

#ifndef HAVE_GLES
	// glGetTexImage does not exist on OpenGL ES; a readback there has to go
	// through an FBO and glReadPixels. Nothing in the renderer calls this --
	// it is here only to complete the DSA table -- so rather than write a
	// readback path with no caller, leave the pointer alone on ES and let the
	// link fail loudly if one ever appears.
	void APIENTRY GetTexureImage(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei bufSize, void *pixels) {
		BindTextureUnit(7, texture);
		glGetTexImage(GL_TEXTURE_2D, level, format, type, pixels);
	}
#endif

	void APIENTRY TextureParameteri (GLuint texture, GLenum pname, GLint param) {
		BindTextureUnit(7, texture);
		glTexParameteri(GL_TEXTURE_2D, pname, param);
	}

	void APIENTRY GenerateTextureMipmap(GLuint texture) {
		BindTextureUnit(7, texture);
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	// Misc entry point
	// (only purpose is to have a consistent API otherwise it is useless)
	void APIENTRY CreateProgramPipelines(GLsizei n, GLuint *pipelines) {
		glGenProgramPipelines(n, pipelines);
	}

	void APIENTRY CreateSamplers(GLsizei n, GLuint *samplers) {
		glGenSamplers(n, samplers);
	}

	// Replace function pointer to emulate DSA (Direct State Access) behavior
	void Init(void)
	{
		log_cb(RETRO_LOG_WARN, "DSA (Direct State Access) is not supported. Expect slower performance\n");
		glBindTextureUnit             = BindTextureUnit;
		glCreateTextures              = CreateTexture;
		glTextureStorage2D            = TextureStorage;
		glTextureSubImage2D           = TextureSubImage;
		glCopyTextureSubImage2D       = CopyTextureSubImage;
#ifndef HAVE_GLES
		glGetTextureImage             = GetTexureImage;
#endif
		glTextureParameteri           = TextureParameteri;
		// Defined above but never wired up, so a DSA-less target called through
		// a null pointer the moment a texture wanted mipmaps.
		glGenerateTextureMipmap       = GenerateTextureMipmap;

		glCreateProgramPipelines      = CreateProgramPipelines;
		glCreateSamplers              = CreateSamplers;
	}
}

namespace GLLoader {
	bool amd_legacy_buggy_driver                   = false;
	bool vendor_id_amd                             = false;
	bool vendor_id_nvidia                          = false;
	bool vendor_id_intel                           = false;
	bool mesa_driver                               = false;
	bool buggy_sso_dual_src                        = false;

	/* OpenGL ES */
	bool found_GL_EXT_buffer_storage               = false;
	bool found_GL_EXT_blend_func_extended          = false;
	bool found_GL_EXT_geometry_point_size          = false;

	/* Desktop OpenGL */
	bool found_gl_version_3_3                      = false;
	bool found_gl_version_4_0                      = false;
	bool found_gl_version_4_1                      = false;
	bool found_gl_version_4_2                      = false;
	bool found_gl_version_4_3                      = false;
	bool found_gl_version_4_4                      = false;
	bool found_gl_version_4_5                      = false;
	bool found_gl_version_4_6                      = false;

	bool found_GL_ARB_buffer_storage               = false;
	bool found_geometry_shader                     = true; /* We require at least OpenGL 3.3 so geometry must be supported by default */
	bool found_GL_ARB_clear_texture                = false;

	/* DirectX11 class GPU for OpenGL */
	bool found_GL_ARB_gpu_shader5                  = false; /* Require Ivy Bridge */
	bool found_GL_ARB_shader_image_load_store      = false; /* Intel IB. Nvidia/AMD miss Mesa implementation. */

#ifdef HAVE_GLES
	/* Every ARB extension this renderer asks for by name, and how the same
	 * functionality is spelled on OpenGL ES. Nothing here is reimplemented --
	 * the entry points come from get_proc_address either way, and the enum
	 * values are shared. Only the names the driver advertises differ.
	 *
	 * An empty alternatives list means the feature is core in ES 3.2, which is
	 * the context this build asks for, so the check is simply satisfied. */
	struct gles_alias
	{
		const char* arb;
		const char* alternatives[3];
	};

	static const gles_alias gles_aliases[] = {
		{"GL_ARB_separate_shader_objects",   {"GL_EXT_separate_shader_objects", nullptr, nullptr}},
		{"GL_ARB_shading_language_420pack",  {nullptr, nullptr, nullptr}},
		{"GL_ARB_texture_storage",           {"GL_EXT_texture_storage", nullptr, nullptr}},
		{"GL_ARB_buffer_storage",            {"GL_EXT_buffer_storage", nullptr, nullptr}},
		{"GL_ARB_copy_image",                {"GL_EXT_copy_image", "GL_OES_copy_image", nullptr}},
		{"GL_ARB_clip_control",              {"GL_EXT_clip_control", nullptr, nullptr}},
		{"GL_ARB_gpu_shader5",               {"GL_EXT_gpu_shader5", "GL_OES_gpu_shader5", nullptr}},
		{"GL_ARB_shader_image_load_store",   {nullptr, nullptr, nullptr}},
		{"GL_ARB_clear_texture",             {"GL_EXT_clear_texture", nullptr, nullptr}},
		{"GL_ARB_texture_barrier",           {"GL_NV_texture_barrier", nullptr, nullptr}},
		{"GL_ARB_viewport_array",            {"GL_OES_viewport_array", nullptr, nullptr}},
		{"GL_ARB_texture_buffer_object",     {"GL_EXT_texture_buffer", "GL_OES_texture_buffer", nullptr}},
	};

	/* No GLES equivalent exists for direct state access, so it is deliberately
	 * absent from the table above: the lookup fails, and Emulate_DSA takes
	 * over. */
	static bool has_extension(const std::string& ext)
	{
		if (GLExtension::Has(ext))
			return true;

		for (const gles_alias& alias : gles_aliases)
		{
			if (ext != alias.arb)
				continue;

			bool core_in_es32 = true;
			for (const char* alt : alias.alternatives)
			{
				if (!alt)
					continue;
				core_in_es32 = false;
				if (GLExtension::Has(alt))
					return true;
			}
			return core_in_es32;
		}
		return false;
	}
#else
	static bool has_extension(const std::string& ext) { return GLExtension::Has(ext); }
#endif

	static bool mandatory(const std::string& ext)
	{
		bool found = has_extension(ext);
		if (!found) /* TODO/FIXME - return error */
			log_cb(RETRO_LOG_ERROR, "ERROR: %s is NOT SUPPORTED\n", ext.c_str());
		return found;
	}

	static bool optional(const std::string& name)
	{
		bool found = has_extension(name);
		if (!found)
			log_cb(RETRO_LOG_WARN, "INFO: %s is NOT SUPPORTED\n", name.c_str());
		return found;
	}

	bool check_gl_version(int major, int minor)
	{
		GLint major_gl     = 0;
		GLint minor_gl     = 0;
		const char* vendor = (const char*)glGetString(GL_VENDOR);

		if (strstr(vendor, "Advanced Micro Devices") || strstr(vendor, "ATI Technologies Inc.") || strstr(vendor, "ATI"))
			vendor_id_amd = true;
		else if (strstr(vendor, "NVIDIA Corporation"))
			vendor_id_nvidia = true;
#ifdef _WIN32
		else if (strstr(vendor, "Intel"))
			vendor_id_intel = true;
#else
		// On linux assumes the free driver if it isn't nvidia or amd pro driver
		mesa_driver        = !vendor_id_nvidia && !vendor_id_amd;
#endif
		// As of 2019 SSO is still broken on intel (Kaby Lake confirmed).
		buggy_sso_dual_src = vendor_id_intel || vendor_id_amd;

		glGetIntegerv(GL_MAJOR_VERSION, &major_gl);
		glGetIntegerv(GL_MINOR_VERSION, &minor_gl);
		if ((major_gl < major) || (major_gl == major && minor_gl < minor))
		{
			log_cb(RETRO_LOG_ERROR, "OpenGL %d.%d is not supported. Only OpenGL %d.%d\n was found\n", major, minor, major_gl, minor_gl);
			return false;
		}

		found_gl_version_3_3 = true;

		if (major_gl == 4)
		{
			if (minor_gl >= 0)
				found_gl_version_4_0 = true;
			if (minor_gl >= 1)
				found_gl_version_4_1 = true;
			if (minor_gl >= 2)
				found_gl_version_4_2 = true;
			if (minor_gl >= 3)
				found_gl_version_4_3 = true;
			if (minor_gl >= 4)
				found_gl_version_4_4 = true;
			if (minor_gl >= 5)
				found_gl_version_4_5 = true;
			if (minor_gl == 6)
				found_gl_version_4_6 = true;
		}

		log_cb(RETRO_LOG_INFO, "OpenGL version supported: %d.%d.\n", major_gl, minor_gl);

		return true;
	}

	bool check_gl_supported_extension(void)
	{
		bool ok = true;
		GLint i;
		int max_ext = 0;
		glGetIntegerv(GL_NUM_EXTENSIONS, &max_ext);
		for (i = 0; i < max_ext; i++)
		{
			std::string ext{(const char*)glGetStringi(GL_EXTENSIONS, i)};
			GLExtension::Set(ext, true);
		}

		/* Mandatory for both OpenGL HW/SW renderers */
		{
			/* OpenGL 4.1 */
			mandatory("GL_ARB_separate_shader_objects");
			/* OpenGL 4.2 */
			mandatory("GL_ARB_shading_language_420pack");
			mandatory("GL_ARB_texture_storage");
			/* TODO/FIXME - can put this behind debug ifdef */
			/* OpenGL 4.3 */
			mandatory("GL_KHR_debug");
			/* OpenGL 4.4 */
			found_GL_ARB_buffer_storage = mandatory("GL_ARB_buffer_storage") && glBufferStorage != nullptr;
		}


		// Only for HW renderer
		if (GetCurrentRendererType() == GSRendererType::OGL_HW)
		{
			// Both are called unconditionally further in, so a missing one is
			// a jump through a null pointer rather than a degraded picture.
			// Report it and let the caller refuse the renderer.
			if (!mandatory("GL_ARB_copy_image") || !glCopyImageSubData)
				ok = false;
			if (!mandatory("GL_ARB_clip_control") || !glClipControl)
				ok = false;
		}

#ifdef HAVE_GLES
		/* OpenGL ES */
		{
			// Alternative for GL_ARB_buffer_storage. Its entry point is
			// glBufferStorageEXT, loaded into the same pointer in GS.cpp, so
			// the pointer is what decides whether it is usable.
			found_GL_EXT_buffer_storage               = optional("GL_EXT_buffer_storage") && glBufferStorage != nullptr;

			// Needed by the shaders rather than by any entry point: the
			// fragment stage uses layout(index = 1) for dual-source blending
			// and the geometry stage redeclares gl_PerVertex with
			// gl_PointSize. Neither is expressible on ES without these, so
			// refuse the renderer rather than let the shaders fail to compile
			// -- nothing checks GL_COMPILE_STATUS, so that failure is silent
			// and shows up as a white screen.
			found_GL_EXT_blend_func_extended          = mandatory("GL_EXT_blend_func_extended");
			found_GL_EXT_geometry_point_size          = mandatory("GL_EXT_geometry_point_size");
			if (!found_GL_EXT_blend_func_extended || !found_GL_EXT_geometry_point_size)
				ok = false;
		}
#endif

		// Extra
		{
			/* OpenGL 4.0 */
			found_GL_ARB_gpu_shader5                  = optional("GL_ARB_gpu_shader5");
			/* OpenGL 4.2 */
			found_GL_ARB_shader_image_load_store      = optional("GL_ARB_shader_image_load_store");
			/* OpenGL 4.4 */
			found_GL_ARB_clear_texture                = optional("GL_ARB_clear_texture") && glClearTexImage != nullptr;
			/* OpenGL 4.5 */
			optional("GL_ARB_direct_state_access");

			/* Mandatory for the advance HW renderer effect. Unfortunately Mesa LLVMPIPE/SWR renderers doesn't support this extension.
			 * Rendering might be corrupted but it could be good enough for test/virtual machine. */
			optional("GL_ARB_texture_barrier");
		}

		if (!has_extension("GL_ARB_viewport_array"))
		{
			glScissorIndexed   = ReplaceGL::ScissorIndexed;
			glViewportIndexedf = ReplaceGL::ViewportIndexedf;
			log_cb(RETRO_LOG_WARN, "GL_ARB_viewport_array is not supported! Function pointer will be replaced\n");
		}

		// The pointer is checked as well as the extension string: on ES the
		// barrier arrives via GL_NV_texture_barrier, whose entry point is
		// glTextureBarrierNV, so the string can be advertised while the
		// unsuffixed pointer is still null. Calling through it would be a jump
		// to zero rather than a missing feature.
		if (!has_extension("GL_ARB_texture_barrier") || !glTextureBarrier)
		{
			glTextureBarrier = ReplaceGL::TextureBarrier;
			log_cb(RETRO_LOG_WARN, "GL_ARB_texture_barrier is not supported! Blending emulation will not be supported\n");
		}

		// Thank you Intel for not providing support of basic features on your iGPUs.
		// OpenGL ES has no direct state access at all, so this is the normal
		// path there rather than a workaround for one vendor.
		if (!has_extension("GL_ARB_direct_state_access"))
			Emulate_DSA::Init();

		return ok;
	}

	bool check_gl_requirements(void)
	{
		// ES tops out at 3.2, and 3.2 is where geometry shaders, texture
		// buffers and image load/store all became core -- everything this
		// renderer needs beyond ES 3.0.
#ifdef HAVE_GLES
		if (!check_gl_version(3, 2))
			return false;
#else
		if (!check_gl_version(3, 3))
			return false;
#endif

		return check_gl_supported_extension();
	}
}
