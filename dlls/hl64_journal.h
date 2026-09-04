// hl64 diagnostic journal: server-side state/event JSONL recorder for human
// reference runs (hl64 repo, docs/HUMAN-REFERENCE.md).  Local instrumentation
// only: compiled when HL64_JOURNAL is defined (dlls/wscript adds it for the
// waf build; the hl64 oracle's CMake build never defines it and never
// compiles hl64_journal.cpp).  Even when compiled in, nothing is written
// unless HL64_JOURNAL_PATH is set in the environment, so stock diagnostic
// sessions stay byte-identical in behaviour.
//
// Include after cbase.h; the signatures need CBaseEntity and USE_TYPE.
#pragma once
#ifdef HL64_JOURNAL
void HL64_JournalFrame( void );
void HL64_JournalFireTargets( const char *targetName, CBaseEntity *pActivator,
			      CBaseEntity *pCaller, USE_TYPE useType, float value );
void HL64_JournalPlayerUse( CBaseEntity *pObject, float value );
#endif
