/*
 *  Copyright (C) 2008-2009  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <iomanip>

#include "ComponentFactory.h"
#include "GXemul.h"
#include "components/MIPS_CPUComponent.h"
#include "mips_cpu_types.h"
#include "opcodes_mips.h"

static const char* hi6_names[] = HI6_NAMES;
static const char* regnames[] = MIPS_REGISTER_NAMES;
static const char* special_names[] = SPECIAL_NAMES;
static const char* special_rot_names[] = SPECIAL_ROT_NAMES;
static const char* regimm_names[] = REGIMM_NAMES;
static mips_cpu_type_def cpu_type_defs[] = MIPS_CPU_TYPE_DEFS;


MIPS_CPUComponent::MIPS_CPUComponent()
	: CPUComponent("mips_cpu", "MIPS")
	, m_mips_type("5KE")	// defaults to a MIPS64 rev 2 cpu
{
	m_frequency = 100e6;

	// Find (and cache) the cpu type in m_type:
	memset((void*) &m_type, 0, sizeof(m_type));
	for (size_t j=0; cpu_type_defs[j].name != NULL; j++) {
		if (m_mips_type == cpu_type_defs[j].name) {
			m_type = cpu_type_defs[j];
			break;
		}
	}

	if (m_type.name == NULL) {
		std::cerr << "Internal error: Unimplemented MIPS type?\n";
		throw std::exception();
	}

	ResetState();

	AddVariable("model", &m_mips_type);

	AddVariable("hi", &m_hi);
	AddVariable("lo", &m_lo);

	for (size_t i=0; i<N_MIPS_GPRS; i++)
		AddVariable(regnames[i], &m_gpr[i]);
}


refcount_ptr<Component> MIPS_CPUComponent::Create(const ComponentCreateArgs& args)
{
	// Defaults:
	ComponentCreationSettings settings;
	settings["model"] = "5KE";

	if (!ComponentFactory::GetCreationArgOverrides(settings, args))
		return NULL;

	refcount_ptr<Component> cpu = new MIPS_CPUComponent();
	if (!cpu->SetVariableValue("model", "\"" + settings["model"] + "\""))
		return NULL;

	return cpu;
}


void MIPS_CPUComponent::ResetState()
{
	// Most MIPS CPUs use 4 KB native page size.
	// TODO: A few use 1 KB pages; this should be supported as well.
	m_pageSize = 4096;

	m_hi = 0;
	m_lo = 0;

	for (size_t i=0; i<N_MIPS_GPRS; i++)
		m_gpr[i] = 0;

	// MIPS CPUs are hardwired to start at 0xffffffffbfc00000:
	m_pc = MIPS_INITIAL_PC;

	// Reasonable initial stack pointer.
	m_gpr[MIPS_GPR_SP] = MIPS_INITIAL_STACK_POINTER;

	CPUComponent::ResetState();
}


bool MIPS_CPUComponent::PreRunCheckForComponent(GXemul* gxemul)
{
	if (m_gpr[MIPS_GPR_ZERO] != 0) {
		gxemul->GetUI()->ShowDebugMessage(this, "the zero register (zr) "
		    "must contain the value 0.\n");
		return false;
	}

	return CPUComponent::PreRunCheckForComponent(gxemul);
}


bool MIPS_CPUComponent::Is32Bit() const
{
	return m_type.isa_level == 32 || m_type.isa_level <= 2;
}


static uint64_t Trunc3264(uint64_t x, bool is32bit)
{
	return is32bit? (uint32_t)x : x;
}


static uint64_t TruncSigned3264(uint64_t x, bool is32bit)
{
	return is32bit? (int32_t)x : x;
}


void MIPS_CPUComponent::ShowRegisters(GXemul* gxemul, const vector<string>& arguments) const
{
	bool is32bit = Is32Bit();
	stringstream ss;

	ss.flags(std::ios::hex);
	ss << std::setfill('0');

	// Yuck, this is horrible. Is there some portable way to put e.g.
	// std::setw(16) into an object, and just pass that same object several
	// times?

	ss << "pc=";
	if (is32bit)
		ss << std::setw(8);
	else
		ss << std::setw(16);
	ss << Trunc3264(m_pc, is32bit);
	string symbol = GetSymbolRegistry().LookupAddress(TruncSigned3264(m_pc, is32bit), true);
	if (symbol != "")
		ss << " <" << symbol << ">";
	ss << "\n";

	ss << "hi=";
	if (is32bit)
		ss << std::setw(8);
	else
		ss << std::setw(16);
	ss << Trunc3264(m_hi, is32bit) << " lo=";
	if (is32bit)
		ss << std::setw(8);
	else
		ss << std::setw(16);
	ss << Trunc3264(m_lo, is32bit) << "\n";

	for (size_t i=0; i<N_MIPS_GPRS; i++) {
		ss << regnames[i] << "=";
		if (is32bit)
			ss << std::setw(8);
		else
			ss << std::setw(16);
		ss << Trunc3264(m_gpr[i], is32bit);
		if ((i&3) == 3)
			ss << "\n";
		else
			ss << " ";
	}

	gxemul->GetUI()->ShowDebugMessage(ss.str());
}


int MIPS_CPUComponent::Execute(GXemul* gxemul, int nrOfCycles)
{
	return DyntransExecute(gxemul, nrOfCycles);
}


int MIPS_CPUComponent::GetDyntransICshift() const
{
	bool mips16 = m_pc & 1? true : false;

	// Normal encoding: 4 bytes per instruction, i.e. shift is 2 bits.
	// MIPS16 encoding: 2 bytes per instruction, i.e. shift is 1 bit.
	return mips16? 1 : 2;
}


void (*MIPS_CPUComponent::GetDyntransToBeTranslated())(CPUComponent*, DyntransIC*) const
{
	return instr_ToBeTranslated;
}


bool MIPS_CPUComponent::VirtualToPhysical(uint64_t vaddr, uint64_t& paddr,
	bool& writable)
{
	if (Is32Bit())
		vaddr = (int32_t)vaddr;

	// TODO. For now, just return the lowest 29 bits.
	if (vaddr >= 0xffffffff80000000 && vaddr < 0xffffffffc0000000) {
		paddr = vaddr & 0x1fffffff;
		writable = true;
		return true;
	}

	// TODO  ... or the lowest 44.
	if (vaddr >= 0xa800000000000000 && vaddr < 0xa8000fffffffffff) {
		paddr = vaddr & 0xfffffffffff;
		writable = true;
		return true;
	}

	return false;
}


size_t MIPS_CPUComponent::DisassembleInstructionMIPS16(uint64_t vaddr,
	unsigned char *instruction, vector<string>& result)
{
	// Read the instruction word:
	uint16_t iword = *((uint16_t *) instruction);
	if (m_isBigEndian)
		iword = BE16_TO_HOST(iword);
	else
		iword = LE16_TO_HOST(iword);

	// ... and add it to the result:
	char tmp[5];
	snprintf(tmp, sizeof(tmp), "%04x", iword);
	result.push_back(tmp);

	// TODO
	result.push_back("unimplemented MIPS16 instruction");

	return sizeof(uint16_t);
}


size_t MIPS_CPUComponent::DisassembleInstruction(uint64_t vaddr, size_t maxLen,
	unsigned char *instruction, vector<string>& result)
{
	bool mips16 = vaddr & 1? true : false;
	size_t instrSize = mips16? sizeof(uint16_t) : sizeof(uint32_t);

	if (maxLen < instrSize) {
		assert(false);
		return 0;
	}

	if (mips16)
		return DisassembleInstructionMIPS16(vaddr,
		    instruction, result);

	// Read the instruction word:
	uint32_t iword = *((uint32_t *) instruction);
	if (m_isBigEndian)
		iword = BE32_TO_HOST(iword);
	else
		iword = LE32_TO_HOST(iword);

	// ... and add it to the result:
	char tmp[9];
	snprintf(tmp, sizeof(tmp), "%08x", (int) iword);
	result.push_back(tmp);

	int hi6 = iword >> 26;
	int rs = (iword >> 21) & 31;
	int rt = (iword >> 16) & 31;
	int rd = (iword >> 11) & 31;
	int sa = (iword >>  6) & 31;

	switch (hi6) {

	case HI6_SPECIAL:
		{
			int special6 = iword & 0x3f;
			int sub = rs;
			stringstream ss;

			switch (special6) {

			case SPECIAL_SLL:
			case SPECIAL_SRL:
			case SPECIAL_SRA:
			case SPECIAL_DSLL:
			case SPECIAL_DSRL:
			case SPECIAL_DSRA:
			case SPECIAL_DSLL32:
			case SPECIAL_DSRL32:
			case SPECIAL_DSRA32:
				if (rd == 0 && special6 == SPECIAL_SLL) {
					if (sa == 0)
						ss << "nop";
					else if (sa == 1)
						ss << "ssnop";
					else if (sa == 3)
						ss << "ehb";
					else
						ss << "nop (weird, sa="
						    << sa << ")";

					result.push_back(ss.str());
					break;
				}

				switch (sub) {
				case 0x00:
					result.push_back(
					    special_names[special6]);
					ss << regnames[rd] << "," <<
					    regnames[rt] << "," << sa;
					result.push_back(ss.str());
					break;
				case 0x01:
					result.push_back(
					    special_rot_names[special6]);
					ss << regnames[rd] << "," <<
					    regnames[rt] << "," << sa;
					result.push_back(ss.str());
					break;
				default:ss << "unimplemented special, sub="
					    << sub;
					result.push_back(ss.str());
				}
				break;

			case SPECIAL_DSRLV:
			case SPECIAL_DSRAV:
			case SPECIAL_DSLLV:
			case SPECIAL_SLLV:
			case SPECIAL_SRAV:
			case SPECIAL_SRLV:
				sub = sa;

				switch (sub) {
				case 0x00:
					result.push_back(
					    special_names[special6]);
					ss << regnames[rd] << "," <<
					    regnames[rt] << "," << regnames[rs];
					result.push_back(ss.str());
					break;
				case 0x01:
					result.push_back(
					    special_rot_names[special6]);
					ss << regnames[rd] << "," <<
					    regnames[rt] << "," << regnames[rs];
					result.push_back(ss.str());
					break;
				default:ss << "unimplemented special, sub="
					    << sub;
					result.push_back(ss.str());
				}
				break;

			case SPECIAL_JR:
				/*  .hb = hazard barrier hint on MIPS32/64
				    rev 2  */
				if ((iword >> 10) & 1)
					result.push_back("jr.hb");
				else
					result.push_back("jr");
				ss << regnames[rs];
				result.push_back(ss.str());
				break;
				
			case SPECIAL_JALR:
				/*  .hb = hazard barrier hint on
				     MIPS32/64 rev 2  */
				if ((iword >> 10) & 1)
					result.push_back("jalr.hb");
				else
					result.push_back("jalr");
				ss << regnames[rd] << "," << regnames[rs];
				result.push_back(ss.str());
				break;

			case SPECIAL_MFHI:
			case SPECIAL_MFLO:
				result.push_back(special_names[special6]);
				result.push_back(regnames[rd]);
				break;

			case SPECIAL_MTLO:
			case SPECIAL_MTHI:
				result.push_back(special_names[special6]);
				result.push_back(regnames[rs]);
				break;

			case SPECIAL_ADD:
			case SPECIAL_ADDU:
			case SPECIAL_SUB:
			case SPECIAL_SUBU:
			case SPECIAL_AND:
			case SPECIAL_OR:
			case SPECIAL_XOR:
			case SPECIAL_NOR:
			case SPECIAL_SLT:
			case SPECIAL_SLTU:
			case SPECIAL_DADD:
			case SPECIAL_DADDU:
			case SPECIAL_DSUB:
			case SPECIAL_DSUBU:
			case SPECIAL_MOVZ:
			case SPECIAL_MOVN:
				result.push_back(special_names[special6]);
				ss << regnames[rd] << "," <<
				    regnames[rs] << "," << regnames[rt];
				result.push_back(ss.str());
				break;

			case SPECIAL_MULT:
			case SPECIAL_MULTU:
			case SPECIAL_DMULT:
			case SPECIAL_DMULTU:
			case SPECIAL_DIV:
			case SPECIAL_DIVU:
			case SPECIAL_DDIV:
			case SPECIAL_DDIVU:
			case SPECIAL_TGE:
			case SPECIAL_TGEU:
			case SPECIAL_TLT:
			case SPECIAL_TLTU:
			case SPECIAL_TEQ:
			case SPECIAL_TNE:
				result.push_back(special_names[special6]);
				if (rd != 0) {
					if (m_type.rev == MIPS_R5900) {
						if (special6 == SPECIAL_MULT ||
						    special6 == SPECIAL_MULTU)
							ss << regnames[rd]<<",";
						else
							ss << "WEIRD_R5900_RD,";
					} else {
						ss << "WEIRD_R5900_RD,";
					}
				}

				ss << regnames[rs] << "," << regnames[rt];
				result.push_back(ss.str());
				break;

			case SPECIAL_SYNC:
				result.push_back(special_names[special6]);
				ss << ((iword >> 6) & 31);
				result.push_back(ss.str());
				break;

			case SPECIAL_SYSCALL:
			case SPECIAL_BREAK:
				result.push_back(special_names[special6]);
				if (((iword >> 6) & 0xfffff) != 0) {
					ss << ((iword >> 6) & 0xfffff);
					result.push_back(ss.str());
				}
				break;

			case SPECIAL_MFSA:
				if (m_type.rev == MIPS_R5900) {
					result.push_back("mfsa");
					result.push_back(regnames[rd]);
				} else {
					result.push_back(
					    "unimplemented special 0x28");
				}
				break;

			case SPECIAL_MTSA:
				if (m_type.rev == MIPS_R5900) {
					result.push_back("mtsa");
					result.push_back(regnames[rs]);
				} else {
					result.push_back(
					    "unimplemented special 0x29");
				}
				break;

			default:
				ss << "unimplemented instruction: " <<
				    special_names[special6];
				result.push_back(ss.str());
				break;
			}
		}
		break;

	case HI6_BEQ:
	case HI6_BEQL:
	case HI6_BNE:
	case HI6_BNEL:
	case HI6_BGTZ:
	case HI6_BGTZL:
	case HI6_BLEZ:
	case HI6_BLEZL:
		{
			int imm = (int16_t) iword;
			uint64_t addr = vaddr + 4 + (imm << 2);

			stringstream ss;

			if (hi6 == HI6_BEQ && rt == MIPS_GPR_ZERO &&
			    rs == MIPS_GPR_ZERO) {
				result.push_back("b");
			} else {
				result.push_back(hi6_names[hi6]);

				switch (hi6) {
				case HI6_BEQ:
				case HI6_BEQL:
				case HI6_BNE:
				case HI6_BNEL:
					ss << regnames[rt] << ",";
				}

				ss << regnames[rs] << ",";
			}

			ss.flags(std::ios::hex | std::ios::showbase);
			ss << addr;
			result.push_back(ss.str());

			string symbol = GetSymbolRegistry().LookupAddress(addr, true);
			if (symbol != "")
				result.push_back("; <" + symbol + ">");
		}
		break;

	case HI6_ADDI:
	case HI6_ADDIU:
	case HI6_DADDI:
	case HI6_DADDIU:
	case HI6_SLTI:
	case HI6_SLTIU:
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
		{
			result.push_back(hi6_names[hi6]);

			stringstream ss;
			ss << regnames[rt] << "," << regnames[rs] << ",";
			if (hi6 == HI6_ANDI || hi6 == HI6_ORI ||
			    hi6 == HI6_XORI) {
				ss.flags(std::ios::hex | std::ios::showbase);
				ss << (uint16_t) iword;
			} else {
				ss << (int16_t) iword;
			}
			result.push_back(ss.str());
		}
		break;

	case HI6_LUI:
		{
			result.push_back(hi6_names[hi6]);

			stringstream ss;
			ss << regnames[rt] << ",";
			ss.flags(std::ios::hex | std::ios::showbase);
			ss << (uint16_t) iword;
			result.push_back(ss.str());
		}
		break;

	case HI6_LB:
	case HI6_LBU:
	case HI6_LH:
	case HI6_LHU:
	case HI6_LW:
	case HI6_LWU:
	case HI6_LD:
	case HI6_LQ_MDMX:
	case HI6_LWC1:
	case HI6_LWC2:
	case HI6_LWC3:
	case HI6_LDC1:
	case HI6_LDC2:
	case HI6_LL:
	case HI6_LLD:
	case HI6_SB:
	case HI6_SH:
	case HI6_SW:
	case HI6_SD:
	case HI6_SQ_SPECIAL3:
	case HI6_SC:
	case HI6_SCD:
	case HI6_SWC1:
	case HI6_SWC2:
	case HI6_SWC3:
	case HI6_SDC1:
	case HI6_SDC2:
	case HI6_LWL:
	case HI6_LWR:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_SWL:
	case HI6_SWR:
	case HI6_SDL:
	case HI6_SDR:
		{
			if (hi6 == HI6_LQ_MDMX && m_type.rev != MIPS_R5900) {
				result.push_back("mdmx (UNIMPLEMENTED)");
				break;
			}
			if (hi6 == HI6_SQ_SPECIAL3 && m_type.rev!=MIPS_R5900) {
				result.push_back("special3 (UNIMPLEMENTED)");
				break;
			}


			int imm = (int16_t) iword;
			stringstream ss;

			/*  LWC3 is PREF in the newer ISA levels:  */
			/*  TODO: Which ISAs? IV? V? 32? 64?  */
			if (m_type.isa_level >= 4 && hi6 == HI6_LWC3) {
				result.push_back("pref");

				ss << rt << "," << imm <<
				    "(" << regnames[rs] << ")";
				result.push_back(ss.str());
				break;
			}

			result.push_back(hi6_names[hi6]);

			if (hi6 == HI6_SWC1 || hi6 == HI6_SWC2 ||
			    hi6 == HI6_SWC3 ||
			    hi6 == HI6_SDC1 || hi6 == HI6_SDC2 ||
			    hi6 == HI6_LWC1 || hi6 == HI6_LWC2 ||
			    hi6 == HI6_LWC3 ||
			    hi6 == HI6_LDC1 || hi6 == HI6_LDC2)
				ss << "r" << rt;
			else
				ss << regnames[rt];

			ss << "," << imm << "(" << regnames[rs] << ")";

			result.push_back(ss.str());

			// TODO: Symbol lookup, if running.
		}
		break;

	case HI6_J:
	case HI6_JAL:
		{
			result.push_back(hi6_names[hi6]);

			int imm = (iword & 0x03ffffff) << 2;
			uint64_t addr = (vaddr + 4) & ~((1 << 28) - 1);
			addr |= imm;

			stringstream ss;
			ss.flags(std::ios::hex | std::ios::showbase);
			ss << addr;
			result.push_back(ss.str());

			string symbol = GetSymbolRegistry().LookupAddress(addr, true);
			if (symbol != "")
				result.push_back("; <" + symbol + ">");
		}
		break;

	// CopX here. TODO
	// Cache
	// Special2

	case HI6_REGIMM:
		{
			int regimm5 = (iword >> 16) & 0x1f;
			int imm = (int16_t) iword;
			uint64_t addr = (vaddr + 4) + (imm << 2);

			stringstream ss;
			ss.flags(std::ios::hex | std::ios::showbase);

			switch (regimm5) {

			case REGIMM_BLTZ:
			case REGIMM_BGEZ:
			case REGIMM_BLTZL:
			case REGIMM_BGEZL:
			case REGIMM_BLTZAL:
			case REGIMM_BLTZALL:
			case REGIMM_BGEZAL:
			case REGIMM_BGEZALL:
				result.push_back(regimm_names[regimm5]);

				ss << regnames[rs] << "," << addr;
				result.push_back(ss.str());
				break;

			case REGIMM_SYNCI:
				result.push_back(regimm_names[regimm5]);

				ss << imm << "(" << regnames[rs] << ")";
				result.push_back(ss.str());
				break;

			default:
				{
					ss << "unimplemented instruction: " <<
					    regimm_names[regimm5];
					result.push_back(ss.str());
				}
			}
		}
		break;

	default:
		{
			stringstream ss;
			ss << "unimplemented instruction: " << hi6_names[hi6];
			result.push_back(ss.str());
		}
		break;
	}

	return instrSize;
}


string MIPS_CPUComponent::GetAttribute(const string& attributeName)
{
	// if (attributeName == "stable")
	//	return "yes";

	if (attributeName == "description")
		return "MIPS processor.";

	return Component::GetAttribute(attributeName);
}


/*****************************************************************************/


void MIPS_CPUComponent::Translate(uint32_t iword, struct DyntransIC* ic)
{
	UI* ui = GetUI();	// for debug messages
	int requiredISA = 1;		// 1, 2, 3, 4, 32, or 64
	int requiredISArevision = 1;	// 1 or 2 (for MIPS32/64)

	int hi6 = iword >> 26;
	int rs = (iword >> 21) & 31;
	int rt = (iword >> 16) & 31;
//	int rd = (iword >> 11) & 31;
//	int sa = (iword >>  6) & 31;
	int32_t imm = (int16_t)iword;
//	s6 = iword & 63;
//	s10 = (rs << 5) | sa;

	switch (hi6) {

//	case HI6_ADDI:
	case HI6_ADDIU:
//	case HI6_SLTI:
//	case HI6_SLTIU:
//	case HI6_DADDI:
	case HI6_DADDIU:
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
		ic->arg[0] = (size_t)&m_gpr[rt];
		ic->arg[1] = (size_t)&m_gpr[rs];
		if (hi6 == HI6_ADDI || hi6 == HI6_ADDIU ||
		    hi6 == HI6_SLTI || hi6 == HI6_SLTIU ||
		    hi6 == HI6_DADDI || hi6 == HI6_DADDIU)
			ic->arg[2] = (int16_t)iword;
		else
			ic->arg[2] = (uint16_t)iword;

		switch (hi6) {
//		case HI6_ADDI:    ic->f = instr(addi); break;
		case HI6_ADDIU:   ic->f = instr_add_u64_u64_imms32_truncS32; break;
//		case HI6_SLTI:    ic->f = instr(slti); break;
//		case HI6_SLTIU:   ic->f = instr(sltiu); break;
//		case HI6_DADDI:   ic->f = instr(daddi); requiredISA = 3; break;
		case HI6_DADDIU:  ic->f = instr_add_u64_u64_imms32; requiredISA = 3; break;
		case HI6_ANDI:    ic->f = instr_and_u64_u64_immu32; break;
		case HI6_ORI:     ic->f = instr_or_u64_u64_immu32; break;
		case HI6_XORI:    ic->f = instr_xor_u64_u64_immu32; break;
		}

		if (rt == MIPS_GPR_ZERO)
			ic->f = instr_nop;
		break;

	case HI6_LUI:
		ic->f = instr_set_u64_imms32;
		ic->arg[0] = (size_t)&m_gpr[rt];
		ic->arg[1] = (int32_t) (imm << 16);

		if (rt == MIPS_GPR_ZERO)
			ic->f = instr_nop;
		break;

	default:
		if (ui != NULL) {
			stringstream ss;
			ss.flags(std::ios::hex);
			ss << "unimplemented opcode 0x" << hi6;
			ui->ShowDebugMessage(this, ss.str());
		}
	}

	// Attempting a MIPS32 instruction on e.g. a MIPS IV CPU?
	if (requiredISA > m_type.isa_level) {
		// TODO: Cause MIPS "unimplemented instruction" exception instead.
		ic->f = NULL;

		// TODO: Only print the warning once; actual real-world code may
		// rely on this mechanism to detect cpu type, or similar.
		if (ui != NULL) {
			stringstream ss;
			ss.flags(std::ios::hex);
			ss << "instruction at 0x" << m_pc << " requires ISA level ";
			ss.flags(std::ios::dec);
			ss << requiredISA << "; this cpu supports only ISA level " <<
			    m_type.isa_level << "\n";
			ui->ShowDebugMessage(this, ss.str());
		}
	}

	// Attempting a MIPS III or IV instruction on e.g. a MIPS32 CPU?
	if ((requiredISA == 3 || requiredISA == 4) && Is32Bit()) {
		// TODO: Cause MIPS "unimplemented instruction" exception instead.
		ic->f = NULL;

		// TODO: Only print the warning once; actual real-world code may
		// rely on this mechanism to detect cpu type, or similar.
		if (ui != NULL) {
			stringstream ss;
			ss.flags(std::ios::hex);
			ss << "instruction at 0x" << m_pc << " is a 64-bit instruction,"
			    " which cannot be executed on this CPU\n";
			ui->ShowDebugMessage(this, ss.str());
		}
	}

	// Attempting a revision 2 opcode on a revision1 MIPS32/64 CPU?
	if (requiredISArevision > 1 && m_type.isa_revision) {
		// TODO: Cause MIPS "unimplemented instruction" exception instead.
		ic->f = NULL;

		// TODO: Only print the warning once; actual real-world code may
		// rely on this mechanism to detect cpu type, or similar.
		if (ui != NULL) {
			stringstream ss;
			ss.flags(std::ios::hex);
			ss << "instruction at 0x" << m_pc << " is a MIPS32/64 revision ";
			ss << requiredISArevision << " instruction; this cpu supports"
			    " only revision " << m_type.isa_revision << "\n";
			ui->ShowDebugMessage(this, ss.str());
		}
	}
}


DYNTRANS_INSTR(MIPS_CPUComponent,ToBeTranslated)
{
	DYNTRANS_INSTR_HEAD(MIPS_CPUComponent)

	cpu->DyntransToBeTranslatedBegin(ic);

	uint32_t iword;
	if (cpu->DyntransReadInstruction(iword))
		cpu->Translate(iword, ic);

	cpu->DyntransToBeTranslatedDone(ic);
}


/*****************************************************************************/


#ifdef WITHUNITTESTS

#include "ComponentFactory.h"

static void Test_MIPS_CPUComponent_IsStable()
{
	UnitTest::Assert("the MIPS_CPUComponent should not be stable yet",
	    !ComponentFactory::HasAttribute("mips_cpu", "stable"));
}

static void Test_MIPS_CPUComponent_Create()
{
	refcount_ptr<Component> cpu =
	    ComponentFactory::CreateComponent("mips_cpu");
	UnitTest::Assert("component was not created?", !cpu.IsNULL());

	const StateVariable * p = cpu->GetVariable("pc");
	UnitTest::Assert("cpu has no pc state variable?", p != NULL);
	UnitTest::Assert("initial pc", p->ToString(), "0xffffffffbfc00000");
}

static void Test_MIPS_CPUComponent_IsCPU()
{
	refcount_ptr<Component> mips_cpu =
	    ComponentFactory::CreateComponent("mips_cpu");
	CPUComponent* cpu = mips_cpu->AsCPUComponent();
	UnitTest::Assert("mips_cpu is not a CPUComponent?", cpu != NULL);
}

static void Test_MIPS_CPUComponent_DefaultModel()
{
	refcount_ptr<Component> cpu =
	    ComponentFactory::CreateComponent("mips_cpu");

	// 5KE is a good default model (MIPS64 rev 2 ISA)
	UnitTest::Assert("wrong default model",
	    cpu->GetVariable("model")->ToString(), "5KE");
}

static void Test_MIPS_CPUComponent_Disassembly_Basic()
{
	refcount_ptr<Component> mips_cpu =
	    ComponentFactory::CreateComponent("mips_cpu");
	CPUComponent* cpu = mips_cpu->AsCPUComponent();

	vector<string> result;
	size_t len;
	unsigned char instruction[sizeof(uint32_t)];
	// This assumes that the default endianness is BigEndian...
	instruction[0] = 0x27;
	instruction[1] = 0xbd;
	instruction[2] = 0xff;
	instruction[3] = 0xd8;

	len = cpu->DisassembleInstruction(0x12345678, sizeof(uint32_t),
	    instruction, result);

	UnitTest::Assert("disassembled instruction was wrong length?", len, 4);
	UnitTest::Assert("disassembly result incomplete?", result.size(), 3);
	UnitTest::Assert("disassembly result[0]", result[0], "27bdffd8");
	UnitTest::Assert("disassembly result[1]", result[1], "addiu");
	UnitTest::Assert("disassembly result[2]", result[2], "sp,sp,-40");
}

UNITTESTS(MIPS_CPUComponent)
{
	UNITTEST(Test_MIPS_CPUComponent_IsStable);
	UNITTEST(Test_MIPS_CPUComponent_Create);
	UNITTEST(Test_MIPS_CPUComponent_IsCPU);
	UNITTEST(Test_MIPS_CPUComponent_DefaultModel);

	// Disassembly:
	UNITTEST(Test_MIPS_CPUComponent_Disassembly_Basic);
}

#endif

