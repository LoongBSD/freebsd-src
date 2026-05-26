/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20251212 (64-bit version)
 * Copyright (c) 2000 - 2025 Intel Corporation
 * 
 * Disassembly of pptt.dat
 *
 * ACPI Data Table [PPTT]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue (in hex)
 */

[000h 0000 004h]                   Signature : "PPTT"    [Processor Properties Topology Table]
[004h 0004 004h]                Table Length : 00000060
[008h 0008 001h]                    Revision : 02
[009h 0009 001h]                    Checksum : 27
[00Ah 0010 006h]                      Oem ID : "BOCHS "
[010h 0016 008h]                Oem Table ID : "BXPC    "
[018h 0024 004h]                Oem Revision : 00000001
[01Ch 0028 004h]             Asl Compiler ID : "BXPC"
[020h 0032 004h]       Asl Compiler Revision : 00000001


[024h 0036 001h]               Subtable Type : 00 [Processor Hierarchy Node]
[025h 0037 001h]                      Length : 14
[026h 0038 002h]                    Reserved : 0000
[028h 0040 004h]       Flags (decoded below) : 00000011
                            Physical package : 1
                     ACPI Processor ID valid : 0
                       Processor is a thread : 0
                              Node is a leaf : 0
                    Identical Implementation : 1
[02Ch 0044 004h]                      Parent : 00000000
[030h 0048 004h]           ACPI Processor ID : 00000000
[034h 0052 004h]     Private Resource Number : 00000000

[038h 0056 001h]               Subtable Type : 00 [Processor Hierarchy Node]
[039h 0057 001h]                      Length : 14
[03Ah 0058 002h]                    Reserved : 0000
[03Ch 0060 004h]       Flags (decoded below) : 00000011
                            Physical package : 1
                     ACPI Processor ID valid : 0
                       Processor is a thread : 0
                              Node is a leaf : 0
                    Identical Implementation : 1
[040h 0064 004h]                      Parent : 00000024
[044h 0068 004h]           ACPI Processor ID : 00000000
[048h 0072 004h]     Private Resource Number : 00000000

[04Ch 0076 001h]               Subtable Type : 00 [Processor Hierarchy Node]
[04Dh 0077 001h]                      Length : 14
[04Eh 0078 002h]                    Reserved : 0000
[050h 0080 004h]       Flags (decoded below) : 0000000A
                            Physical package : 0
                     ACPI Processor ID valid : 1
                       Processor is a thread : 0
                              Node is a leaf : 1
                    Identical Implementation : 0
[054h 0084 004h]                      Parent : 00000038
[058h 0088 004h]           ACPI Processor ID : 00000000
[05Ch 0092 004h]     Private Resource Number : 00000000

Raw Table Data: Length 96 (0x60)

    0000: 50 50 54 54 60 00 00 00 02 27 42 4F 43 48 53 20  // PPTT`....'BOCHS 
    0010: 42 58 50 43 20 20 20 20 01 00 00 00 42 58 50 43  // BXPC    ....BXPC
    0020: 01 00 00 00 00 14 00 00 11 00 00 00 00 00 00 00  // ................
    0030: 00 00 00 00 00 00 00 00 00 14 00 00 11 00 00 00  // ................
    0040: 24 00 00 00 00 00 00 00 00 00 00 00 00 14 00 00  // $...............
    0050: 0A 00 00 00 38 00 00 00 00 00 00 00 00 00 00 00  // ....8...........
