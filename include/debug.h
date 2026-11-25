/*
 *  Copyright (C) 2002-2021  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <cstdint>
#include <string>
#include <vector>

void DEBUG_SetupConsole(void);
void DEBUG_DrawScreen(void);
bool DEBUG_Breakpoint(void);
bool DEBUG_IntBreakpoint(uint8_t intNum);
void DEBUG_Enable(bool pressed);
void DEBUG_CheckExecuteBreakpoint(uint16_t seg, uint32_t off);
bool DEBUG_ExitLoop(void);
void DEBUG_RefreshPage(char scroll);
Bitu DEBUG_EnableDebugger(void);

#if C_DEBUG
bool DEBUG_AgentStep(bool over, bool* continued);
bool DEBUG_AgentResumeAfterTerminate(void);
bool DEBUG_AgentCanStartTarget(void);
uint64_t DEBUG_AgentEntryBreakpointSequence(void);
bool DEBUG_AgentCreateExecutionBreakpoint(uint16_t seg, uint32_t off, bool once, uintptr_t* handle);
bool DEBUG_AgentCreateMemoryBreakpoint(uint16_t seg, uint32_t off, bool protected_mode, bool linear, uintptr_t* handle);
bool DEBUG_AgentDeleteBreakpoint(uintptr_t handle);
uintptr_t DEBUG_AgentConsumeLastBreakpoint(void);
void DEBUG_AgentClearLastBreakpoint(void);
bool DEBUG_AgentBeginOutputCapture(void);
std::string DEBUG_AgentEndOutputCapture(void);
#if C_HEAVY_DEBUG
struct DEBUG_AgentTraceEvent {
    uint16_t cs = 0;
    uint32_t instruction_pointer = 0;
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    uint32_t esi = 0;
    uint32_t edi = 0;
    uint32_t ebp = 0;
    uint32_t esp = 0;
    uint16_t ds = 0;
    uint16_t es = 0;
    uint16_t fs = 0;
    uint16_t gs = 0;
    uint16_t ss = 0;
    uint32_t flags = 0;
    std::string instruction;
    std::string analysis;
};
bool DEBUG_AgentStartTrace(uint32_t instruction_count);
bool DEBUG_AgentStopTrace(uint32_t* event_count);
bool DEBUG_AgentTraceIsActive(void);
void DEBUG_AgentCopyTraceEvents(std::vector<DEBUG_AgentTraceEvent>* events);
#endif
#endif

// Exposed for GDB server
uint32_t DEBUG_GetRegister(int reg);
// Whether a GDB register write can be applied in the current CPU mode
bool DEBUG_CanSetRegister(int reg, uint32_t value);
void DEBUG_SetRegister(int reg, uint32_t value);
uint8_t DEBUG_ReadMemory(uint32_t address);
void DEBUG_WriteMemory(uint32_t address, uint8_t value);
#if C_REMOTEDEBUG
void DEBUG_StartGDBServer(int port);
void DEBUG_StopGDBServer();
void DEBUG_StartQMPServer(int port);
void DEBUG_StopQMPServer();
// Called by the main loop to handle GDB requests; true while GDB holds the CPU
// stopped. *decoder_ret is a decoder result for the main loop to handle.
bool DEBUG_CheckGDBStep(Bits* decoder_ret);

// GDB breakpoints and watchpoints by Z packet type, at linear addresses:
// 1 = done, 0 = unsupported in this build, -1 = error
int DEBUG_GDBInsertBreakpoint(int type, uint32_t address, uint32_t length);
int DEBUG_GDBRemoveBreakpoint(int type, uint32_t address, uint32_t length);

bool DEBUG_IsGDBClientConnected();
bool DEBUG_IsGDBStopped();  // A GDB client holds the CPU stopped

// GDB-aware program execution (for QMP debug-execute)
void DEBUG_SetGDBBreakOnExec(bool enable); // Set flag to break at entry for GDB
#endif

extern Bitu cycle_count;
extern Bitu debugCallback;

#ifdef C_HEAVY_DEBUG
bool DEBUG_HeavyIsBreakpoint(void);
void DEBUG_HeavyWriteLogInstruction(void);
#endif
