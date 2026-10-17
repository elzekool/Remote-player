# Places the Helix MP3 decoder (libhelix.a) in RAM instead of flash.
#
# The decoder's code and tables (~32 KB) don't fit in the 16 KB flash (XIP) cache, so every MP3
# stream caused ~180,000 cache misses per second, each stalling the CPU while flash is read.
# The core's linker script keeps a few libraries out of the flash .text and .rodata sections;
# whatever is kept out ends up in .data, which is copied to RAM at start-up. This adds
# libhelix.a to those exclusions each time the core generates the linker script.
#
# Others found the same with MP3 decoders running from flash:
# - Helix on NXP LPC4330 from SPI flash, advised to run the decoder from internal RAM:
#   https://community.nxp.com/t5/LPC-Microcontrollers/Slow-playback-of-mp3-data/m-p/581573
# - libmad on RP2350, frame decode 13.1 ms -> 4.6 ms after moving hot state to SRAM:
#   https://github.com/PicoDeck/picodeck/issues/28

Import("env")

LINKER_SCRIPT = "$BUILD_DIR/memmap_default.ld"
EXCLUSIONS = "EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:)"
WITH_HELIX = "EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a: *libhelix.a:)"


def place_helix_in_ram(target, source, env):
    path = target[0].get_abspath()
    with open(path) as f:
        script = f.read()
    # One exclusion list for code (.text), one for constant tables (.rodata).
    if script.count(EXCLUSIONS) != 2:
        raise Exception(
            "helix_in_ram.py: the core's linker script has changed; update EXCLUSIONS")
    with open(path, "w") as f:
        f.write(script.replace(EXCLUSIONS, WITH_HELIX))
    print("Placed the Helix MP3 decoder in RAM")


env.AddPostAction(LINKER_SCRIPT, place_helix_in_ram)
