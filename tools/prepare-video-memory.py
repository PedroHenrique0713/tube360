#!/usr/bin/env python3
"""Extensão local do vulkan4xenon: R8 linear e memória física cacheada não coerente."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
stamp = root / "tube360-video-memory-v2"
if stamp.exists():
    sys.exit(0)
previous = root / "tube360-video-memory-v1"
if previous.exists():
    path = root / "src/memory.c"
    text = path.read_text()
    if text.count('"dcbi 0, %0"') != 1:
        raise RuntimeError("Invalidate do tipo físico cacheado incompatível")
    path.write_text(text.replace('"dcbi 0, %0"', '"dcbf 0, %0"'))
    previous.unlink()
    stamp.write_text("physical cached; dcbst flush and dcbf invalidate\n")
    sys.exit(0)


def replace(file, before, after):
    path = root / "src" / file
    text = path.read_text()
    if text.count(before) != 1:
        raise RuntimeError(f"Fonte incompatível: {path}: {before[:80]!r}")
    path.write_text(text.replace(before, after))


replace("image.h", "V4X_LINEAR_IMAGE_USAGE = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,",
        "V4X_LINEAR_IMAGE_USAGE = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,")
replace("physical_device.c", ".memoryTypeCount = 2,", ".memoryTypeCount = 3,")
replace("physical_device.c", "\t\t\t\t0 } },", "\t\t\t\t0 },\n\t\t\t{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0 } },")
replace("physical_device.c", "\tif (image)\n\t\tpFormatProperties->optimalTilingFeatures = image->features;", """\tif (image)
\t\tpFormatProperties->optimalTilingFeatures = image->features;
\tif (format == VK_FORMAT_R8_UNORM)
\t\tpFormatProperties->linearTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;""")
replace("objects.h", "\tbool cached;\n} DeviceMemory;", "\tbool cached, physical_cached;\n} DeviceMemory;")
replace("memory.c", '#include "xsdk/gpu.h"', '#include "xsdk/gpu.h"\n#include "xsdk/kernel.h"')
replace("memory.c", "if (type != MEMORY_TYPE_GPU && type != MEMORY_TYPE_CACHED)", "if (type != MEMORY_TYPE_GPU && type != MEMORY_TYPE_CACHED && type != 2)")
replace("memory.c", "\tmemory->cached = type == MEMORY_TYPE_CACHED;", "\tmemory->cached = type == MEMORY_TYPE_CACHED;\n\tmemory->physical_cached = type == 2;")
replace("memory.c", "memory->cached ? memalign(CACHED_ALIGNMENT, memory->size_bytes) : xsdk_gpu_alloc(memory->size_bytes);", """memory->physical_cached
\t\t? MmAllocatePhysicalMemoryEx(0, (memory->size_bytes + 4095) & ~4095u, PAGE_READWRITE | MEM_LARGE_PAGES, 0, UINT32_MAX, 4096)
\t\t: memory->cached ? memalign(CACHED_ALIGNMENT, memory->size_bytes) : xsdk_gpu_alloc(memory->size_bytes);""")
replace("image.c", ".memoryTypeBits = MEMORY_TYPE_BITS,", ".memoryTypeBits = MEMORY_TYPE_BITS | (object->is_texture && !object->texture.tiled ? 4u : 0u),")
# O tipo 2 usa o mesmo endereço físico que a textura; o tipo 1 continua sendo somente staging POSIX.
# O flush é explícito e roda na thread que recebe o quadro completo do FFmpeg.
start = "\t(void)device;\n\t(void)memoryRangeCount;\n\t(void)pMemoryRanges;\n\treturn VK_SUCCESS;\n}"
path = root / "src/memory.c"
text = path.read_text()
if text.count(start) != 2:
    raise RuntimeError("flush/invalidate incompatíveis")
for instruction in ("dcbst", "dcbf"):
    body = """\t(void)device;
\tfor (uint32_t i = 0; i < memoryRangeCount; ++i) {
\t\tconst DeviceMemory *memory = v4x_object(pMemoryRanges[i].memory);
\t\tif (!memory->physical_cached) continue;
\t\tconst uint32_t offset = (uint32_t)pMemoryRanges[i].offset;
\t\tconst uint32_t size = pMemoryRanges[i].size == VK_WHOLE_SIZE ? memory->size_bytes - offset : (uint32_t)pMemoryRanges[i].size;
\t\tconst uintptr_t begin = (uintptr_t)memory->pointer + offset;
\t\tfor (uintptr_t p = begin & ~(uintptr_t)127; p < begin + size; p += 128)
\t\t\t__asm__ volatile("INSTRUCTION 0, %0" :: "r"(p) : "memory");
\t}
\t__asm__ volatile("sync" ::: "memory");
\treturn VK_SUCCESS;
}""".replace("INSTRUCTION", instruction)
    text = text.replace(start, body, 1)
path.write_text(text)
stamp.write_text("R8 linear sampling; memory type 2 physical cached, explicit 128-byte flush\n")
