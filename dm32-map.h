/*
 * Address map of Baofeng DM-32UV memory space.
 */
{ 0x000000, 0x1000, 0x000000 },   // 000000 - Firmware header, version tag
{ 0x0001000, 0x1000, 0x0001000 }, // 001000 - Boot banner strings
{ 0x0002000, 0x1000, 0x0002000 }, // 002000 - Reserved/identity fields
{ 0x0003000, 0x1000, 0x0003000 }, // 003000 - Reserved
{ 0x0004000, 0x1000, 0x0004000 }, // 004000 - Messages / UI strings
{ 0x0005000, 0x1000, 0x0005000 }, // 005000 - Messages / UI strings
{ 0x0006000, 0x1000, 0x0006000 }, // 006000 - Resource catalogue
{ 0x0007000, 0x1000, 0x0007000 }, // 007000 - V-frame metadata
{ 0x0008000, 0x1000, 0x0008000 }, // 008000 - Scan lists
{ 0x0009000, 0x1000, 0x0009000 }, // 009000 - Roam lists
{ 0x000a000, 0x1000, 0x000a000 }, // 00a000 - Reserved
{ 0x000b000, 0x1000, 0x000b000 }, // 00b000 - Reserved
{ 0x000c000, 0x1000, 0x000c000 }, // 00c000 - Reserved
{ 0x000d000, 0x1000, 0x000d000 }, // 00d000 - Reserved
{ 0x000f000, 0x1000, 0x000f000 }, // 00f000 - Zone table prefix
{ 0x0011000, 0x1000, 0x0011000 }, // 011000 - Zone table window
{ 0x0012000, 0x1000, 0x0012000 }, // 012000 - Zone table window
{ 0x001f000, 0x1000, 0x001f000 }, // 01f000 - Pointer tables / indices
{ 0, 0, 0 },
