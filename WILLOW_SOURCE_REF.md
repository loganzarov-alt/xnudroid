# Redmi Note 8T / willow source-reference bring-up

This branch is the control path for the XNU-on-Android bring-up experiment.

## Sources used

- Xiaomi kernel: `MiCode/Xiaomi_Kernel_OpenSource`, branch `willow-p-oss`.
- Device architecture/config: `vendor/trinket-perf_defconfig` (`ARCH_TRINKET`, DRM/SDE, appended DTB).
- TWRP device tree reference: `TeamWin/android_device_xiaomi_ginkgo` (unified ginkgo/willow tree).

## What v0.4 does

The workflow builds the official Xiaomi kernel without replacing its trinket/SDE/DSI implementation. It then creates a tiny ARM64 initramfs. The init process waits for the framebuffer exposed by the kernel and draws `ZAROS` in the middle of the screen.

The boot image is packed as Android boot header v2 using the same core layout values published by the TWRP device tree (base 0, 4096-byte pages, ramdisk offset 0x01000000, tags offset 0x100).

## Test policy

Use the produced image only as a temporary boot test (`fastboot boot ...`). Do not flash it to boot/recovery while the bring-up is experimental.

If the kernel boots but no visible framebuffer node is exposed, the next version will switch the tiny init from the framebuffer API to the kernel's native DRM/KMS interface rather than guessing a physical framebuffer address.
