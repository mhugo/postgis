/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.refractions.net
 *
 * Referenced object functions
 *
 * Copyright 2012-2013 Oslandia <infos@oslandia.com>
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of the GNU General Public Licence. See the COPYING file.
 *
 **********************************************************************/

#include "../postgis_config.h"
#include "lwgeom_ref.h"
#include "lwgeom_sfcgal.h"
#include "lwgeom_log.h"
#include "lwgeom_pg.h"

static GSERIALIZED* sfcgal_serialize( void *ptr );

struct ref_type_definition
{
    const char* name;
    GSERIALIZED* (*serialize_fn)   ( void *ptr );
    void*        (*deserialize_fn) ( GSERIALIZED *ptr );
    void         (*delete_fn)      ( void *ptr );
};

static GSERIALIZED* sfcgal_serialize( void *ptr )
{
    GSERIALIZED *ret;
    sfcgal_prepared_geometry_t* pgeom = (sfcgal_prepared_geometry_t*)ptr;
    ret = SFCGALPreparedGeometry2POSTGIS( pgeom, /* force3D?? */0 );
    return ret;
}

struct ref_type_definition ref_types[NUM_REF_TYPES] = {
    {
	.name = "sfcgal",
	.serialize_fn = sfcgal_serialize,
	.deserialize_fn = POSTGIS2SFCGALPreparedGeometry,
        .delete_fn = sfcgal_prepared_geometry_delete
    },
    {
	.name = "lwgeom",
	.serialize_fn = geometry_serialize,
	.deserialize_fn = lwgeom_from_gserialized,
        .delete_fn = lwgeom_free
    }
};

GSERIALIZED* serialize_ref_object( void *pgeom, bool nested, int type )
{
    GSERIALIZED* ret;
    ref_object_t* ref;

    lwnotice( "**** serialize_ref_object nested=%d", nested ? 1:0);
    if ( ! nested ) {
	/* serialize */
        lwnotice("**** serialize from '%s'", ref_types[type].name);
	LWDEBUGF( 4, "[SERIALIZE] serialize to GSERIALIZED from '%s'", ref_types[type].name );
	ret = (*ref_types[type].serialize_fn)( pgeom );
        (*ref_types[type].delete_fn)( pgeom );
    }
    else {
        lwnotice("**** serializing by passing pointer of type %s", ref_types[type].name );
	LWDEBUGF( 4, "[SERIALIZE] no need to serialize, pass pointer of type '%s'", ref_types[type].name );
	ref = (ref_object_t*)lwalloc( sizeof(ref_object_t) );
	SET_VARSIZE( ref, sizeof(ref_object_t) );
        ref->flags = 0;
        FLAGS_SET_ISPOINTER( ref->flags, 1 );
	ref->ref_ptr = pgeom;
	ref->ref_type = type;
        ret = (GSERIALIZED*)ref;
    }

    return ret;
}

/*
 * ginput: Datum from PG_GETARG_DATUM(i)
 */
void* unserialize_ref_object( Datum ginput, int requested_type )
{
    void *ret;
    ref_object_t *rgeom;
    int ref_type;

    lwnotice( "**** unserialize_ref_object");
    rgeom = (ref_object_t*)PG_DETOAST_DATUM( ginput );

    if ( FLAGS_GET_ISPOINTER( rgeom->flags ) ) {
        lwnotice("**** is pointer");
        ref_type = rgeom->ref_type;
	if ( requested_type == -1 ) {
            lwnotice("**** forcing serialization from %s", ref_types[ref_type].name);
	    LWDEBUG( 4, "[REF] forcing serialization");
            ret = (*ref_types[ref_type].serialize_fn) (rgeom->ref_ptr);
	}
	else if ( rgeom->ref_type != requested_type ) {
            lwnotice("**** conversion %s to %s", ref_types[ref_type].name, ref_types[requested_type].name);
            GSERIALIZED *sobj;
	    LWDEBUGF( 4, "[REF] type conversion from '%s' to '%s'", ref_types[ref_type].name, ref_types[requested_type].name);
	    /* serialize the current pointer */
	    sobj = (*ref_types[ref_type].serialize_fn) ( rgeom->ref_ptr );
	    /* unserialize to the requested type */
	    ret = (*ref_types[requested_type].deserialize_fn) ( sobj );
	}
	else {
            lwnotice("**** direct pointer of type %s", ref_types[ref_type].name);
	    LWDEBUGF( 4, "[REF] deserialize a pointer of type %s", ref_types[ref_type].name );
	    ret = rgeom->ref_ptr;
            lwfree(rgeom);
	}
    }
    else {
        lwnotice("**** not pointer");
	if ( requested_type == -1 ) {
            lwnotice("**** nop");
	    ret = rgeom;
	}
	else {
            lwnotice("**** deserialize to %s", ref_types[requested_type].name);
	    LWDEBUGF( 4, "[REF] deserialize to pointer of type %s", ref_types[requested_type].name );
	    ret = (*ref_types[requested_type].deserialize_fn) ((GSERIALIZED*)rgeom);
	    /* PG_FREE_IF_COPY equivalent */
	    if ( ginput != rgeom ) {
		pfree( rgeom );
	    }
	}
    }

    return ret;
}
