//  h6280.c - CPU Emulation
//
/* 2026-11-12 (GPLv3 section 5a) PCEngine for TTGO VGA32:
 *  - This file is compiled with O3 and with jump tables (the Arduino core for the ESP32 compiles everything
 *    with -Os -fno-jump-tables, which turns the 256-way opcode switch into about 8 levels of compare and branch).
 *  - h6280_run keeps PC/A/X/Y/P/S and the cycle counter in local variables (h6280_regs_t; every opcode function
 *    is inlined, so the compiler keeps them in registers) and writes them back when it returns. The I/O handlers
 *    read PCE.Cycles (timer, PSG queue), so it is written before every I/O call. The interrupt lines and mask
 *    stay in PCE.CPU (the I/O handlers and the VDC change them).
 *  - The block transfers run in h6280_xfer(): memory to memory within one bank, and TIA to the VDC data port
 *    (VRAM write), are done with plain loops; everything else goes byte by byte as before. */
#pragma GCC optimize("O3")
#if defined(__GNUC__) && defined(ARDUINO)
#pragma GCC optimize("jump-tables")
#endif

#include <stdio.h>
#include <stdlib.h>

#include "pce-go.h"
#include "pce.h"

#ifdef ARDUINO
#include <esp_attr.h>
#define PCE_FAST IRAM_ATTR
#else
#define PCE_FAST
#endif

#define OPCODE(n, f) case n: f; break;
#define H_INLINE static inline __attribute__((__always_inline__))

#include "h6280_dbg.h"


/**
 * Reset CPU
 **/
void
h6280_reset(void)
{
	CPU.A = CPU.X = CPU.Y = 0x00;
	CPU.P = (FL_I|FL_B);
	CPU.S = 0xFF;
	CPU.PC = pce_read16(VEC_RESET);
	CPU.irq_mask = CPU.irq_mask_delay = CPU.irq_lines = 0;
}


/**
 * Log assembler instructions starting at current PC
 **/
void
h6280_debug(void)
{
	//
}


typedef struct
{
	uint16_t PC;
	uint8_t A;
	uint8_t X;
	uint8_t Y;
	uint8_t P;
	uint8_t S;
	int32_t cyc;
	uint8_t *io;
} h6280_regs_t;

#define H6280_REGS h6280_regs_t *const R

H_INLINE h6280_t *
h6280_global(void)
{
	return &CPU;
}

H_INLINE int32_t
h6280_global_cycles(void)
{
	return PCE.Cycles;
}

H_INLINE void
regs_load(H6280_REGS)
{
	R->PC = CPU.PC;
	R->A = CPU.A;
	R->X = CPU.X;
	R->Y = CPU.Y;
	R->P = CPU.P;
	R->S = CPU.S;
	R->cyc = PCE.Cycles;
	R->io = PCE.IOAREA;
}

H_INLINE void
regs_store(H6280_REGS)
{
	CPU.PC = R->PC;
	CPU.A = R->A;
	CPU.X = R->X;
	CPU.Y = R->Y;
	CPU.P = R->P;
	CPU.S = R->S;
	PCE.Cycles = R->cyc;
}

H_INLINE uint8_t
cpu_read8(H6280_REGS, uint16_t addr)
{
	uint8_t *page = PageR[addr >> 13];

	if (page == R->io) {
		PCE.Cycles = R->cyc;
		return pce_readIO(addr);
	}
	return page[addr];
}

H_INLINE void
cpu_write8(H6280_REGS, uint16_t addr, uint8_t byte)
{
	uint8_t *page = PageW[addr >> 13];

	if (page == R->io) {
		PCE.Cycles = R->cyc;
		pce_writeIO(addr, byte);
	} else
		page[addr] = byte;
}

H_INLINE void
cpu_vdc_write(H6280_REGS, unsigned port, uint8_t byte)
{
	PCE.Cycles = R->cyc;
	pce_writeVDC(port, byte);
}


enum { XFER_TII, XFER_TDD, XFER_TIN, XFER_TIA, XFER_TAI };

static void PCE_FAST __attribute__((noinline))
h6280_xfer(unsigned mode, unsigned from, unsigned to, unsigned len)
{
	uint8_t *const io = PCE.IOAREA;
	uint8_t *const rpage = PageR[from >> 13];
	uint8_t *const wpage = PageW[to >> 13];
	const unsigned fofs = from & 0x1FFF, tofs = to & 0x1FFF;
	bool src_ok = false;

	if (rpage != io) {
		switch (mode) {
		case XFER_TDD: src_ok = fofs + 1 >= len; break;
		case XFER_TAI: src_ok = fofs != 0x1FFF; break;
		default:       src_ok = fofs + len <= 0x2000; break;
		}
	}

	if (src_ok) {
		const uint8_t *s = rpage + from;

		if (wpage != io) {
			bool dst_ok;
			switch (mode) {
			case XFER_TDD: dst_ok = tofs + 1 >= len; break;
			case XFER_TIN: dst_ok = true; break;
			case XFER_TIA: dst_ok = tofs != 0x1FFF; break;
			default:       dst_ok = tofs + len <= 0x2000; break;
			}
			if (dst_ok) {
				uint8_t *d = wpage + to;
				unsigned i;
				switch (mode) {
				case XFER_TII: for (i = 0; i < len; i++) d[i] = s[i]; break;
				case XFER_TDD: for (i = 0; i < len; i++) *d-- = *s--; break;
				case XFER_TIN: for (i = 0; i < len; i++) *d = s[i]; break;
				case XFER_TIA: for (i = 0; i < len; i++) d[i & 1] = s[i]; break;
				default:       for (i = 0; i < len; i++) d[i] = s[i & 1]; break;
				}
				return;
			}
		} else if (mode == XFER_TIA && to == 0x0002 && PCE.VDC.reg == VWR) {
			UWord *const vr = PCE.VDC.regs;
			uint16_t *const vram = PCE.VRAM;
			const unsigned step = IO_VDC_INC_STEP();
			unsigned mawr = vr[MAWR].W;
			unsigned i;
			for (i = 0; i + 1 < len; i += 2) {
				if (mawr < 0x8000)
					vram[mawr] = (uint16_t)((s[i + 1] << 8) | s[i]);
				mawr = (mawr + step) & 0xFFFF;
			}
			vr[MAWR].W = (uint16_t)mawr;
			if (len & 1) {
				vr[VWR].B.l = s[len - 1];
				if (len > 1)
					vr[VWR].B.h = s[len - 2];
			} else {
				vr[VWR].B.l = s[len - 2];
				vr[VWR].B.h = s[len - 1];
			}
			return;
		}
	}

	{
		uint16_t f = (uint16_t)from, t = (uint16_t)to;
		unsigned alternate = 0;
		switch (mode) {
		case XFER_TII: while (len-- != 0) { pce_write8(t++, pce_read8(f++)); } break;
		case XFER_TDD: while (len-- != 0) { pce_write8(t--, pce_read8(f--)); } break;
		case XFER_TIN: while (len-- != 0) { pce_write8(t, pce_read8(f++)); } break;
		case XFER_TIA: while (len-- != 0) { pce_write8((uint16_t)(t + alternate), pce_read8(f++)); alternate ^= 1; } break;
		default:       while (len-- != 0) { pce_write8(t++, pce_read8((uint16_t)(f + alternate))); alternate ^= 1; } break;
		}
	}
}

H_INLINE void
cpu_xfer(H6280_REGS, unsigned mode, unsigned from, unsigned to, unsigned len)
{
	PCE.Cycles = R->cyc;
	h6280_xfer(mode, from, to, len);
}


#undef CPU
#define CPU (*R)
#define GCPU (*h6280_global())
#define Cycles (R->cyc)
#define pce_read8(addr) cpu_read8(R, (addr))
#define pce_write8(addr, byte) cpu_write8(R, (addr), (byte))

#include "h6280_instr.h"


/**
 * Branch into interrupt
 **/
void
h6280_irq(int type)
{
	h6280_regs_t regs, *const R = &regs;

	regs_load(R);
	interrupt(R, type);
	regs_store(R);
}


/**
 * CPU emulation
 **/
void PCE_FAST
h6280_run(int max_cycles)
{
	/* Handle active block transfers, ie: do nothing. (tai/tdd/tia/tin/tii) */
	if (h6280_global_cycles() >= max_cycles) {
		return;
	}

	h6280_regs_t regs, *const R = &regs;

	regs_load(R);

	/* Handle pending interrupts (Should be in the loop, but it's too slow) */
	/* 2026-11-26 (GPLv3 section 5a) PCEngine for TTGO VGA32: a pending interrupt is taken after one instruction
	   (the pass with limit = Cycles + 1), not before it. The VDC raises its interrupt between two runs, and a game that
	   polls the VDC status with the interrupt enabled (Dungeon Explorer on the title after button I: LDA #$20 /
	   BIT $0000 / BEQ) never saw the flag, because the handler always read (and cleared) it first. On the real chip
	   it is a race the polling loop wins every few frames; now it can win when its BIT comes first. */
	unsigned irq = GCPU.irq_lines & ~GCPU.irq_mask & INT_MASK;
	int pend = ((CPU.P & FL_I) == 0 && irq);
	int limit = pend ? Cycles + 1 : max_cycles;

again:
	/* Run for roughly one scanline */
	while (Cycles < limit)
	{
		UBYTE opcode = imm_operand(CPU.PC);

		TRACE_CPU("0x%4X: %s\n", CPU.PC, opcodes[opcode].name);

		switch (opcode)
		{
			OPCODE(0x00, brk(R));				// BRK
			OPCODE(0x01, ora_zpindx(R));			// ORA (IND,X)
			OPCODE(0x02, sxy(R));				// SXY
			OPCODE(0x03, st0(R));				// ST0 #$nn
			OPCODE(0x04, tsb_zp(R));				// TSB $ZZ
			OPCODE(0x05, ora_zp(R));				// ORA $ZZ
			OPCODE(0x06, asl_zp(R));				// ASL $ZZ
			OPCODE(0x07, rmb(R, 0));				// RMB0 $ZZ
			OPCODE(0x08, php(R));				// PHP
			OPCODE(0x09, ora_imm(R));			// ORA #$nn
			OPCODE(0x0A, asl_a(R));				// ASL A
			OPCODE(0x0C, tsb_abs(R));			// TSB $hhll
			OPCODE(0x0D, ora_abs(R));			// ORA $hhll
			OPCODE(0x0E, asl_abs(R));			// ASL $hhll
			OPCODE(0x0F, bbr(R, 0));				// BBR0 $ZZ,$rr

			OPCODE(0x10, bpl(R));				// BPL REL
			OPCODE(0x11, ora_zpindy(R));			// ORA (IND),Y
			OPCODE(0x12, ora_zpind(R));			// ORA (IND)
			OPCODE(0x13, st1(R));				// ST1 #$nn
			OPCODE(0x14, trb_zp(R));				// TRB $ZZ
			OPCODE(0x15, ora_zpx(R));			// ORA $ZZ,X
			OPCODE(0x16, asl_zpx(R));			// ASL $ZZ,X
			OPCODE(0x17, rmb(R, 1));				// RMB1 $ZZ
			OPCODE(0x18, clc(R));				// CLC
			OPCODE(0x19, ora_absy(R));			// ORA $hhll,Y
			OPCODE(0x1A, inc_a(R));				// INC A
			OPCODE(0x1C, trb_abs(R));			// TRB $hhll
			OPCODE(0x1D, ora_absx(R));			// ORA $hhll,X
			OPCODE(0x1E, asl_absx(R));			// ASL $hhll,X
			OPCODE(0x1F, bbr(R, 1));				// BBR1 $ZZ,$rr

			OPCODE(0x20, jsr(R));				// JSR $hhll
			OPCODE(0x21, and_zpindx(R));			// AND (IND,X)
			OPCODE(0x22, sax(R));				// SAX
			OPCODE(0x23, st2(R));				// ST2 #$nn
			OPCODE(0x24, bit_zp(R));				// BIT $ZZ
			OPCODE(0x25, and_zp(R));				// AND $ZZ
			OPCODE(0x26, rol_zp(R));				// ROL $ZZ
			OPCODE(0x27, rmb(R, 2));				// RMB2 $ZZ
			OPCODE(0x28, plp(R));				// PLP
			OPCODE(0x29, and_imm(R));			// AND #$nn
			OPCODE(0x2A, rol_a(R));				// ROL A
			OPCODE(0x2C, bit_abs(R));			// BIT $hhll
			OPCODE(0x2D, and_abs(R));			// AND $hhll
			OPCODE(0x2E, rol_abs(R));			// ROL $hhll
			OPCODE(0x2F, bbr(R, 2));				// BBR2 $ZZ,$rr

			OPCODE(0x30, bmi(R));				// BMI $rr
			OPCODE(0x31, and_zpindy(R));			// AND (IND),Y
			OPCODE(0x32, and_zpind(R));			// AND (IND)
			OPCODE(0x34, bit_zpx(R));			// BIT $ZZ,X
			OPCODE(0x35, and_zpx(R));			// AND $ZZ,X
			OPCODE(0x36, rol_zpx(R));			// ROL $ZZ,X
			OPCODE(0x37, rmb(R, 3));				// RMB3 $ZZ
			OPCODE(0x38, sec(R));				// SEC
			OPCODE(0x39, and_absy(R));			// AND $hhll,Y
			OPCODE(0x3A, dec_a(R));				// DEC A
			OPCODE(0x3C, bit_absx(R));			// BIT $hhll,X
			OPCODE(0x3D, and_absx(R));			// AND $hhll,X
			OPCODE(0x3E, rol_absx(R));			// ROL $hhll,X
			OPCODE(0x3F, bbr(R, 3));				// BBR3 $ZZ,$rr

			OPCODE(0x40, rti(R));				// RTI
			OPCODE(0x41, eor_zpindx(R));			// EOR (IND,X)
			OPCODE(0x42, say(R));				// SAY
			OPCODE(0x43, tma(R));				// TMAi
			OPCODE(0x44, bsr(R));				// BSR $rr
			OPCODE(0x45, eor_zp(R));				// EOR $ZZ
			OPCODE(0x46, lsr_zp(R));				// LSR $ZZ
			OPCODE(0x47, rmb(R, 4));				// RMB4 $ZZ
			OPCODE(0x48, pha(R));				// PHA
			OPCODE(0x49, eor_imm(R));			// EOR #$nn
			OPCODE(0x4A, lsr_a(R));				// LSR A
			OPCODE(0x4C, jmp(R));				// JMP $hhll
			OPCODE(0x4D, eor_abs(R));			// EOR $hhll
			OPCODE(0x4E, lsr_abs(R));			// LSR $hhll
			OPCODE(0x4F, bbr(R, 4));				// BBR4 $ZZ,$rr

			OPCODE(0x50, bvc(R));				// BVC $rr
			OPCODE(0x51, eor_zpindy(R));			// EOR (IND),Y
			OPCODE(0x52, eor_zpind(R));			// EOR (IND)
			OPCODE(0x53, tam(R));				// TAMi
			OPCODE(0x54, csl(R));				// CSL
			OPCODE(0x55, eor_zpx(R));			// EOR $ZZ,X
			OPCODE(0x56, lsr_zpx(R));			// LSR $ZZ,X
			OPCODE(0x57, rmb(R, 5));				// RMB5 $ZZ
			OPCODE(0x58, cli(R));				// CLI
			OPCODE(0x59, eor_absy(R));			// EOR $hhll,Y
			OPCODE(0x5A, phy(R));				// PHY
			OPCODE(0x5D, eor_absx(R));			// EOR $hhll,X
			OPCODE(0x5E, lsr_absx(R));			// LSR $hhll,X
			OPCODE(0x5F, bbr(R, 5));				// BBR5 $ZZ,$rr

			OPCODE(0x60, rts(R));				// RTS
			OPCODE(0x61, adc_zpindx(R));			// ADC ($ZZ,X)
			OPCODE(0x62, cla(R));				// CLA
			OPCODE(0x64, stz_zp(R));				// STZ $ZZ
			OPCODE(0x65, adc_zp(R));				// ADC $ZZ
			OPCODE(0x66, ror_zp(R));				// ROR $ZZ
			OPCODE(0x67, rmb(R, 6));				// RMB6 $ZZ
			OPCODE(0x68, pla(R));				// PLA
			OPCODE(0x69, adc_imm(R));			// ADC #$nn
			OPCODE(0x6A, ror_a(R));				// ROR A
			OPCODE(0x6C, jmp_absind(R));			// JMP ($hhll)
			OPCODE(0x6D, adc_abs(R));			// ADC $hhll
			OPCODE(0x6E, ror_abs(R));			// ROR $hhll
			OPCODE(0x6F, bbr(R, 6));				// BBR6 $ZZ,$rr

			OPCODE(0x70, bvs(R));				// BVS $rr
			OPCODE(0x71, adc_zpindy(R));			// ADC ($ZZ),Y
			OPCODE(0x72, adc_zpind(R));			// ADC ($ZZ)
			OPCODE(0x73, tii(R));				// TII $SHSL,$DHDL,$LHLL
			OPCODE(0x74, stz_zpx(R));			// STZ $ZZ,X
			OPCODE(0x75, adc_zpx(R));			// ADC $ZZ,X
			OPCODE(0x76, ror_zpx(R));			// ROR $ZZ,X
			OPCODE(0x77, rmb(R, 7));				// RMB7 $ZZ
			OPCODE(0x78, sei(R));				// SEI
			OPCODE(0x79, adc_absy(R));			// ADC $hhll,Y
			OPCODE(0x7A, ply(R));				// PLY
			OPCODE(0x7C, jmp_absindx(R))			// JMP $hhll,X
			OPCODE(0x7D, adc_absx(R));			// ADC $hhll,X
			OPCODE(0x7E, ror_absx(R));			// ROR $hhll,X
			OPCODE(0x7F, bbr(R, 7));				// BBR7 $ZZ,$rr

			OPCODE(0x80, bra(R));				// BRA $rr
			OPCODE(0x81, sta_zpindx(R));			// STA (IND,X)
			OPCODE(0x82, clx(R));				// CLX
			OPCODE(0x83, tstins_zp(R));			// TST #$nn,$ZZ
			OPCODE(0x84, sty_zp(R));				// STY $ZZ
			OPCODE(0x85, sta_zp(R));				// STA $ZZ
			OPCODE(0x86, stx_zp(R));				// STX $ZZ
			OPCODE(0x87, smb(R, 0));				// SMB0 $ZZ
			OPCODE(0x88, dey(R));				// DEY
			OPCODE(0x89, bit_imm(R));			// BIT #$nn
			OPCODE(0x8A, txa(R));				// TXA
			OPCODE(0x8C, sty_abs(R));			// STY $hhll
			OPCODE(0x8D, sta_abs(R));			// STA $hhll
			OPCODE(0x8E, stx_abs(R));			// STX $hhll
			OPCODE(0x8F, bbs(R, 0));				// BBS0 $ZZ,$rr

			OPCODE(0x90, bcc(R));				// BCC $rr
			OPCODE(0x91, sta_zpindy(R));			// STA (IND),Y
			OPCODE(0x92, sta_zpind(R));			// STA (IND)
			OPCODE(0x93, tstins_abs(R));			// TST #$nn,$hhll
			OPCODE(0x94, sty_zpx(R));			// STY $ZZ,X
			OPCODE(0x95, sta_zpx(R));			// STA $ZZ,X
			OPCODE(0x96, stx_zpy(R));			// STX $ZZ,Y
			OPCODE(0x97, smb(R, 1));				// SMB1 $ZZ
			OPCODE(0x98, tya(R));				// TYA
			OPCODE(0x99, sta_absy(R));			// STA $hhll,Y
			OPCODE(0x9A, txs(R));				// TXS
			OPCODE(0x9C, stz_abs(R));			// STZ $hhll
			OPCODE(0x9D, sta_absx(R));			// STA $hhll,X
			OPCODE(0x9E, stz_absx(R));			// STZ $hhll,X
			OPCODE(0x9F, bbs(R, 1));				// BBS1 $ZZ,$rr

			OPCODE(0xA0, ldy_imm(R));			// LDY #$nn
			OPCODE(0xA1, lda_zpindx(R));			// LDA (IND,X)
			OPCODE(0xA2, ldx_imm(R));			// LDX #$nn
			OPCODE(0xA3, tstins_zpx(R));			// TST #$nn,$ZZ,X
			OPCODE(0xA4, ldy_zp(R));				// LDY $ZZ
			OPCODE(0xA5, lda_zp(R));				// LDA $ZZ
			OPCODE(0xA6, ldx_zp(R));				// LDX $ZZ
			OPCODE(0xA7, smb(R, 2));				// SMB2 $ZZ
			OPCODE(0xA8, tay(R));				// TAY
			OPCODE(0xA9, lda_imm(R));			// LDA #$nn
			OPCODE(0xAA, tax(R));				// TAX
			OPCODE(0xAC, ldy_abs(R));			// LDY $hhll
			OPCODE(0xAD, lda_abs(R));			// LDA $hhll
			OPCODE(0xAE, ldx_abs(R));			// LDX $hhll
			OPCODE(0xAF, bbs(R, 2));				// BBS2 $ZZ,$rr

			OPCODE(0xB0, bcs(R));				// BCS $rr
			OPCODE(0xB1, lda_zpindy(R));			// LDA (IND),Y
			OPCODE(0xB2, lda_zpind(R));			// LDA  (IND)
			OPCODE(0xB3, tstins_absx(R))			// TST #$nn,$hhll,X
			OPCODE(0xB4, ldy_zpx(R));			// LDY $ZZ,X
			OPCODE(0xB5, lda_zpx(R));			// LDA $ZZ,X
			OPCODE(0xB6, ldx_zpy(R));			// LDX $ZZ,Y
			OPCODE(0xB7, smb(R, 3));				// SMB3 $ZZ
			OPCODE(0xB8, clv(R));				// CLV
			OPCODE(0xB9, lda_absy(R));			// LDA $hhll,Y
			OPCODE(0xBA, tsx(R));				// TSX
			OPCODE(0xBC, ldy_absx(R));			// LDY $hhll,X
			OPCODE(0xBD, lda_absx(R));			// LDA $hhll,X
			OPCODE(0xBE, ldx_absy(R));			// LDX $hhll,Y
			OPCODE(0xBF, bbs(R, 3));				// BBS3 $ZZ,$rr

			OPCODE(0xC0, cpy_imm(R));			// CPY #$nn
			OPCODE(0xC1, cmp_zpindx(R));			// CMP (IND,X)
			OPCODE(0xC2, cly(R));				// CLY
			OPCODE(0xC3, tdd(R));				// TDD $SHSL,$DHDL,$LHLL
			OPCODE(0xC4, cpy_zp(R));				// CPY $ZZ
			OPCODE(0xC5, cmp_zp(R));				// CMP $ZZ
			OPCODE(0xC6, dec_zp(R));				// DEC $ZZ
			OPCODE(0xC7, smb(R, 4));				// SMB4 $ZZ
			OPCODE(0xC8, iny(R));				// INY
			OPCODE(0xC9, cmp_imm(R));			// CMP #$nn
			OPCODE(0xCA, dex(R));				// DEX
			OPCODE(0xCC, cpy_abs(R));			// CPY $hhll
			OPCODE(0xCD, cmp_abs(R));			// CMP $hhll
			OPCODE(0xCE, dec_abs(R));			// DEC $hhll
			OPCODE(0xCF, bbs(R, 4));				// BBS4 $ZZ,$rr

			OPCODE(0xD0, bne(R));				// BNE $rr
			OPCODE(0xD1, cmp_zpindy(R));			// CMP (IND),Y
			OPCODE(0xD2, cmp_zpind(R));			// CMP (IND)
			OPCODE(0xD3, tin(R));				// TIN $SHSL,$DHDL,$LHLL
			OPCODE(0xD4, csh(R));				// CSH
			OPCODE(0xD5, cmp_zpx(R));			// CMP $ZZ,X
			OPCODE(0xD6, dec_zpx(R));			// DEC $ZZ,X
			OPCODE(0xD7, smb(R, 5));				// SMB5 $ZZ
			OPCODE(0xD8, cld(R));				// CLD
			OPCODE(0xD9, cmp_absy(R));			// CMP $hhll,Y
			OPCODE(0xDA, phx(R));				// PHX
			OPCODE(0xDD, cmp_absx(R));			// CMP $hhll,X
			OPCODE(0xDE, dec_absx(R));			// DEC $hhll,X
			OPCODE(0xDF, bbs(R, 5));				// BBS5 $ZZ,$rr

			OPCODE(0xE0, cpx_imm(R));			// CPX #$nn
			OPCODE(0xE1, sbc_zpindx(R));			// SBC (IND,X)
			OPCODE(0xE3, tia(R));				// TIA $SHSL,$DHDL,$LHLL
			OPCODE(0xE4, cpx_zp(R));				// CPX $ZZ
			OPCODE(0xE5, sbc_zp(R));				// SBC $ZZ
			OPCODE(0xE6, inc_zp(R));				// INC $ZZ
			OPCODE(0xE7, smb(R, 6));				// SMB6 $ZZ
			OPCODE(0xE8, inx(R));				// INX
			OPCODE(0xE9, sbc_imm(R));			// SBC #$nn
			OPCODE(0xEA, nop(R));				// NOP
			OPCODE(0xEC, cpx_abs(R));			// CPX $hhll
			OPCODE(0xED, sbc_abs(R));			// SBC $hhll
			OPCODE(0xEE, inc_abs(R));			// INC $hhll
			OPCODE(0xEF, bbs(R, 6));				// BBS6 $ZZ,$rr
			OPCODE(0xF0, beq(R));				// BEQ $rr

			OPCODE(0xF1, sbc_zpindy(R));			// SBC (IND),Y
			OPCODE(0xF2, sbc_zpind(R));			// SBC (IND)
			OPCODE(0xF3, tai(R));				// TAI $SHSL,$DHDL,$LHLL
			OPCODE(0xF4, set(R));				// SET
			OPCODE(0xF5, sbc_zpx(R));			// SBC $ZZ,X
			OPCODE(0xF6, inc_zpx(R));			// INC $ZZ,X
			OPCODE(0xF7, smb(R, 7));				// SMB7 $ZZ
			OPCODE(0xF8, sed(R));				// SED
			OPCODE(0xF9, sbc_absy(R));			// SBC $hhll,Y
			OPCODE(0xFA, plx(R));				// PLX
			OPCODE(0xFD, sbc_absx(R));			// SBC $hhll,X
			OPCODE(0xFE, inc_absx(R));			// INC $hhll,X
			OPCODE(0xFF, bbs(R, 7));				// BBS7 $ZZ,$rr

			default:
				// Illegal opcodes are treated as NOP
				MESSAGE_DEBUG("Illegal opcode 0x%02X at pc=0x%04X!\n", opcode, CPU.PC);
				nop(R);
		}
	}
	if (pend) {
		pend = 0;
		irq = GCPU.irq_lines & ~GCPU.irq_mask & INT_MASK;
		if ((CPU.P & FL_I) == 0 && irq) {
			interrupt(R, irq);
		}
		limit = max_cycles;
		goto again;
	}

	regs_store(R);
}
