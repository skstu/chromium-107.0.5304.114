// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/test/test_gles2_interface.h"

#include "base/bind.h"
#include "base/containers/contains.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/scoped_refptr.h"
#include "components/viz/test/test_context_support.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace viz {

static unsigned NextContextId() {
  static uint16_t s_context_id = 1;
  // We need to ensure that the context_id fits in 16 bits since it is placed on
  // the top 16 bits of the 32 bit identifiers (program_id, framebuffer_id,
  // shader_id, etc.) generated by the context.
  if (s_context_id == std::numeric_limits<uint16_t>::max()) {
    LOG(ERROR) << "Exceeded max context id count; wrapping around";
    s_context_id = 1;
  }
  return s_context_id++;
}

TestGLES2Interface::TestGLES2Interface() : context_id_(NextContextId()) {
  // For stream textures.
  set_have_extension_egl_image(true);
  set_max_texture_size(2048);
}

TestGLES2Interface::~TestGLES2Interface() = default;

void TestGLES2Interface::GenTextures(GLsizei n, GLuint* textures) {
  for (int i = 0; i < n; ++i) {
    textures[i] = NextTextureId();
    textures_.insert(textures[i]);
  }
}

void TestGLES2Interface::GenBuffers(GLsizei n, GLuint* buffers) {
  for (int i = 0; i < n; ++i)
    buffers[i] = NextBufferId();
}

void TestGLES2Interface::GenFramebuffers(GLsizei n, GLuint* framebuffers) {
  for (int i = 0; i < n; ++i)
    framebuffers[i] = NextFramebufferId();
}

void TestGLES2Interface::GenRenderbuffers(GLsizei n, GLuint* renderbuffers) {
  for (int i = 0; i < n; ++i)
    renderbuffers[i] = NextRenderbufferId();
}

void TestGLES2Interface::GenQueriesEXT(GLsizei n, GLuint* queries) {
  for (GLsizei i = 0; i < n; ++i) {
    queries[i] = 1u;
  }
}

void TestGLES2Interface::DeleteTextures(GLsizei n, const GLuint* textures) {
  for (int i = 0; i < n; ++i) {
    RetireTextureId(textures[i]);
    textures_.erase(textures[i]);
  }
}

void TestGLES2Interface::DeleteBuffers(GLsizei n, const GLuint* buffers) {
  for (int i = 0; i < n; ++i)
    RetireBufferId(buffers[i]);
}

void TestGLES2Interface::DeleteFramebuffers(GLsizei n,
                                            const GLuint* framebuffers) {
  for (int i = 0; i < n; ++i) {
    if (framebuffers[i]) {
      RetireFramebufferId(framebuffers[i]);
      if (framebuffers[i] == current_framebuffer_)
        current_framebuffer_ = 0;
    }
  }
}

void TestGLES2Interface::DeleteQueriesEXT(GLsizei n, const GLuint* queries) {
}

GLuint TestGLES2Interface::CreateShader(GLenum type) {
  unsigned shader = next_shader_id_++ | context_id_ << 16;
  shader_set_.insert(shader);
  return shader;
}

GLuint TestGLES2Interface::CreateProgram() {
  unsigned program = next_program_id_++ | context_id_ << 16;
  program_set_.insert(program);
  return program;
}

void TestGLES2Interface::BindTexture(GLenum target, GLuint texture) {
  if (times_bind_texture_succeeds_ >= 0) {
    if (!times_bind_texture_succeeds_) {
      LoseContextCHROMIUM(GL_GUILTY_CONTEXT_RESET_ARB,
                          GL_INNOCENT_CONTEXT_RESET_ARB);
    }
    --times_bind_texture_succeeds_;
  }

  if (!texture)
    return;
  DCHECK(base::Contains(textures_, texture));
  used_textures_.insert(texture);
}

void TestGLES2Interface::GetIntegerv(GLenum pname, GLint* params) {
  if (pname == GL_MAX_TEXTURE_SIZE)
    *params = test_capabilities_.max_texture_size;
  else if (pname == GL_ACTIVE_TEXTURE)
    *params = GL_TEXTURE0;
  else if (pname == GL_UNPACK_ALIGNMENT)
    *params = unpack_alignment_;
  else if (pname == GL_FRAMEBUFFER_BINDING)
    *params = current_framebuffer_;
  else if (pname == GL_MAX_SAMPLES)
    *params = test_capabilities_.max_samples;
}

void TestGLES2Interface::GetShaderiv(GLuint shader,
                                     GLenum pname,
                                     GLint* params) {
  if (pname == GL_COMPILE_STATUS)
    *params = 1;
}

void TestGLES2Interface::GetProgramiv(GLuint program,
                                      GLenum pname,
                                      GLint* params) {
  if (pname == GL_LINK_STATUS)
    *params = 1;
}

void TestGLES2Interface::GetShaderPrecisionFormat(GLenum shadertype,
                                                  GLenum precisiontype,
                                                  GLint* range,
                                                  GLint* precision) {
  // Return the minimum precision requirements of the GLES2
  // specification.
  switch (precisiontype) {
    case GL_LOW_INT:
      range[0] = 8;
      range[1] = 8;
      *precision = 0;
      break;
    case GL_MEDIUM_INT:
      range[0] = 10;
      range[1] = 10;
      *precision = 0;
      break;
    case GL_HIGH_INT:
      range[0] = 16;
      range[1] = 16;
      *precision = 0;
      break;
    case GL_LOW_FLOAT:
      range[0] = 8;
      range[1] = 8;
      *precision = 8;
      break;
    case GL_MEDIUM_FLOAT:
      range[0] = 14;
      range[1] = 14;
      *precision = 10;
      break;
    case GL_HIGH_FLOAT:
      range[0] = 62;
      range[1] = 62;
      *precision = 16;
      break;
    default:
      NOTREACHED();
      break;
  }
}

void TestGLES2Interface::UseProgram(GLuint program) {
  if (!program)
    return;
  if (!program_set_.count(program))
    ADD_FAILURE() << "useProgram called on unknown program " << program;
}

GLenum TestGLES2Interface::CheckFramebufferStatus(GLenum target) {
  if (context_lost_)
    return GL_FRAMEBUFFER_UNDEFINED_OES;
  return GL_FRAMEBUFFER_COMPLETE;
}

void TestGLES2Interface::Flush() {
  test_support_->CallAllSyncPointCallbacks();
}

void TestGLES2Interface::Finish() {
  test_support_->CallAllSyncPointCallbacks();
}

void TestGLES2Interface::ShallowFinishCHROMIUM() {
  test_support_->CallAllSyncPointCallbacks();
}

void TestGLES2Interface::BindRenderbuffer(GLenum target, GLuint renderbuffer) {
  if (!renderbuffer)
    return;
  if (renderbuffer != 0 &&
      renderbuffer_set_.find(renderbuffer) == renderbuffer_set_.end()) {
    ADD_FAILURE() << "bindRenderbuffer called with unknown renderbuffer";
  } else if ((renderbuffer >> 16) != context_id_) {
    ADD_FAILURE()
        << "bindRenderbuffer called with renderbuffer from other context";
  }
}

void TestGLES2Interface::BindFramebuffer(GLenum target, GLuint framebuffer) {
  if (framebuffer != 0 &&
      framebuffer_set_.find(framebuffer) == framebuffer_set_.end()) {
    ADD_FAILURE() << "bindFramebuffer called with unknown framebuffer";
  } else if (framebuffer != 0 && (framebuffer >> 16) != context_id_) {
    ADD_FAILURE()
        << "bindFramebuffer called with framebuffer from other context";
  } else {
    current_framebuffer_ = framebuffer;
  }
}

void TestGLES2Interface::BindBuffer(GLenum target, GLuint buffer) {
  bound_buffer_[target] = buffer;
  if (!buffer)
    return;
  unsigned context_id = buffer >> 16;
  unsigned buffer_id = buffer & 0xffff;
  DCHECK(buffer_id);
  DCHECK_LT(buffer_id, next_buffer_id_);
  DCHECK_EQ(context_id, context_id_);

  if (buffers_.count(bound_buffer_[target]) == 0)
    buffers_[bound_buffer_[target]] = std::make_unique<Buffer>();

  buffers_[bound_buffer_[target]]->target = target;
}

void TestGLES2Interface::PixelStorei(GLenum pname, GLint param) {
  switch (pname) {
    case GL_UNPACK_ALIGNMENT:
      // Param should be a power of two <= 8.
      EXPECT_EQ(0, param & (param - 1));
      EXPECT_GE(8, param);
      switch (param) {
        case 1:
        case 2:
        case 4:
        case 8:
          unpack_alignment_ = param;
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}

void* TestGLES2Interface::MapBufferCHROMIUM(GLuint target, GLenum access) {
  DCHECK_GT(bound_buffer_.count(target), 0u);
  DCHECK_GT(buffers_.count(bound_buffer_[target]), 0u);
  DCHECK_EQ(target, buffers_[bound_buffer_[target]]->target);
  if (times_map_buffer_chromium_succeeds_ >= 0) {
    if (!times_map_buffer_chromium_succeeds_) {
      return nullptr;
    }
    --times_map_buffer_chromium_succeeds_;
  }

  return buffers_[bound_buffer_[target]]->pixels.get();
}

GLboolean TestGLES2Interface::UnmapBufferCHROMIUM(GLuint target) {
  DCHECK_GT(bound_buffer_.count(target), 0u);
  DCHECK_GT(buffers_.count(bound_buffer_[target]), 0u);
  DCHECK_EQ(target, buffers_[bound_buffer_[target]]->target);
  buffers_[bound_buffer_[target]]->pixels = nullptr;
  return true;
}

void TestGLES2Interface::BufferData(GLenum target,
                                    GLsizeiptr size,
                                    const void* data,
                                    GLenum usage) {
  DCHECK_GT(bound_buffer_.count(target), 0u);
  DCHECK_GT(buffers_.count(bound_buffer_[target]), 0u);
  DCHECK_EQ(target, buffers_[bound_buffer_[target]]->target);
  Buffer* buffer = buffers_[bound_buffer_[target]].get();
  if (context_lost_) {
    buffer->pixels = nullptr;
    return;
  }

  buffer->pixels.reset(new uint8_t[size]);
  buffer->size = size;
  if (data != nullptr)
    memcpy(buffer->pixels.get(), data, size);
}

void TestGLES2Interface::GenSyncTokenCHROMIUM(GLbyte* sync_token) {
  // Don't return a valid sync token if context is lost. This matches behavior
  // of CommandBufferProxyImpl.
  if (context_lost_)
    return;
  gpu::SyncToken sync_token_data(gpu::CommandBufferNamespace::GPU_IO,
                                 gpu::CommandBufferId(),
                                 next_insert_fence_sync_++);
  sync_token_data.SetVerifyFlush();
  memcpy(sync_token, &sync_token_data, sizeof(sync_token_data));
}

void TestGLES2Interface::GenUnverifiedSyncTokenCHROMIUM(GLbyte* sync_token) {
  // Don't return a valid sync token if context is lost. This matches behavior
  // of CommandBufferProxyImpl.
  if (context_lost_)
    return;
  gpu::SyncToken sync_token_data(gpu::CommandBufferNamespace::GPU_IO,
                                 gpu::CommandBufferId(),
                                 next_insert_fence_sync_++);
  memcpy(sync_token, &sync_token_data, sizeof(sync_token_data));
}

void TestGLES2Interface::VerifySyncTokensCHROMIUM(GLbyte** sync_tokens,
                                                  GLsizei count) {
  for (GLsizei i = 0; i < count; ++i) {
    gpu::SyncToken sync_token_data;
    memcpy(sync_token_data.GetData(), sync_tokens[i], sizeof(sync_token_data));
    sync_token_data.SetVerifyFlush();
    memcpy(sync_tokens[i], &sync_token_data, sizeof(sync_token_data));
  }
}

void TestGLES2Interface::WaitSyncTokenCHROMIUM(const GLbyte* sync_token) {
  gpu::SyncToken sync_token_data;
  if (sync_token)
    memcpy(&sync_token_data, sync_token, sizeof(sync_token_data));

  if (sync_token_data.release_count() >
      last_waited_sync_token_.release_count()) {
    last_waited_sync_token_ = sync_token_data;
  }
}

void TestGLES2Interface::BeginQueryEXT(GLenum target, GLuint id) {}

void TestGLES2Interface::EndQueryEXT(GLenum target) {
  if (times_end_query_succeeds_ >= 0) {
    if (!times_end_query_succeeds_) {
      LoseContextCHROMIUM(GL_GUILTY_CONTEXT_RESET_ARB,
                          GL_INNOCENT_CONTEXT_RESET_ARB);
    }
    --times_end_query_succeeds_;
  }
}

void TestGLES2Interface::GetQueryObjectuivEXT(GLuint id,
                                              GLenum pname,
                                              GLuint* params) {
  // If the context is lost, behave as if result is available.
  if (pname == GL_QUERY_RESULT_AVAILABLE_EXT ||
      pname == GL_QUERY_RESULT_AVAILABLE_NO_FLUSH_CHROMIUM_EXT) {
    *params = 1;
  }
}

void TestGLES2Interface::ProduceTextureDirectCHROMIUM(GLuint texture,
                                                      GLbyte* mailbox) {
  gpu::Mailbox gpu_mailbox = gpu::Mailbox::Generate();
  memcpy(mailbox, gpu_mailbox.name, sizeof(gpu_mailbox.name));
}

GLuint TestGLES2Interface::CreateAndConsumeTextureCHROMIUM(
    const GLbyte* mailbox) {
  GLuint texture_id;
  GenTextures(1, &texture_id);
  return texture_id;
}

GLuint TestGLES2Interface::CreateAndTexStorage2DSharedImageCHROMIUM(
    const GLbyte* mailbox) {
  GLuint texture_id;
  GenTextures(1, &texture_id);
  return texture_id;
}

void TestGLES2Interface::ResizeCHROMIUM(GLuint width,
                                        GLuint height,
                                        float device_scale,
                                        GLcolorSpace color_space,
                                        GLboolean has_alpha) {
  reshape_called_ = true;
  width_ = width;
  height_ = height;
  scale_factor_ = device_scale;
}

void TestGLES2Interface::LoseContextCHROMIUM(GLenum current, GLenum other) {
  if (context_lost_)
    return;
  context_lost_ = true;
  if (!context_lost_callback_.is_null())
    std::move(context_lost_callback_).Run();

  for (size_t i = 0; i < shared_contexts_.size(); ++i)
    shared_contexts_[i]->LoseContextCHROMIUM(current, other);
  shared_contexts_.clear();
}

GLenum TestGLES2Interface::GetGraphicsResetStatusKHR() {
  if (IsContextLost())
    return GL_UNKNOWN_CONTEXT_RESET_KHR;
  return GL_NO_ERROR;
}

void TestGLES2Interface::set_times_bind_texture_succeeds(int times) {
  times_bind_texture_succeeds_ = times;
}

void TestGLES2Interface::set_have_extension_io_surface(bool have) {
  test_capabilities_.iosurface = have;
  test_capabilities_.texture_rectangle = have;
}

void TestGLES2Interface::set_have_extension_egl_image(bool have) {
  test_capabilities_.egl_image_external = have;
}

void TestGLES2Interface::set_have_post_sub_buffer(bool have) {
  test_capabilities_.post_sub_buffer = have;
}

void TestGLES2Interface::set_have_swap_buffers_with_bounds(bool have) {
  test_capabilities_.swap_buffers_with_bounds = have;
}

void TestGLES2Interface::set_have_commit_overlay_planes(bool have) {
  test_capabilities_.commit_overlay_planes = have;
}

void TestGLES2Interface::set_have_discard_framebuffer(bool have) {
  test_capabilities_.discard_framebuffer = have;
}

void TestGLES2Interface::set_support_compressed_texture_etc1(bool support) {
  test_capabilities_.texture_format_etc1 = support;
}

void TestGLES2Interface::set_support_texture_format_bgra8888(bool support) {
  test_capabilities_.texture_format_bgra8888 = support;
}

void TestGLES2Interface::set_support_texture_storage(bool support) {
  test_capabilities_.texture_storage = support;
}

void TestGLES2Interface::set_support_texture_usage(bool support) {
  test_capabilities_.texture_usage = support;
}

void TestGLES2Interface::set_support_sync_query(bool support) {
  test_capabilities_.sync_query = support;
}

void TestGLES2Interface::set_support_texture_rectangle(bool support) {
  test_capabilities_.texture_rectangle = support;
}

void TestGLES2Interface::set_support_texture_half_float_linear(bool support) {
  test_capabilities_.texture_half_float_linear = support;
}

void TestGLES2Interface::set_support_texture_norm16(bool support) {
  test_capabilities_.texture_norm16 = support;
}

void TestGLES2Interface::set_msaa_is_slow(bool msaa_is_slow) {
  test_capabilities_.msaa_is_slow = msaa_is_slow;
}

void TestGLES2Interface::set_gpu_rasterization(bool gpu_rasterization) {
  test_capabilities_.gpu_rasterization = gpu_rasterization;
}

void TestGLES2Interface::set_avoid_stencil_buffers(bool avoid_stencil_buffers) {
  test_capabilities_.avoid_stencil_buffers = avoid_stencil_buffers;
}

void TestGLES2Interface::set_support_multisample_compatibility(bool support) {
  test_capabilities_.multisample_compatibility = support;
}

void TestGLES2Interface::set_support_texture_storage_image(bool support) {
  test_capabilities_.texture_storage_image = support;
}

void TestGLES2Interface::set_support_texture_npot(bool support) {
  test_capabilities_.texture_npot = support;
}

void TestGLES2Interface::set_max_texture_size(int size) {
  test_capabilities_.max_texture_size = size;
}

void TestGLES2Interface::set_supports_oop_raster(bool support) {
  test_capabilities_.supports_oop_raster = support;
}

void TestGLES2Interface::set_supports_shared_image_swap_chain(bool support) {
  test_capabilities_.shared_image_swap_chain = support;
}

void TestGLES2Interface::set_supports_gpu_memory_buffer_format(
    gfx::BufferFormat format,
    bool support) {
  if (support) {
    test_capabilities_.gpu_memory_buffer_formats.Add(format);
  } else {
    test_capabilities_.gpu_memory_buffer_formats.Remove(format);
  }
}

size_t TestGLES2Interface::NumTextures() const {
  return textures_.size();
}

GLuint TestGLES2Interface::NextTextureId() {
  GLuint texture_id = next_texture_id_++;
  DCHECK(texture_id < (1 << 16));
  texture_id |= context_id_ << 16;
  return texture_id;
}

void TestGLES2Interface::RetireTextureId(GLuint id) {
  unsigned context_id = id >> 16;
  unsigned texture_id = id & 0xffff;
  DCHECK(texture_id);
  DCHECK_LT(texture_id, next_texture_id_);
  DCHECK_EQ(context_id, context_id_);
}

GLuint TestGLES2Interface::NextBufferId() {
  GLuint buffer_id = next_buffer_id_++;
  DCHECK(buffer_id < (1 << 16));
  buffer_id |= context_id_ << 16;
  return buffer_id;
}

void TestGLES2Interface::RetireBufferId(GLuint id) {
  unsigned context_id = id >> 16;
  unsigned buffer_id = id & 0xffff;
  DCHECK(buffer_id);
  DCHECK_LT(buffer_id, next_buffer_id_);
  DCHECK_EQ(context_id, context_id_);
}

GLuint TestGLES2Interface::NextImageId() {
  GLuint image_id = next_image_id_++;
  DCHECK(image_id < (1 << 16));
  image_id |= context_id_ << 16;
  return image_id;
}

void TestGLES2Interface::RetireImageId(GLuint id) {
  unsigned context_id = id >> 16;
  unsigned image_id = id & 0xffff;
  DCHECK(image_id);
  DCHECK_LT(image_id, next_image_id_);
  DCHECK_EQ(context_id, context_id_);
}

GLuint TestGLES2Interface::NextFramebufferId() {
  GLuint id = next_framebuffer_id_++;
  DCHECK(id < (1 << 16));
  id |= context_id_ << 16;
  framebuffer_set_.insert(id);
  return id;
}

void TestGLES2Interface::RetireFramebufferId(GLuint id) {
  DCHECK(base::Contains(framebuffer_set_, id));
  framebuffer_set_.erase(id);
}

GLuint TestGLES2Interface::NextRenderbufferId() {
  GLuint id = next_renderbuffer_id_++;
  DCHECK(id < (1 << 16));
  id |= context_id_ << 16;
  renderbuffer_set_.insert(id);
  return id;
}

void TestGLES2Interface::RetireRenderbufferId(GLuint id) {
  DCHECK(base::Contains(renderbuffer_set_, id));
  renderbuffer_set_.erase(id);
}

size_t TestGLES2Interface::NumFramebuffers() const {
  return framebuffer_set_.size();
}

size_t TestGLES2Interface::NumRenderbuffers() const {
  return renderbuffer_set_.size();
}

TestGLES2Interface::Buffer::Buffer() : target(0), size(0) {}

TestGLES2Interface::Buffer::~Buffer() = default;

}  // namespace viz
