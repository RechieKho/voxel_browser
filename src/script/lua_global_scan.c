/* Walks the bytecode of a compiled Lua chunk and reports every access to a global
 * (GETTABUP/SETTABUP on the _ENV upvalue) with its line number. Used by
 * `voxel_browser_server --check-pack` (architecture_spec/dev-experience.md §3.4.1)
 * to tell, per file, which engine globals it touches.
 *
 * Written in C because it needs Lua's internal headers (Proto, opcodes); everything
 * else in the engine sees only the callback interface in global_scan.hpp. Lua 5.4. */
#include <string.h>

#include "lauxlib.h"
#include "ldebug.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lua.h"

#if LUA_VERSION_NUM != 504
#error "lua_global_scan.c is written against the Lua 5.4 bytecode"
#endif

typedef void (*vb_global_cb)(void *ud, const char *name, int line, int is_write);

static void scan_proto(const Proto *p, vb_global_cb cb, void *ud) {
	int pc;
	int i;
	for (pc = 0; pc < p->sizecode; ++pc) {
		const Instruction ins = p->code[pc];
		const OpCode op = GET_OPCODE(ins);
		int up;
		int key;
		int is_write;
		if (op == OP_GETTABUP) {
			up = GETARG_B(ins);
			key = GETARG_C(ins);
			is_write = 0;
		} else if (op == OP_SETTABUP) {
			up = GETARG_A(ins);
			key = GETARG_B(ins);
			is_write = 1;
		} else {
			continue;
		}
		if (up < 0 || up >= p->sizeupvalues) {
			continue;
		}
		{
			const TString *upname = p->upvalues[up].name;
			const TValue *k;
			if (upname == NULL || strcmp(getstr(upname), "_ENV") != 0) {
				continue;
			}
			if (key < 0 || key >= p->sizek) {
				continue;
			}
			k = &p->k[key];
			if (!ttisstring(k)) {
				continue;
			}
			cb(ud, getstr(tsvalue(k)), luaG_getfuncline(p, pc), is_write);
		}
	}
	for (i = 0; i < p->sizep; ++i) {
		scan_proto(p->p[i], cb, ud);
	}
}

/* The compiled chunk (a Lua function) must be on top of the stack. */
void vb_scan_chunk_globals(lua_State *L, vb_global_cb cb, void *ud) {
	const LClosure *cl = (const LClosure *)lua_topointer(L, -1);
	if (cl != NULL) {
		scan_proto(cl->p, cb, ud);
	}
}
