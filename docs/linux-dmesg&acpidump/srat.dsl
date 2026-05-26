/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20251212 (64-bit version)
 * Copyright (c) 2000 - 2025 Intel Corporation
 * 
 * Disassembly of srat.dat
 *
 * ACPI Data Table [SRAT]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue (in hex)
 */

[000h 0000 004h]                   Signature : "SRAT"    [System Resource Affinity Table]
[004h 0004 004h]                Table Length : 00000090
[008h 0008 001h]                    Revision : 01
[009h 0009 001h]                    Checksum : 64
[00Ah 0010 006h]                      Oem ID : "BOCHS "
[010h 0016 008h]                Oem Table ID : "BXPC    "
[018h 0024 004h]                Oem Revision : 00000001
[01Ch 0028 004h]             Asl Compiler ID : "BXPC"
[020h 0032 004h]       Asl Compiler Revision : 00000001

[024h 0036 004h]              Table Revision : 00000001
[028h 0040 008h]                    Reserved : 0000000000000000

[030h 0048 001h]               Subtable Type : 00 [Processor Local APIC/SAPIC Affinity]
[031h 0049 001h]                      Length : 10

[032h 0050 001h]     Proximity Domain Low(8) : 00
[033h 0051 001h]                     Apic ID : 00
[034h 0052 004h]       Flags (decoded below) : 00000001
                                     Enabled : 1
[038h 0056 001h]             Local Sapic EID : 00
[039h 0057 003h]   Proximity Domain High(24) : 000000
[03Ch 0060 004h]                Clock Domain : 00000000

[040h 0064 001h]               Subtable Type : 01 [Memory Affinity]
[041h 0065 001h]                      Length : 28

[042h 0066 004h]            Proximity Domain : 00000000
[046h 0070 002h]                   Reserved1 : 0000
[048h 0072 008h]                Base Address : 0000000000000000
[050h 0080 008h]              Address Length : 0000000010000000
[058h 0088 004h]                   Reserved2 : 00000000
[05Ch 0092 004h]       Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[060h 0096 008h]                   Reserved3 : 0000000000000000

[068h 0104 001h]               Subtable Type : 01 [Memory Affinity]
[069h 0105 001h]                      Length : 28

[06Ah 0106 004h]            Proximity Domain : 00000000
[06Eh 0110 002h]                   Reserved1 : 0000
[070h 0112 008h]                Base Address : 0000000080000000
[078h 0120 008h]              Address Length : 0000000070000000
[080h 0128 004h]                   Reserved2 : 00000000
[084h 0132 004h]       Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[088h 0136 008h]                   Reserved3 : 0000000000000000

Raw Table Data: Length 144 (0x90)

    0000: 53 52 41 54 90 00 00 00 01 64 42 4F 43 48 53 20  // SRAT.....dBOCHS 
    0010: 42 58 50 43 20 20 20 20 01 00 00 00 42 58 50 43  // BXPC    ....BXPC
    0020: 01 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0030: 00 10 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0040: 01 28 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // .(..............
    0050: 00 00 00 10 00 00 00 00 00 00 00 00 01 00 00 00  // ................
    0060: 00 00 00 00 00 00 00 00 01 28 00 00 00 00 00 00  // .........(......
    0070: 00 00 00 80 00 00 00 00 00 00 00 70 00 00 00 00  // ...........p....
    0080: 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
