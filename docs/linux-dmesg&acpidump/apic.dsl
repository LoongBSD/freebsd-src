/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20251212 (64-bit version)
 * Copyright (c) 2000 - 2025 Intel Corporation
 * 
 * Disassembly of apic.dat
 *
 * ACPI Data Table [APIC]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue (in hex)
 */

[000h 0000 004h]                   Signature : "APIC"    [Multiple APIC Description Table (MADT)]
[004h 0004 004h]                Table Length : 0000006C
[008h 0008 001h]                    Revision : 01
[009h 0009 001h]                    Checksum : F5
[00Ah 0010 006h]                      Oem ID : "BOCHS "
[010h 0016 008h]                Oem Table ID : "BXPC    "
[018h 0024 004h]                Oem Revision : 00000001
[01Ch 0028 004h]             Asl Compiler ID : "BXPC"
[020h 0032 004h]       Asl Compiler Revision : 00000001

[024h 0036 004h]          Local Apic Address : 00000000
[028h 0040 004h]       Flags (decoded below) : 00000001
                         PC-AT Compatibility : 1

[02Ch 0044 001h]               Subtable Type : 11 [CPU Core Interrupt Controller]
[02Dh 0045 001h]                      Length : 0F
[02Eh 0046 001h]                     Version : 01
[02Fh 0047 004h]                 ProcessorId : 00000000
[033h 0051 004h]                      CoreId : 00000000
[037h 0055 004h]                       Flags : 00000001

[03Bh 0059 001h]               Subtable Type : 14 [Extend I/O Interrupt Controller]
[03Ch 0060 001h]                      Length : 0D
[03Dh 0061 001h]                     Version : 01
[03Eh 0062 001h]                     Cascade : 03
[03Fh 0063 001h]                        Node : 00
[040h 0064 008h]                     NodeMap : 000000000000FFFF

[048h 0072 001h]               Subtable Type : 15 [MSI Interrupt Controller]
[049h 0073 001h]                      Length : 13
[04Ah 0074 001h]                     Version : 01
[04Bh 0075 008h]                  MsgAddress : 000000002FF00000
[053h 0083 004h]                       Start : 00000040
[057h 0087 004h]                       Count : 000000C0

[05Bh 0091 001h]               Subtable Type : 16 [Bridge I/O Interrupt Controller]
[05Ch 0092 001h]                      Length : 11
[05Dh 0093 001h]                     Version : 01
[05Eh 0094 008h]                     Address : 0000000010000000
[066h 0102 002h]                        Size : 1000
[068h 0104 002h]                          Id : 0000
[06Ah 0106 002h]                     GsiBase : 0040

Raw Table Data: Length 108 (0x6C)

    0000: 41 50 49 43 6C 00 00 00 01 F5 42 4F 43 48 53 20  // APICl.....BOCHS 
    0010: 42 58 50 43 20 20 20 20 01 00 00 00 42 58 50 43  // BXPC    ....BXPC
    0020: 01 00 00 00 00 00 00 00 01 00 00 00 11 0F 01 00  // ................
    0030: 00 00 00 00 00 00 00 01 00 00 00 14 0D 01 03 00  // ................
    0040: FF FF 00 00 00 00 00 00 15 13 01 00 00 F0 2F 00  // ............../.
    0050: 00 00 00 40 00 00 00 C0 00 00 00 16 11 01 00 00  // ...@............
    0060: 00 10 00 00 00 00 00 10 00 00 40 00              // ..........@.
