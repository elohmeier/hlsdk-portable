// hl64 diagnostic journal implementation.  See hl64_journal.h for the guard
// and activation contract.  Records server-side *state*, not input: per-frame
// player kinematics/stance plus FireTargets and player +use events, as JSONL
// whose "event" vocabulary matches hl64's native hlsim trace (t, event,
// target, ...), so the existing aligners can consume it.
#ifdef HL64_JOURNAL

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *g_journal;
static bool g_journal_failed;
static char g_journal_map[64];
static CBaseEntity *g_last_use_object;	// pointer identity only, never dereferenced
static float g_last_use_value;
static float g_last_use_time;

static FILE *JournalHandle( void )
{
	if( g_journal || g_journal_failed )
		return g_journal;
	const char *path = getenv( "HL64_JOURNAL_PATH" );
	if( !path || !path[0] )
	{
		g_journal_failed = true;
		return NULL;
	}
	g_journal = fopen( path, "a" );
	if( !g_journal )
	{
		ALERT( at_console, "HL64 journal: cannot open %s\n", path );
		g_journal_failed = true;
		return NULL;
	}
	fprintf( g_journal,
		"{\"t\":%.6f,\"event\":\"session_start\",\"journal_version\":1}\n",
		gpGlobals->time );
	fflush( g_journal );
	return g_journal;
}

// Journal strings are map-authored tokens; escape the JSON specials anyway so
// one odd targetname cannot corrupt the stream.
static void JournalString( FILE *f, const char *s )
{
	fputc( '"', f );
	for( ; s && *s; s++ )
	{
		unsigned char c = (unsigned char)*s;
		if( c == '"' || c == '\\' )
			fprintf( f, "\\%c", c );
		else if( c < 0x20 )
			fprintf( f, "\\u%04x", c );
		else
			fputc( c, f );
	}
	fputc( '"', f );
}

static void JournalEntityFields( FILE *f, const char *prefix, CBaseEntity *ent )
{
	if( !ent )
		return;
	fprintf( f, ",\"%s\":", prefix );
	JournalString( f, STRING( ent->pev->classname ) );
	if( !FStringNull( ent->pev->targetname ) )
	{
		fprintf( f, ",\"%s_name\":", prefix );
		JournalString( f, STRING( ent->pev->targetname ) );
	}
}

// Events can fire during map spawn, before the first StartFrame of the new
// map, so every emitter checks the boundary itself.
static void JournalMapBoundary( FILE *f )
{
	const char *map = STRING( gpGlobals->mapname );
	if( !strncmp( g_journal_map, map, sizeof( g_journal_map ) - 1 ))
		return;
	fprintf( f, "{\"t\":%.6f,\"event\":\"map_start\",\"map\":", gpGlobals->time );
	JournalString( f, map );
	if( g_journal_map[0] )
	{
		fputs( ",\"previous_map\":", f );
		JournalString( f, g_journal_map );
	}
	fputs( "}\n", f );
	strncpy( g_journal_map, map, sizeof( g_journal_map ) - 1 );
	g_journal_map[sizeof( g_journal_map ) - 1] = '\0';
	g_last_use_object = NULL;	// edicts do not survive a map change
	fflush( f );
}

void HL64_JournalFrame( void )
{
	FILE *f = JournalHandle();
	if( !f )
		return;
	JournalMapBoundary( f );

	CBaseEntity *ent = UTIL_PlayerByIndex( 1 );
	if( !ent || !ent->IsPlayer() )
		return;
	CBasePlayer *player = (CBasePlayer *)ent;
	entvars_t *pev = player->pev;

	fprintf( f,
		"{\"t\":%.6f,\"event\":\"frame\","
		"\"origin\":[%.3f,%.3f,%.3f],"
		"\"velocity\":[%.3f,%.3f,%.3f],"
		"\"angles\":[%.3f,%.3f,%.3f],"
		"\"buttons\":%d,\"pressed\":%d,"
		"\"health\":%.1f,\"armor\":%.1f,"
		"\"on_ground\":%s,\"ducked\":%s,\"on_train\":%s,"
		"\"waterlevel\":%d,\"movetype\":%d,\"dead\":%d,"
		"\"weapons\":%d",
		gpGlobals->time,
		pev->origin.x, pev->origin.y, pev->origin.z,
		pev->velocity.x, pev->velocity.y, pev->velocity.z,
		pev->v_angle.x, pev->v_angle.y, pev->v_angle.z,
		pev->button, player->m_afButtonPressed,
		pev->health, pev->armorvalue,
		FBitSet( pev->flags, FL_ONGROUND ) ? "true" : "false",
		FBitSet( pev->flags, FL_DUCKING ) ? "true" : "false",
		( player->m_afPhysicsFlags & PFLAG_ONTRAIN ) ? "true" : "false",
		pev->waterlevel, pev->movetype, pev->deadflag,
		pev->weapons );
	if( player->m_pActiveItem )
	{
		fputs( ",\"weapon\":", f );
		JournalString( f, STRING( player->m_pActiveItem->pev->classname ));
	}
	if( !FNullEnt( pev->groundentity ))
	{
		CBaseEntity *ground = CBaseEntity::Instance( pev->groundentity );
		JournalEntityFields( f, "ground", ground );
	}
	fputs( "}\n", f );
	fflush( f );
}

void HL64_JournalFireTargets( const char *targetName, CBaseEntity *pActivator,
			      CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	FILE *f = JournalHandle();
	if( !f || !targetName )
		return;
	JournalMapBoundary( f );
	fprintf( f, "{\"t\":%.6f,\"event\":\"fire\",\"target\":", gpGlobals->time );
	JournalString( f, targetName );
	fprintf( f, ",\"use_type\":%d,\"value\":%.3f", (int)useType, value );
	JournalEntityFields( f, "activator", pActivator );
	JournalEntityFields( f, "caller", pCaller );
	fputs( "}\n", f );
	fflush( f );
}

void HL64_JournalPlayerUse( CBaseEntity *pObject, float value )
{
	FILE *f = JournalHandle();
	if( !f || !pObject )
		return;
	// Continuous +use dispatches every frame; keep entity/value changes plus
	// a 0.5 s heartbeat instead of every dispatch.
	if( pObject == g_last_use_object && value == g_last_use_value
	    && gpGlobals->time - g_last_use_time < 0.5f )
		return;
	g_last_use_object = pObject;
	g_last_use_value = value;
	g_last_use_time = gpGlobals->time;
	JournalMapBoundary( f );
	fprintf( f, "{\"t\":%.6f,\"event\":\"use\",\"value\":%.1f",
		gpGlobals->time, value );
	JournalEntityFields( f, "target", pObject );
	fputs( "}\n", f );
	fflush( f );
}

#endif	// HL64_JOURNAL
