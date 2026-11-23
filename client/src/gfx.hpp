// Renderizador 2D do Tube360 em Vulkan 1.0: retângulos arredondados, imagens, texto e o quadro do vídeo (YUV).
// O mesmo código no Fedora (Mesa + janela do SDL) e no 360 (vulkan4xenon, VK_KHR_display). Só 2D: sem profundidade
// e sem pós-processamento.
#pragma once
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifdef __XSDK__
#include "xsdk/log.h"
#include "shaders_bundle.h"
#define LOG(...) xsdk_log_write(__VA_ARGS__)
#else
#include <SDL3/SDL_vulkan.h>
#include "ui.vert.h"
#include "shape.frag.h"
#include "image.frag.h"
#include "text.frag.h"
#include "video.frag.h"
#define LOG(...) (std::printf(__VA_ARGS__), std::printf("\n"), std::fflush(stdout))
#endif

#include "stb_truetype.h"

#define VK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { LOG("Vulkan falhou (%d): %s", (int)r_, #x); std::exit(1); } } while (0)

namespace gfx {

struct Buf { VkBuffer b = VK_NULL_HANDLE; VkDeviceMemory m = VK_NULL_HANDLE; void* map = nullptr; VkDeviceSize size = 0; };
struct Tex { VkImage img = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkDescriptorSet set = VK_NULL_HANDLE; int w = 0, h = 0; VkFormat fmt = VK_FORMAT_UNDEFINED; };
struct Vtx { float x, y, u, v, r, g, b, a; };
enum Pipe { SHAPE, IMAGE, TEXT, VIDEO };
struct Cmd { Pipe pipe; VkDescriptorSet set; float params[4]; uint32_t first, count; };

struct Color { float r, g, b, a; };
inline Color rgb(unsigned hex, float a = 1) { return {((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a}; }

struct Glyph { float x0, y0, x1, y1, u0, v0, u1, v1, adv; };
struct Font { float size = 0, ascent = 0, lineH = 0; std::map<uint32_t, Glyph> glyphs; };

// Quadro do vídeo: três texturas de um canal e o descritor que liga as três.
struct Video {
  Tex y, cb, cr; VkDescriptorSet set = VK_NULL_HANDLE; Buf staging[2]; int w = 0, h = 0; bool ready = false;
  bool bt709 = false, fullRange = false;
  bool pending[2] = {false, false};
};

struct Gfx {
  VkInstance inst; VkPhysicalDevice phys; VkDevice dev; VkQueue queue; uint32_t family = 0;
  VkSurfaceKHR surface; VkSwapchainKHR swap = VK_NULL_HANDLE; VkFormat format; VkExtent2D extent;
  std::vector<VkImage> images; std::vector<VkImageView> views; std::vector<VkFramebuffer> fbs;
  VkRenderPass pass; VkDescriptorSetLayout texLayout, vidLayout; VkPipelineLayout texPL, vidPL;
  VkPipeline pipes[4]; VkDescriptorPool pool; VkSampler sampler;
  VkCommandPool cmdPool; VkCommandBuffer cbs[2]; VkFence fences[2]; VkSemaphore acqs[2], rends[2];
  Buf vbuf[2]; uint32_t vcap = 80000; VkPhysicalDeviceMemoryProperties memProps;
  int fi = 0; uint32_t img = 0; bool readback = false;
  std::vector<Vtx> verts; std::vector<Cmd> cmds;
  Tex white, atlas; std::vector<Font> fonts;
  Video* video = nullptr;
  SDL_Window* win = nullptr;
  float clip[4] = {-1e9f, -1e9f, 1e9f, 1e9f};
};

inline uint32_t memType(Gfx& g, uint32_t bits, VkMemoryPropertyFlags want) {
  for (uint32_t i = 0; i < g.memProps.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (g.memProps.memoryTypes[i].propertyFlags & want) == want) return i;
  LOG("sem tipo de memória para 0x%x", (unsigned)bits); std::exit(1);
}

// cached: memória com cache da CPU (no 360 o tipo 1 do vulkan4xenon), para o que a CPU copia todo quadro.
inline Buf makeBuf(Gfx& g, VkDeviceSize size, VkBufferUsageFlags usage, bool cached = false) {
  Buf b; b.size = size;
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VK(vkCreateBuffer(g.dev, &bi, nullptr, &b.b));
  VkMemoryRequirements req; vkGetBufferMemoryRequirements(g.dev, b.b, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size;
  VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  bool hasCached = false;
  for (uint32_t i = 0; i < g.memProps.memoryTypeCount; i++)
    if ((req.memoryTypeBits & (1u << i)) && (g.memProps.memoryTypes[i].propertyFlags & (want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) == (want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) hasCached = true;
  if (cached && hasCached) want |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  ai.memoryTypeIndex = memType(g, req.memoryTypeBits, want);
  VK(vkAllocateMemory(g.dev, &ai, nullptr, &b.m)); VK(vkBindBufferMemory(g.dev, b.b, b.m, 0));
  VK(vkMapMemory(g.dev, b.m, 0, size, 0, &b.map));
  return b;
}
inline void freeBuf(Gfx& g, Buf& b) { if (b.b) { vkDestroyBuffer(g.dev, b.b, nullptr); vkFreeMemory(g.dev, b.m, nullptr); } b = Buf{}; }

template <class F> inline void oneShot(Gfx& g, F&& rec) {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool = g.cmdPool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
  VkCommandBuffer c; VK(vkAllocateCommandBuffers(g.dev, &ai, &c));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK(vkBeginCommandBuffer(c, &bi)); rec(c); VK(vkEndCommandBuffer(c));
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &c;
  VK(vkQueueSubmit(g.queue, 1, &si, VK_NULL_HANDLE)); VK(vkQueueWaitIdle(g.queue));
  vkFreeCommandBuffers(g.dev, g.cmdPool, 1, &c);
}

inline void barrier(VkCommandBuffer c, VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags sa, VkAccessFlags da, VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = from; b.newLayout = to; b.srcAccessMask = sa; b.dstAccessMask = da;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = img; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(c, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
}

inline VkDescriptorSet allocSet(Gfx& g, VkDescriptorSetLayout layout) {
  VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; da.descriptorPool = g.pool; da.descriptorSetCount = 1; da.pSetLayouts = &layout;
  VkDescriptorSet s; VK(vkAllocateDescriptorSets(g.dev, &da, &s)); return s;
}
inline void bindImage(Gfx& g, VkDescriptorSet set, uint32_t binding, VkImageView view) {
  VkDescriptorImageInfo di{g.sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; wr.dstSet = set; wr.dstBinding = binding; wr.descriptorCount = 1;
  wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr.pImageInfo = &di;
  vkUpdateDescriptorSets(g.dev, 1, &wr, 0, nullptr);
}

// Imagem amostrada vazia (layout de leitura), com descritor próprio quando withSet.
inline Tex makeImage(Gfx& g, int w, int h, VkFormat fmt, bool withSet) {
  Tex t; t.w = w; t.h = h; t.fmt = fmt;
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt; ii.extent = {(uint32_t)w, (uint32_t)h, 1};
  ii.mipLevels = 1; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VK(vkCreateImage(g.dev, &ii, nullptr, &t.img));
  VkMemoryRequirements req; vkGetImageMemoryRequirements(g.dev, t.img, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size; ai.memoryTypeIndex = memType(g, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK(vkAllocateMemory(g.dev, &ai, nullptr, &t.mem)); VK(vkBindImageMemory(g.dev, t.img, t.mem, 0));
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = t.img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VK(vkCreateImageView(g.dev, &vi, nullptr, &t.view));
  oneShot(g, [&](VkCommandBuffer c) {
    barrier(c, t.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  });
  if (withSet) { t.set = allocSet(g, g.texLayout); bindImage(g, t.set, 0, t.view); }
  return t;
}

// Copia pixels (tamanho exato da imagem) para a imagem já criada, esperando a GPU (uso: carga de miniatura e fonte).
inline void upload(Gfx& g, Tex& t, const void* data, size_t bytes) {
  Buf st = makeBuf(g, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT); std::memcpy(st.map, data, bytes);
  oneShot(g, [&](VkCommandBuffer c) {
    barrier(c, t.img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy r{}; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; r.imageExtent = {(uint32_t)t.w, (uint32_t)t.h, 1};
    vkCmdCopyBufferToImage(c, st.b, t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    barrier(c, t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  });
  freeBuf(g, st);
}

inline VkShaderModule shader(Gfx& g, const uint32_t* code, size_t bytes) {
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize = bytes; ci.pCode = code;
  VkShaderModule m; VK(vkCreateShaderModule(g.dev, &ci, nullptr, &m)); return m;
}

inline VkPipeline makePipeline(Gfx& g, Pipe which) {
#ifdef __XSDK__
  VkShaderModule vs = shader(g, ui_vert_spirv, UI_VERT_SPIRV_BYTES);
  VkShaderModule fs = which == SHAPE ? shader(g, shape_frag_spirv, SHAPE_FRAG_SPIRV_BYTES) : which == IMAGE ? shader(g, image_frag_spirv, IMAGE_FRAG_SPIRV_BYTES)
                    : which == TEXT ? shader(g, text_frag_spirv, TEXT_FRAG_SPIRV_BYTES) : shader(g, video_frag_spirv, VIDEO_FRAG_SPIRV_BYTES);
#else
  VkShaderModule vs = shader(g, ui_vert, sizeof(ui_vert));
  VkShaderModule fs = which == SHAPE ? shader(g, shape_frag, sizeof(shape_frag)) : which == IMAGE ? shader(g, image_frag, sizeof(image_frag))
                    : which == TEXT ? shader(g, text_frag, sizeof(text_frag)) : shader(g, video_frag, sizeof(video_frag));
#endif
  VkPipelineShaderStageCreateInfo st[2]{};
  st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = vs; st[0].pName = "main";
  st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fs; st[1].pName = "main";
  VkVertexInputBindingDescription bind{0, sizeof(Vtx), VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription attrs[3] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 8}, {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16}};
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind; vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = attrs;
  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport vp{0, 0, (float)g.extent.width, (float)g.extent.height, 0, 1}; VkRect2D sc{{0, 0}, g.extent};
  VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;
  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  VkPipelineColorBlendAttachmentState cb{}; cb.colorWriteMask = 0xF;
  cb.blendEnable = which == VIDEO ? VK_FALSE : VK_TRUE;
  cb.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cb.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cb.colorBlendOp = VK_BLEND_OP_ADD;
  cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cb.alphaBlendOp = VK_BLEND_OP_ADD;
  VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cbs.attachmentCount = 1; cbs.pAttachments = &cb;
  VkDynamicState dyn[] = {VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dy.dynamicStateCount = 1; dy.pDynamicStates = dyn;
  VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pi.stageCount = 2; pi.pStages = st; pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia; pi.pViewportState = &vps; pi.pRasterizationState = &rs;
  pi.pMultisampleState = &ms; pi.pDepthStencilState = &ds; pi.pColorBlendState = &cbs; pi.pDynamicState = &dy;
  pi.layout = which == VIDEO ? g.vidPL : g.texPL; pi.renderPass = g.pass;
  VkPipeline p; VK(vkCreateGraphicsPipelines(g.dev, VK_NULL_HANDLE, 1, &pi, nullptr, &p));
  vkDestroyShaderModule(g.dev, vs, nullptr); vkDestroyShaderModule(g.dev, fs, nullptr);
  return p;
}

#ifdef __XSDK__
inline VkSurfaceKHR displaySurface(Gfx& g) {
  uint32_t count = 1; VkDisplayPropertiesKHR display; VK(vkGetPhysicalDeviceDisplayPropertiesKHR(g.phys, &count, &display));
  count = 1; VkDisplayModePropertiesKHR mode; VK(vkGetDisplayModePropertiesKHR(g.phys, display.display, &count, &mode));
  LOG("tela: %ux%u a %u mHz", (unsigned)mode.parameters.visibleRegion.width, (unsigned)mode.parameters.visibleRegion.height, (unsigned)mode.parameters.refreshRate);
  VkDisplaySurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
  si.displayMode = mode.displayMode; si.planeIndex = 0; si.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  si.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR; si.imageExtent = mode.parameters.visibleRegion;
  VkSurfaceKHR s; VK(vkCreateDisplayPlaneSurfaceKHR(g.inst, &si, nullptr, &s)); return s;
}
#endif

// ---------------------------------------------------------------- fonte (stb_truetype, atlas de um canal)
struct FontSpec { const char* file; float px; };
inline bool loadFonts(Gfx& g, const std::string& dir, const std::vector<FontSpec>& specs) {
  const int AW = 1024, AH = 2048;
  std::vector<unsigned char> pix((size_t)AW * AH, 0);
  stbtt_pack_context pc;
  if (!stbtt_PackBegin(&pc, pix.data(), AW, AH, 0, 1, nullptr)) return false;
  std::map<std::string, std::vector<unsigned char>> files;
  const int ranges[][2] = {{32, 126}, {160, 255}, {0x2010, 0x2026}, {0x2190, 0x2193}, {0x25B6, 0x25C0}};
  for (auto& s : specs) {
    auto& data = files[s.file];
    if (data.empty()) {
      FILE* f = std::fopen((dir + s.file).c_str(), "rb");
      if (!f) { LOG("fonte ausente: %s%s", dir.c_str(), s.file); return false; }
      std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
      data.resize((size_t)n); size_t got = std::fread(data.data(), 1, data.size(), f); std::fclose(f);
      if (got != data.size()) return false;
    }
    stbtt_fontinfo info; stbtt_InitFont(&info, data.data(), 0);
    int asc, desc, gap; stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    float scale = stbtt_ScaleForPixelHeight(&info, s.px);
    Font font; font.size = s.px; font.ascent = asc * scale; font.lineH = (asc - desc + gap) * scale;
    for (auto& r : ranges) {
      int n = r[1] - r[0] + 1;
      std::vector<stbtt_packedchar> pcs((size_t)n);
      stbtt_PackFontRange(&pc, data.data(), 0, s.px, r[0], n, pcs.data());
      for (int i = 0; i < n; i++) {
        auto& q = pcs[(size_t)i];
        if (q.x1 == 0 && q.y1 == 0 && q.xadvance == 0) continue;
        font.glyphs[(uint32_t)(r[0] + i)] = {q.xoff, q.yoff, q.xoff2, q.yoff2, q.x0 / (float)AW, q.y0 / (float)AH, q.x1 / (float)AW, q.y1 / (float)AH, q.xadvance};
      }
    }
    g.fonts.push_back(std::move(font));
  }
  stbtt_PackEnd(&pc);
  g.atlas = makeImage(g, AW, AH, VK_FORMAT_R8_UNORM, true);
  upload(g, g.atlas, pix.data(), pix.size());
  return true;
}

inline uint32_t utf8(const std::string& s, size_t& i) {
  unsigned char c = (unsigned char)s[i++];
  if (c < 0x80) return c;
  int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  uint32_t cp = c & (0x3F >> extra);
  for (int k = 0; k < extra && i < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
  return cp;
}

inline const Glyph* glyph(const Font& f, uint32_t cp) {
  auto it = f.glyphs.find(cp);
  if (it != f.glyphs.end()) return &it->second;
  return nullptr;
}

inline float textWidth(Gfx& g, int font, const std::string& s, float track = 0) {
  const Font& f = g.fonts[(size_t)font]; float w = 0; int n = 0;
  for (size_t i = 0; i < s.size();) { const Glyph* gl = glyph(f, utf8(s, i)); if (gl) { w += gl->adv; n++; } }
  return w + track * std::max(0, n - 1);
}

// Corta com reticências para caber em maxW.
inline std::string ellipsize(Gfx& g, int font, const std::string& s, float maxW) {
  if (textWidth(g, font, s) <= maxW) return s;
  const Font& f = g.fonts[(size_t)font]; const Glyph* dot = glyph(f, 0x2026);
  float ell = dot ? dot->adv : 0, w = 0; size_t cut = 0;
  for (size_t i = 0; i < s.size();) {
    size_t at = i; const Glyph* gl = glyph(f, utf8(s, i)); float a = gl ? gl->adv : 0;
    if (w + a + ell > maxW) { cut = at; break; }
    w += a; cut = i;
  }
  std::string o = s.substr(0, cut);
  while (!o.empty() && o.back() == ' ') o.pop_back();
  return o + (dot ? "\xE2\x80\xA6" : "...");
}

// Quebra em até maxLines linhas; a última leva reticências se sobrar texto.
inline std::vector<std::string> wrap(Gfx& g, int font, const std::string& s, float maxW, int maxLines) {
  std::vector<std::string> words, lines; std::string cur;
  for (char c : s) { if (c == ' ') { if (!cur.empty()) words.push_back(cur); cur.clear(); } else cur += c; }
  if (!cur.empty()) words.push_back(cur);
  std::string line;
  for (size_t i = 0; i < words.size(); i++) {
    std::string tryLine = line.empty() ? words[i] : line + " " + words[i];
    if (textWidth(g, font, tryLine) <= maxW || line.empty()) { line = tryLine; continue; }
    lines.push_back(line); line = words[i];
    if ((int)lines.size() == maxLines - 1) {
      std::string rest = line; for (size_t k = i + 1; k < words.size(); k++) rest += " " + words[k];
      lines.push_back(ellipsize(g, font, rest, maxW)); return lines;
    }
  }
  if (!line.empty()) lines.push_back(ellipsize(g, font, line, maxW));
  return lines;
}

// ---------------------------------------------------------------- lista de desenho
inline void push(Gfx& g, Pipe p, VkDescriptorSet set, const float params[4], const Vtx* v, int n) {
  if (g.verts.size() + (size_t)n > g.vcap) return;
  bool merge = !g.cmds.empty() && g.cmds.back().pipe == p && g.cmds.back().set == set && std::memcmp(g.cmds.back().params, params, 16) == 0
               && g.cmds.back().first + g.cmds.back().count == g.verts.size();
  if (!merge) { Cmd c{p, set, {params[0], params[1], params[2], params[3]}, (uint32_t)g.verts.size(), 0}; g.cmds.push_back(c); }
  g.verts.insert(g.verts.end(), v, v + n); g.cmds.back().count += (uint32_t)n;
}

inline void quad(Gfx& g, Pipe p, VkDescriptorSet set, const float params[4], float x, float y, float w, float h, Color c, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
  Vtx v[6] = {{x, y, u0, v0, c.r, c.g, c.b, c.a}, {x + w, y, u1, v0, c.r, c.g, c.b, c.a}, {x + w, y + h, u1, v1, c.r, c.g, c.b, c.a},
              {x, y, u0, v0, c.r, c.g, c.b, c.a}, {x + w, y + h, u1, v1, c.r, c.g, c.b, c.a}, {x, y + h, u0, v1, c.r, c.g, c.b, c.a}};
  push(g, p, set, params, v, 6);
}

// Retângulo com cor por canto (degradê): c0 em cima à esquerda, c1 em cima à direita, c2 embaixo à direita, c3 embaixo à esquerda.
inline void rectGrad(Gfx& g, float x, float y, float w, float h, Color c0, Color c1, Color c2, Color c3, float radius = 0, float feather = 1) {
  float p[4] = {w, h, std::min(radius, std::min(w, h) * 0.5f), feather};
  Vtx v[6] = {{x, y, 0, 0, c0.r, c0.g, c0.b, c0.a}, {x + w, y, 1, 0, c1.r, c1.g, c1.b, c1.a}, {x + w, y + h, 1, 1, c2.r, c2.g, c2.b, c2.a},
              {x, y, 0, 0, c0.r, c0.g, c0.b, c0.a}, {x + w, y + h, 1, 1, c2.r, c2.g, c2.b, c2.a}, {x, y + h, 0, 1, c3.r, c3.g, c3.b, c3.a}};
  push(g, SHAPE, g.white.set, p, v, 6);
}
inline void hgrad(Gfx& g, float x, float y, float w, float h, Color l, Color r, float radius = 0) { rectGrad(g, x, y, w, h, l, r, r, l, radius); }
inline void vgrad(Gfx& g, float x, float y, float w, float h, Color t, Color b, float radius = 0) { rectGrad(g, x, y, w, h, t, t, b, b, radius); }

// Forma em (x, y, w, h); com esfumado > 1 o quad cresce para a opacidade sumir antes da borda dele.
inline void rect(Gfx& g, float x, float y, float w, float h, Color c, float radius = 0, float feather = 1) {
  float m = std::max(feather, 1.f) - 1;
  float p[4] = {w + 2 * m, h + 2 * m, std::min(radius, std::min(w, h) * 0.5f), feather};
  quad(g, SHAPE, g.white.set, p, x - m, y - m, w + 2 * m, h + 2 * m, c);
}
// Sombra suave embaixo de uma forma.
inline void shadow(Gfx& g, float x, float y, float w, float h, float radius, float blur, float alpha) {
  rect(g, x, y, w, h, {0, 0, 0, alpha}, radius, blur);
}
inline void image(Gfx& g, const Tex& t, float x, float y, float w, float h, float radius = 0, Color tint = {1, 1, 1, 1}) {
  float p[4] = {w, h, radius, 1};
  quad(g, IMAGE, t.set, p, x, y, w, h, tint);
}
// Recorte (u0,v0)-(u1,v1) de uma textura, sem cantos arredondados (ícones do atlas).
inline void imagePart(Gfx& g, const Tex& t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color tint) {
  float p[4] = {w, h, 0, 1};
  quad(g, IMAGE, t.set, p, x, y, w, h, tint, u0, v0, u1, v1);
}
inline void triangle(Gfx& g, float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
  // shape.frag com raio 0 e tamanho enorme: a distância fica negativa em todo o triângulo (cor cheia)
  float p[4] = {1e6f, 1e6f, 0, 1};
  Vtx v[3] = {{x0, y0, 0.5f, 0.5f, c.r, c.g, c.b, c.a}, {x1, y1, 0.5f, 0.5f, c.r, c.g, c.b, c.a}, {x2, y2, 0.5f, 0.5f, c.r, c.g, c.b, c.a}};
  push(g, SHAPE, g.white.set, p, v, 3);
}

// Texto com o topo da linha em y. Devolve a largura.
inline float text(Gfx& g, int font, const std::string& s, float x, float y, Color c, int align = 0, float track = 0) {
  const Font& f = g.fonts[(size_t)font];
  float w = align ? textWidth(g, font, s, track) : 0;
  float pen = align == 1 ? x - w * 0.5f : align == 2 ? x - w : x;
  float base = std::round(y + f.ascent), start = pen;
  pen = std::round(pen);
  float p[4] = {1, 0, 0, 0};
  for (size_t i = 0; i < s.size();) {
    const Glyph* gl = glyph(f, utf8(s, i));
    if (!gl) continue;
    if (gl->x1 > gl->x0) quad(g, TEXT, g.atlas.set, p, pen + gl->x0, base + gl->y0, gl->x1 - gl->x0, gl->y1 - gl->y0, c, gl->u0, gl->v0, gl->u1, gl->v1);
    pen += gl->adv + track;
  }
  return pen - start - (pen > start ? track : 0);
}

// Texto com degradê horizontal (cor de cada glifo pela posição).
inline float textGrad(Gfx& g, int font, const std::string& s, float x, float y, Color a, Color b) {
  const Font& f = g.fonts[(size_t)font];
  float total = std::max(1.f, textWidth(g, font, s)), base = std::round(y + f.ascent), pen = std::round(x);
  float p[4] = {1, 0, 0, 0};
  for (size_t i = 0; i < s.size();) {
    const Glyph* gl = glyph(f, utf8(s, i));
    if (!gl) continue;
    float t0 = (pen + gl->x0 - x) / total, t1 = (pen + gl->x1 - x) / total;
    auto mixc = [&](float t) { t = std::clamp(t, 0.f, 1.f); return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; };
    Color l = mixc(t0), r = mixc(t1);
    float gx = pen + gl->x0, gy = base + gl->y0, gw = gl->x1 - gl->x0, gh = gl->y1 - gl->y0;
    if (gw > 0) {
      Vtx v[6] = {{gx, gy, gl->u0, gl->v0, l.r, l.g, l.b, l.a}, {gx + gw, gy, gl->u1, gl->v0, r.r, r.g, r.b, r.a}, {gx + gw, gy + gh, gl->u1, gl->v1, r.r, r.g, r.b, r.a},
                  {gx, gy, gl->u0, gl->v0, l.r, l.g, l.b, l.a}, {gx + gw, gy + gh, gl->u1, gl->v1, r.r, r.g, r.b, r.a}, {gx, gy + gh, gl->u0, gl->v1, l.r, l.g, l.b, l.a}};
      push(g, TEXT, g.atlas.set, p, v, 6);
    }
    pen += gl->adv;
  }
  return total;
}

inline void videoQuad(Gfx& g, float x, float y, float w, float h, float alpha = 1) {
  if (!g.video || !g.video->ready) return;
  float scale = std::min(w / g.video->w, h / g.video->h);
  float vw = scale * g.video->w, vh = scale * g.video->h;
  x += (w - vw) * .5f; y += (h - vh) * .5f; w = vw; h = vh;
  bool hd = g.video->bt709, full = g.video->fullRange;
  float c = full ? 1.f : 255.f / 224.f;
  float p[4] = {(hd ? 1.5748f : 1.402f) * c, (hd ? 1.8556f : 1.772f) * c,
                full ? 1.f : 255.f / 219.f, full ? 0.f : 16.f / 255.f};
  // Os dois coeficientes do verde usam a cor constante dos vértices. A matriz já chega pronta ao Xenos.
  quad(g, VIDEO, g.video->set, p, x, y, w, h, {(hd ? .187324f : .344136f) * c, (hd ? .468124f : .714136f) * c, 0, alpha});
}

// ---------------------------------------------------------------- vídeo
inline void videoCreate(Gfx& g, Video& v, int w, int h) {
  v.w = w; v.h = h;
  v.y = makeImage(g, w, h, VK_FORMAT_R8_UNORM, false);
  v.cb = makeImage(g, (w + 1) / 2, (h + 1) / 2, VK_FORMAT_R8_UNORM, false);
  v.cr = makeImage(g, (w + 1) / 2, (h + 1) / 2, VK_FORMAT_R8_UNORM, false);
  v.set = allocSet(g, g.vidLayout);
  bindImage(g, v.set, 0, v.y.view); bindImage(g, v.set, 1, v.cb.view); bindImage(g, v.set, 2, v.cr.view);
  size_t bytes = (size_t)w * h + 2 * (size_t)((w + 1) / 2) * ((h + 1) / 2);
  for (auto& s : v.staging) { s = makeBuf(g, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true); std::memset(s.map, 0, bytes); }
  v.ready = false;
}
inline void videoDestroy(Gfx& g, Video& v) {
  vkDeviceWaitIdle(g.dev);
  for (Tex* t : {&v.y, &v.cb, &v.cr}) { if (t->img) { vkDestroyImageView(g.dev, t->view, nullptr); vkDestroyImage(g.dev, t->img, nullptr); vkFreeMemory(g.dev, t->mem, nullptr); } *t = Tex{}; }
  if (v.set) vkFreeDescriptorSets(g.dev, g.pool, 1, &v.set);
  v.set = VK_NULL_HANDLE;
  for (auto& s : v.staging) freeBuf(g, s);
  v.ready = false; v.pending[0] = v.pending[1] = false;
}
// Ponteiro para o staging do quadro da vez: o app escreve Y, Cb e Cr colados e chama videoCommit.
inline uint8_t* videoStaging(Gfx& g, Video& v) { return (uint8_t*)v.staging[g.fi].map; }
inline void videoCommit(Gfx& g, Video& v) { v.pending[g.fi] = true; v.ready = true; }

// ---------------------------------------------------------------- setup e quadro
inline void makeSwapchain(Gfx& g, int w, int h) {
  VkSurfaceCapabilitiesKHR caps; VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g.phys, g.surface, &caps));
  g.extent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : VkExtent2D{(uint32_t)w, (uint32_t)h};
  uint32_t n = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(g.phys, g.surface, &n, nullptr);
  std::vector<VkSurfaceFormatKHR> fmts(n); vkGetPhysicalDeviceSurfaceFormatsKHR(g.phys, g.surface, &n, fmts.data());
  g.format = fmts[0].format; for (auto& f : fmts) if (f.format == VK_FORMAT_B8G8R8A8_UNORM) g.format = f.format;
  VkSwapchainCreateInfoKHR si{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  si.surface = g.surface; si.minImageCount = std::max(2u, caps.minImageCount); si.imageFormat = g.format;
  si.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; si.imageExtent = g.extent; si.imageArrayLayers = 1;
  si.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (g.readback ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
  si.preTransform = caps.currentTransform; si.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; si.presentMode = VK_PRESENT_MODE_FIFO_KHR; si.clipped = VK_TRUE;
  VK(vkCreateSwapchainKHR(g.dev, &si, nullptr, &g.swap));
  vkGetSwapchainImagesKHR(g.dev, g.swap, &n, nullptr); g.images.resize(n); vkGetSwapchainImagesKHR(g.dev, g.swap, &n, g.images.data());
  VkAttachmentDescription at{}; at.format = g.format; at.samples = VK_SAMPLE_COUNT_1_BIT; at.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; at.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  at.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; at.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; at.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sp{}; sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sp.colorAttachmentCount = 1; sp.pColorAttachments = &cr;
  VkSubpassDependency dep{}; dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
  dep.srcStageMask = dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; rp.attachmentCount = 1; rp.pAttachments = &at; rp.subpassCount = 1; rp.pSubpasses = &sp; rp.dependencyCount = 1; rp.pDependencies = &dep;
  VK(vkCreateRenderPass(g.dev, &rp, nullptr, &g.pass));
  g.views.resize(n); g.fbs.resize(n);
  for (uint32_t i = 0; i < n; i++) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = g.images[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = g.format; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK(vkCreateImageView(g.dev, &vi, nullptr, &g.views[i]));
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fi.renderPass = g.pass; fi.attachmentCount = 1; fi.pAttachments = &g.views[i];
    fi.width = g.extent.width; fi.height = g.extent.height; fi.layers = 1;
    VK(vkCreateFramebuffer(g.dev, &fi, nullptr, &g.fbs[i]));
  }
}

inline bool init(Gfx& g, int W, int H, const char* title) {
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = title; app.apiVersion = VK_API_VERSION_1_0;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
#ifdef __XSDK__
  const char* instExt[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME};
  ici.enabledExtensionCount = 2; ici.ppEnabledExtensionNames = instExt;
  VK(vkCreateInstance(&ici, nullptr, &g.inst));
#else
  g.win = SDL_CreateWindow(title, W, H, SDL_WINDOW_VULKAN);
  if (!g.win) { LOG("janela: %s", SDL_GetError()); return false; }
  Uint32 extN = 0; const char* const* instExt = SDL_Vulkan_GetInstanceExtensions(&extN);
  ici.enabledExtensionCount = extN; ici.ppEnabledExtensionNames = instExt;
  VK(vkCreateInstance(&ici, nullptr, &g.inst));
  if (!SDL_Vulkan_CreateSurface(g.win, g.inst, nullptr, &g.surface)) { LOG("superfície: %s", SDL_GetError()); return false; }
#endif
  uint32_t n = 0; vkEnumeratePhysicalDevices(g.inst, &n, nullptr);
  std::vector<VkPhysicalDevice> devs(n); vkEnumeratePhysicalDevices(g.inst, &n, devs.data()); g.phys = devs[0];
#ifdef __XSDK__
  g.surface = displaySurface(g);
#endif
  vkGetPhysicalDeviceMemoryProperties(g.phys, &g.memProps);
  vkGetPhysicalDeviceQueueFamilyProperties(g.phys, &n, nullptr);
  std::vector<VkQueueFamilyProperties> fams(n); vkGetPhysicalDeviceQueueFamilyProperties(g.phys, &n, fams.data());
  for (uint32_t i = 0; i < n; i++) { VkBool32 pres = 0; vkGetPhysicalDeviceSurfaceSupportKHR(g.phys, i, g.surface, &pres); if ((fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) { g.family = i; break; } }
  float prio = 1; VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex = g.family; qi.queueCount = 1; qi.pQueuePriorities = &prio;
  const char* devExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qi; dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = devExt;
  VK(vkCreateDevice(g.phys, &dci, nullptr, &g.dev)); vkGetDeviceQueue(g.dev, g.family, 0, &g.queue);
  makeSwapchain(g, W, H);
  LOG("tela %ux%u", g.extent.width, g.extent.height);
  VkDescriptorSetLayoutBinding tb{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo tl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; tl.bindingCount = 1; tl.pBindings = &tb;
  VK(vkCreateDescriptorSetLayout(g.dev, &tl, nullptr, &g.texLayout));
  VkDescriptorSetLayoutBinding vb[3] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
  VkDescriptorSetLayoutCreateInfo vl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; vl.bindingCount = 3; vl.pBindings = vb;
  VK(vkCreateDescriptorSetLayout(g.dev, &vl, nullptr, &g.vidLayout));
  VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32};
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pl.setLayoutCount = 1; pl.pSetLayouts = &g.texLayout; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pcr;
  VK(vkCreatePipelineLayout(g.dev, &pl, nullptr, &g.texPL));
  pl.pSetLayouts = &g.vidLayout; VK(vkCreatePipelineLayout(g.dev, &pl, nullptr, &g.vidPL));
  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 600};
  VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; dp.maxSets = 300; dp.poolSizeCount = 1; dp.pPoolSizes = &ps;
  VK(vkCreateDescriptorPool(g.dev, &dp, nullptr, &g.pool));
  VkSamplerCreateInfo smp{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; smp.magFilter = smp.minFilter = VK_FILTER_LINEAR; smp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  smp.addressModeU = smp.addressModeV = smp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; smp.maxLod = 0;
  VK(vkCreateSampler(g.dev, &smp, nullptr, &g.sampler));
  VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cp.queueFamilyIndex = g.family;
  VK(vkCreateCommandPool(g.dev, &cp, nullptr, &g.cmdPool));
  VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool = g.cmdPool; ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount = 2;
  VK(vkAllocateCommandBuffers(g.dev, &ca, g.cbs));
  for (int i = 0; i < 2; i++) {
    VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fc.flags = VK_FENCE_CREATE_SIGNALED_BIT; VK(vkCreateFence(g.dev, &fc, nullptr, &g.fences[i]));
    VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VK(vkCreateSemaphore(g.dev, &sc, nullptr, &g.acqs[i])); VK(vkCreateSemaphore(g.dev, &sc, nullptr, &g.rends[i]));
    g.vbuf[i] = makeBuf(g, (VkDeviceSize)g.vcap * sizeof(Vtx), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  }
  for (int p = 0; p < 4; p++) g.pipes[p] = makePipeline(g, (Pipe)p);
  g.white = makeImage(g, 4, 4, VK_FORMAT_R8G8B8A8_UNORM, true);
  std::vector<uint8_t> w(64, 255); upload(g, g.white, w.data(), w.size());
  return true;
}

inline void begin(Gfx& g) {
  VK(vkWaitForFences(g.dev, 1, &g.fences[g.fi], VK_TRUE, UINT64_MAX));
  g.verts.clear(); g.cmds.clear();
}

// Grava e apresenta. Se shotPath, copia a imagem para um PPM (só no PC, para conferir a interface).
inline void end(Gfx& g, const char* shotPath = nullptr) {
  VK(vkAcquireNextImageKHR(g.dev, g.swap, UINT64_MAX, g.acqs[g.fi], VK_NULL_HANDLE, &g.img));
  VK(vkResetFences(g.dev, 1, &g.fences[g.fi]));
  VkCommandBuffer c = g.cbs[g.fi];
  VK(vkResetCommandBuffer(c, 0));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; VK(vkBeginCommandBuffer(c, &bi));
  if (g.video && g.video->pending[g.fi]) {
    Video& v = *g.video; v.pending[g.fi] = false;
    VkDeviceSize off = 0;
    for (Tex* t : {&v.y, &v.cb, &v.cr}) {
      barrier(c, t->img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
      VkBufferImageCopy r{}; r.bufferOffset = off; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; r.imageExtent = {(uint32_t)t->w, (uint32_t)t->h, 1};
      vkCmdCopyBufferToImage(c, v.staging[g.fi].b, t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
      barrier(c, t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
      off += (VkDeviceSize)t->w * t->h;
    }
  }
  if (!g.verts.empty()) std::memcpy(g.vbuf[g.fi].map, g.verts.data(), g.verts.size() * sizeof(Vtx));
  VkClearValue clear{}; clear.color = {{0.055f, 0.055f, 0.063f, 1}};
  VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; rb.renderPass = g.pass; rb.framebuffer = g.fbs[g.img]; rb.renderArea = {{0, 0}, g.extent}; rb.clearValueCount = 1; rb.pClearValues = &clear;
  vkCmdBeginRenderPass(c, &rb, VK_SUBPASS_CONTENTS_INLINE);
  VkRect2D sc{{0, 0}, g.extent}; vkCmdSetScissor(c, 0, 1, &sc);
  VkDeviceSize zero = 0; vkCmdBindVertexBuffers(c, 0, 1, &g.vbuf[g.fi].b, &zero);
  float screen[4] = {2.f / g.extent.width, 2.f / g.extent.height, -1, -1};
  int bound = -1; VkDescriptorSet boundSet = VK_NULL_HANDLE;
  for (auto& cmd : g.cmds) {
    VkPipelineLayout layout = cmd.pipe == VIDEO ? g.vidPL : g.texPL;
    if ((int)cmd.pipe != bound) { vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipes[cmd.pipe]); bound = (int)cmd.pipe; boundSet = VK_NULL_HANDLE; }
    if (cmd.set != boundSet) { vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &cmd.set, 0, nullptr); boundSet = cmd.set; }
    float pc[8] = {screen[0], screen[1], screen[2], screen[3], cmd.params[0], cmd.params[1], cmd.params[2], cmd.params[3]};
    vkCmdPushConstants(c, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32, pc);
    vkCmdDraw(c, cmd.count, 1, cmd.first, 0);
  }
  vkCmdEndRenderPass(c);
  Buf shot;
  if (shotPath && g.readback) {
    shot = makeBuf(g, (VkDeviceSize)g.extent.width * g.extent.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    barrier(c, g.images[g.img], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy r{}; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; r.imageExtent = {g.extent.width, g.extent.height, 1};
    vkCmdCopyImageToBuffer(c, g.images[g.img], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shot.b, 1, &r);
    barrier(c, g.images[g.img], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  }
  VK(vkEndCommandBuffer(c));
  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.waitSemaphoreCount = 1; si.pWaitSemaphores = &g.acqs[g.fi]; si.pWaitDstStageMask = &waitStage;
  si.commandBufferCount = 1; si.pCommandBuffers = &c; si.signalSemaphoreCount = 1; si.pSignalSemaphores = &g.rends[g.fi];
  VK(vkQueueSubmit(g.queue, 1, &si, g.fences[g.fi]));
  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &g.rends[g.fi]; pi.swapchainCount = 1; pi.pSwapchains = &g.swap; pi.pImageIndices = &g.img;
  vkQueuePresentKHR(g.queue, &pi);
  if (shot.b) {
    VK(vkWaitForFences(g.dev, 1, &g.fences[g.fi], VK_TRUE, UINT64_MAX));
    if (FILE* f = std::fopen(shotPath, "wb")) {
      std::fprintf(f, "P6\n%u %u\n255\n", g.extent.width, g.extent.height);
      const uint8_t* p = (const uint8_t*)shot.map;
      bool bgr = g.format == VK_FORMAT_B8G8R8A8_UNORM;
      for (size_t i = 0; i < (size_t)g.extent.width * g.extent.height; i++) { uint8_t px[3] = {p[i * 4 + (bgr ? 2 : 0)], p[i * 4 + 1], p[i * 4 + (bgr ? 0 : 2)]}; std::fwrite(px, 1, 3, f); }
      std::fclose(f);
    }
    freeBuf(g, shot);
  }
  g.fi ^= 1;
}

}  // namespace gfx
