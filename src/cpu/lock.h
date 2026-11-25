#ifndef DOSBOX_CPU_LOCK_H
#define DOSBOX_CPU_LOCK_H

/*
 *  Copyright (C) 2026  The DOSBox-X Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <cstdint>
#include "cpu.h"
#include "regs.h"

// ModR/M helpers: mod==3 selects a register operand (no memory access,
// so not a valid LOCK target); reg is the middle 3-bit opcode-extension
// field used by several instruction groups.
static INLINE bool ModRmIsMemory(uint8_t modrm) { return (modrm>>6)!=3; }
static INLINE uint8_t ModRmReg(uint8_t modrm) { return (modrm>>3)&7; }

// Checks an instruction that carries a LOCK prefix. 'start' is the address of
// the instruction's first byte (including prefixes before LOCK) and read_byte
// must return the bytes the decoder executes. Returns -1 when the instruction
// may run, otherwise the exception to raise (error code 0).
template <typename Address, typename ReadByte>
static INLINE int CPU_LockPrefixFault(Address start, ReadByte read_byte) {
	// The 8086/80186 never fault on LOCK. The 80286 does only in protected
	// mode when CPL > IOPL, and accepts LOCK on any instruction otherwise.
	if (CPU_ArchitectureType < CPU_ARCHTYPE_286) return -1;
	if (CPU_ArchitectureType < CPU_ARCHTYPE_386) {
		if (cpu.pmode && cpu.cpl > ((reg_flags & FLAG_IOPL) >> 12))
			return EXCEPTION_GP;
		return -1;
	}

	// The 386 and later raise #GP for instructions longer than 15 bytes.
	const unsigned max_length = 15;
	unsigned length = 0;
	bool address32 = cpu.code.big;
	bool operand32 = cpu.code.big;
	Address ip = start;
	uint8_t opcode;
	for (;;) {
		if (++length > max_length) return EXCEPTION_GP;
		opcode = read_byte(ip++);
		if (opcode == 0x66) operand32 = !cpu.code.big;
		else if (opcode == 0x67) address32 = !cpu.code.big;
		else if (!(opcode == 0x26 || opcode == 0x2e || opcode == 0x36 || opcode == 0x3e ||
			   opcode == 0x64 || opcode == 0x65 ||
			   opcode == 0xf0 || opcode == 0xf2 || opcode == 0xf3)) // Redundant LOCK/REP prefixes are allowed
			break;
	}

	// Classify the opcode before reading its ModR/M byte: for other opcodes
	// that read could fault on the next page and raise #PF instead of #UD.
	const bool two_byte = opcode == 0x0f;
	if (two_byte) {
		if (++length > max_length) return EXCEPTION_GP;
		opcode = read_byte(ip++);
		switch (opcode) {
			case 0xab: // BTS
			case 0xb0: case 0xb1: // CMPXCHG
			case 0xb3: // BTR
			case 0xbb: // BTC
			case 0xc0: case 0xc1: // XADD
			case 0xba: // BTS/BTR/BTC imm8
			case 0xc7: // CMPXCHG8B/CMPXCHG16B
				break;
			default: // Including BT (0F A3), which faults on the 386 as on later CPUs
				return EXCEPTION_UD;
		}
	} else {
		switch (opcode) {
			// ADD/OR/ADC/SBB/AND/SUB/XOR Eb/Ev,Gb/Gv (r/m is destination;
			// CMP's forms 0x38/0x39 are excluded since CMP does not write
			// its operand)
			case 0x00: case 0x01: case 0x08: case 0x09:
			case 0x10: case 0x11: case 0x18: case 0x19:
			case 0x20: case 0x21: case 0x28: case 0x29:
			case 0x30: case 0x31:
			case 0x86: case 0x87: // XCHG Eb/Ev,Gb/Gv
			case 0x80: case 0x81: case 0x82: case 0x83: // group 1 imm
			case 0xfe: case 0xff: // group 4/5
			case 0xf6: case 0xf7: // group 3
				break;
			default:
				return EXCEPTION_UD;
		}
	}

	if (length + 1 > max_length) return EXCEPTION_GP;
	const uint8_t modrm = read_byte(ip);
	unsigned immediate = 0;
	if (two_byte) {
		if (opcode == 0xba) { // BT itself (reg 4) is excluded
			if (ModRmReg(modrm) < 5) return EXCEPTION_UD;
			immediate = 1;
		} else if (opcode == 0xc7 && ModRmReg(modrm) != 1) {
			return EXCEPTION_UD;
		}
	} else {
		switch (opcode) {
			case 0x80: case 0x81: case 0x82: case 0x83: // excl. CMP
				if (ModRmReg(modrm) == 7) return EXCEPTION_UD;
				immediate = opcode == 0x81 ? (operand32 ? 4 : 2) : 1;
				break;
			case 0xfe: case 0xff: // INC/DEC
				if (ModRmReg(modrm) > 1) return EXCEPTION_UD;
				break;
			case 0xf6: case 0xf7: // NOT/NEG
				if (ModRmReg(modrm) != 2 && ModRmReg(modrm) != 3) return EXCEPTION_UD;
				break;
		}
	}
	if (!ModRmIsMemory(modrm)) return EXCEPTION_UD;

	// Count ModR/M, SIB, displacement and immediate bytes.
	const unsigned mod = modrm >> 6, rm = modrm & 7;
	length++;
	if (address32) {
		if (rm == 4) {
			length++;
			const uint8_t sib = read_byte(ip + 1);
			if (mod == 0 && (sib & 7) == 5) length += 4;
		} else if (mod == 0 && rm == 5) {
			length += 4;
		}
		if (mod == 1) length += 1;
		else if (mod == 2) length += 4;
	} else {
		if (mod == 0 && rm == 6) length += 2;
		else if (mod == 1) length += 1;
		else if (mod == 2) length += 2;
	}
	length += immediate;
	return length > max_length ? EXCEPTION_GP : -1;
}

#endif
