#pragma once
#include "gfx.hpp"

namespace gfx {
struct MappedVideo : video::Surface {
  VkDevice dev = VK_NULL_HANDLE;
  Tex planes[3]; void* maps[3] = {};
  VkDescriptorPool pool = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
  size_t bytes = 0;
  uint64_t flushNs = 0;
  void publish() override {
    uint64_t began = SDL_GetTicksNS();
    VkMappedMemoryRange ranges[3]{};
    for (int i = 0; i < 3; ++i) {
      ranges[i].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      ranges[i].memory = planes[i].mem; ranges[i].size = VK_WHOLE_SIZE;
    }
    VK(vkFlushMappedMemoryRanges(dev,3,ranges));
    flushNs = SDL_GetTicksNS() - began;
  }
  ~MappedVideo() override {
    if (pool) vkDestroyDescriptorPool(dev,pool,nullptr);
    for (auto& t : planes) {
      if (t.view) vkDestroyImageView(dev,t.view,nullptr);
      if (t.img) vkDestroyImage(dev,t.img,nullptr);
      if (t.mem) { vkUnmapMemory(dev,t.mem); vkFreeMemory(dev,t.mem,nullptr); }
    }
  }
};

struct DecodePool : video::Allocator {
  Gfx& g;
  SDL_Mutex* mu = SDL_CreateMutex();
  std::vector<std::shared_ptr<MappedVideo>> slots;
  bool enabled = false;
  size_t bytes = 0;
  explicit DecodePool(Gfx& renderer) : g(renderer) {
    VkFormatProperties props{}; vkGetPhysicalDeviceFormatProperties(g.phys,VK_FORMAT_R8_UNORM,&props);
    enabled = (props.linearTilingFeatures & (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) ==
              (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
    LOG("video-memoria: %s, pool ate 48 superficies/160 MiB, DPB + fila + fences",enabled ? "linear mapeada" : "staging (linear indisponivel)");
  }
  ~DecodePool() override { slots.clear(); SDL_DestroyMutex(mu); }
  std::shared_ptr<video::Surface> acquire(int w,int h,int aw,int ah,const int* alignment) override {
    SDL_LockMutex(mu);
    auto result = allocate(w,h,aw,ah,alignment);
    SDL_UnlockMutex(mu);
    return result;
  }
  std::shared_ptr<MappedVideo> allocate(int w,int h,int aw,int ah,const int* alignment) {
    for (auto& slot : slots)
      if (slot.use_count() == 1 && slot->w == w && slot->h == h && slot->alignedW == aw && slot->alignedH == ah) return slot;
    for (auto it = slots.begin(); it != slots.end();) {
      if (it->use_count() == 1 && ((*it)->w != w || (*it)->h != h)) { bytes -= (*it)->bytes; it = slots.erase(it); }
      else ++it;
    }
    if (slots.size() >= 48) { LOG("video-memoria: pool esgotado, %u superficies",(unsigned)slots.size()); return {}; }
    auto slot = std::make_shared<MappedVideo>(); slot->dev = g.dev;
    slot->w = w; slot->h = h; slot->alignedW = aw; slot->alignedH = ah;
    for (int i = 0; i < 3; ++i) {
      Tex& t = slot->planes[i]; t.w = i ? (w+1)/2 : w; t.h = i ? (h+1)/2 : h; t.fmt = VK_FORMAT_R8_UNORM;
      VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType = VK_IMAGE_TYPE_2D; ii.format = t.fmt;
      ii.extent = {(uint32_t)t.w,(uint32_t)t.h,1}; ii.mipLevels = ii.arrayLayers = 1;
      ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_LINEAR; ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      ii.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
      if (vkCreateImage(g.dev,&ii,nullptr,&t.img) != VK_SUCCESS) return {};
      VkMemoryRequirements req{}; vkGetImageMemoryRequirements(g.dev,t.img,&req);
      VkImageSubresource sub{VK_IMAGE_ASPECT_COLOR_BIT,0,0}; VkSubresourceLayout layout{};
      vkGetImageSubresourceLayout(g.dev,t.img,&sub,&layout);
      if (!layout.rowPitch || layout.rowPitch > INT32_MAX || layout.rowPitch % std::max(1,alignment[i]) ||
          layout.rowPitch < (VkDeviceSize)(i ? (aw+1)/2 : aw)) return {};
      slot->stride[i] = int(layout.rowPitch);
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.allocationSize = std::max(req.size,layout.offset + layout.rowPitch * (i ? (ah+1)/2 : ah) + 64);
      // Aloca a altura alinhada do codec, mas mantém a extensão amostrada na altura visível (1080, não 1088).
      bool found = false;
      for (uint32_t m = 0; m < g.memProps.memoryTypeCount; ++m) {
        auto want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        if ((req.memoryTypeBits & (1u<<m)) && (g.memProps.memoryTypes[m].propertyFlags & want) == (unsigned)want) {
          ai.memoryTypeIndex = m; found = true; break;
        }
      }
      if (!found || bytes + slot->bytes + ai.allocationSize > (160u<<20)) return {};
      if (vkAllocateMemory(g.dev,&ai,nullptr,&t.mem) != VK_SUCCESS) return {};
      slot->bytes += ai.allocationSize;
      if (vkBindImageMemory(g.dev,t.img,t.mem,0) != VK_SUCCESS ||
          vkMapMemory(g.dev,t.mem,0,VK_WHOLE_SIZE,0,&slot->maps[i]) != VK_SUCCESS) return {};
      slot->data[i] = static_cast<uint8_t*>(slot->maps[i]) + layout.offset;
      std::memset(slot->maps[i],i ? 128 : 0,ai.allocationSize);
      VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = t.img;
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = t.fmt; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      if (vkCreateImageView(g.dev,&vi,nullptr,&t.view) != VK_SUCCESS) return {};
    }
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,3};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pi.maxSets = 1; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(g.dev,&pi,nullptr,&slot->pool) != VK_SUCCESS) return {};
    VkDescriptorSetAllocateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; si.descriptorPool = slot->pool;
    si.descriptorSetCount = 1; si.pSetLayouts = &g.vidLayout;
    if (vkAllocateDescriptorSets(g.dev,&si,&slot->set) != VK_SUCCESS) return {};
    for (int i = 0; i < 3; ++i) bindImage(g,slot->set,i,slot->planes[i].view,VK_IMAGE_LAYOUT_GENERAL);
    bytes += slot->bytes; slots.push_back(slot);
    LOG("video-memoria: %dx%d, codec %dx%d, pitch %d/%d/%d, pool %u, %.2f MiB",w,h,aw,ah,
        slot->stride[0],slot->stride[1],slot->stride[2],(unsigned)slots.size(),bytes/1048576.0);
    return slot;
  }
};

inline void videoUse(Gfx& g,Video& v,const std::shared_ptr<video::Surface>& surface,double pts) {
  auto& mapped = static_cast<MappedVideo&>(*surface);
  if (!v.surface && v.set) videoDestroy(g,v);
  v.surface = surface; v.w = surface->w; v.h = surface->h;
  v.y = mapped.planes[0]; v.cb = mapped.planes[1]; v.cr = mapped.planes[2]; v.set = mapped.set;
  v.pts = pts; v.copiedNs = 0; ++g.uploadCount; ++g.directCount;
  if (!g.uploadAt) g.uploadAt = SDL_GetTicks();
  videoCommit(g,v);
  if (g.videoMetrics) LOG("upload-quadro: pts %.6f, %dx%d, direto, staging 0, flush-decode %.3f ms",pts,v.w,v.h,mapped.flushNs/1e6);
}
}
