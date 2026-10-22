/* Exercise production file/memory readers against both shipped RTLS7 layouts. */
#include <assert.h>
#include "../runner/src/common_rtl.c"

static void check_layout(size_t size) {
  uint8_t bytes[230] = {0};
  const uint32_t ppu[2] = {0x30555050u,
      PPU_SAVESTATE_REGS_SIZE + PPU_SAVESTATE_MEM_SIZE};
  uint32_t timer = 0x12345678u;
  size_t timer_offset = size == 216 ? 208 : 210;
  for (unsigned i = 0; i < 208; ++i) bytes[i] = (uint8_t)(i * 17);
  memcpy(bytes + timer_offset, &timer, 4);
  bytes[timer_offset + 4] = 1;
  memcpy(bytes + size, ppu, sizeof(ppu));
  for (unsigned file = 0; file < 2; ++file) {
    Dma dma = {0};
    uint32_t following[2];
    MemorySli memory = {{memory_sli_func, memory_sli_peek},
                       bytes, size + 8, 0, false, false};
    FILE *stream = tmpfile();
    assert(stream);
    assert(fwrite(bytes, 1, size + 8, stream) == size + 8);
    rewind(stream);
    FileSli disk = {{file_sli_func, file_sli_peek}, stream, false, false};
    SaveLoadInfo *reader = file ? &disk.base : &memory.base;
    dma_saveload(&dma, reader);
    assert(dma.dmaTimer == timer && dma.dmaBusy);
    assert(memcmp(dma.channel, bytes, 208) == 0);
    reader->func(reader, following, sizeof(following));
    assert(memcmp(following, ppu, sizeof(ppu)) == 0);
    assert(!disk.error && !memory.error);
    fclose(stream);

    uint8_t saved[222];
    MemorySli output = {{memory_sli_func, memory_sli_peek},
                       saved, sizeof(saved), 0, true, false};
    dma_saveload(&dma, &output.base);
    assert(!output.error && output.position == sizeof(saved));
    assert(memcmp(saved, bytes, 208) == 0);
    assert(memcmp(saved + 210, &timer, 4) == 0 && saved[214] == 1);
    assert(saved[208] == 0 && saved[209] == 0 && saved[221] == 0);
  }
}
int main(void) {
  check_layout(216);
  check_layout(222);
  puts("DMA snapshots: legacy/current layouts, file/memory readers and stable writes PASS");
  return 0;
}
