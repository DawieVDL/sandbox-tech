/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// wad.c

#include "quakedef.h"

int				wad_numlumps;
lumpinfo_t		*wad_lumps;
byte			*wad_base = NULL;

void SwapPic (qpic_t *pic);

/*
==================
W_CleanupName

Lowercases name and pads with spaces and a terminating 0 to the length of
lumpinfo_t->name.
Used so lumpname lookups can proceed rapidly by comparing 4 chars at a time
Space padding is so names can be printed nicely in tables.
Can safely be performed in place.
==================
*/
void W_CleanupName (const char *in, char *out)
{
	int		i;
	int		c;

	for (i=0 ; i<16 ; i++ )
	{
		c = in[i];
		if (!c)
			break;

		if (c >= 'A' && c <= 'Z')
			c += ('a' - 'A');
		out[i] = c;
	}

	for ( ; i< 16 ; i++ )
		out[i] = 0;
}

/*
====================
W_LoadWadFile
====================
*/
void W_LoadWadFile (void) //johnfitz -- filename is now hard-coded for honesty
{
	lumpinfo_t		*lump_p;
	wadinfo_t		*header;
	int			i;
	int			infotableofs;
	const char		*filename = WADFILENAME;

	//johnfitz -- modified to use malloc
	//TODO: use cache_alloc
	if (wad_base)
		free (wad_base);
	wad_base = COM_LoadMallocFile (filename, NULL);
	if (!wad_base)
		Sys_Error ("W_LoadWadFile: couldn't load %s\n\n"
			   "Basedir is: %s\n\n"
			   "Check that this has a " GAMENAME " subdirectory containing pak0.pak and pak1.pak, "
			   "or use the -basedir command-line option to specify another directory.",
			   filename, com_basedir);

	header = (wadinfo_t *)wad_base;

	if (header->identification[0] != 'W' || header->identification[1] != 'A'
	 || header->identification[2] != 'D' || header->identification[3] != '2')
		Sys_Error ("Wad file %s doesn't have WAD2 id\n",filename);

	wad_numlumps = LittleLong(header->numlumps);
	infotableofs = LittleLong(header->infotableofs);
	wad_lumps = (lumpinfo_t *)(wad_base + infotableofs);

	for (i=0, lump_p = wad_lumps ; i<wad_numlumps ; i++,lump_p++)
	{
		lump_p->filepos = LittleLong(lump_p->filepos);
		lump_p->size = LittleLong(lump_p->size);
		W_CleanupName (lump_p->name, lump_p->name);	// CAUTION: in-place editing!!!
		if (lump_p->type == TYP_QPIC)
			SwapPic ( (qpic_t *)(wad_base + lump_p->filepos));
	}
}


/*
=============
W_GetLumpinfo
=============
*/
static lumpinfo_t *W_GetLumpinfo (lumpinfo_t *lumps, int numlumps, const char *name)
{
	int		i;
	lumpinfo_t	*lump_p;
	char	clean[16];

	W_CleanupName (name, clean);

	for (lump_p=lumps, i=0 ; i<numlumps ; i++,lump_p++)
	{
		if (!strcmp(clean, lump_p->name))
			return lump_p;
	}

	Con_SafePrintf ("W_GetLumpinfo: %s not found\n", name); //johnfitz -- was Sys_Error
	return NULL;
}

void *W_GetLumpName (const char *name)
{
	lumpinfo_t	*lump;

	lump = W_GetLumpinfo (wad_lumps, wad_numlumps, name);

	if (!lump) return NULL; //johnfitz

	return (void *)(wad_base + lump->filepos);
}

void *W_GetLumpNum (int num)
{
	lumpinfo_t	*lump;

	if (num < 0 || num > wad_numlumps)
		Sys_Error ("W_GetLumpNum: bad number: %i", num);

	lump = wad_lumps + num;

	return (void *)(wad_base + lump->filepos);
}

/*
=================
W_OpenWadFile
=================
*/
static qboolean W_OpenWadFile (const char *filename, fshandle_t *fh)
{
	FILE *f;
	long  length;

	length = (long)COM_FOpenFile (filename, &f, NULL);
	if (length == -1)
		return false;

	fh->file = f;
	fh->start = ftell (f);
	fh->pos = 0;
	fh->length = length;
	fh->pak = file_from_pak;
	return true;
}

/*
=================
W_AddWadFile
=================
*/
static wad_t *W_AddWadFile (const char *name, fshandle_t *fh)
{
	int			i, id, numlumps, infotableofs, disksize;
	wadinfo_t	header;
	lumpinfo_t *lumps, *info;
	wad_t	   *wad;

	FS_fread ((void *)&header, 1, sizeof (header), fh);

	id = LittleLong (header.identification[0] | (header.identification[1] << 8)
			| (header.identification[2] << 16) | (header.identification[3] << 24));
	if (id != WADID && id != WADID_VALVE)
	{
		Con_Warning ("%s is not a valid WAD\n", name);
		return NULL;
	}

	numlumps = LittleLong (header.numlumps);
	infotableofs = LittleLong (header.infotableofs);

	if (numlumps < 0 || infotableofs < 0)
	{
		Con_Warning ("%s is not a valid WAD (%i lumps, %i info table offset)\n", name, numlumps, infotableofs);
		return NULL;
	}
	if (!numlumps)
	{
		Con_DPrintf2 ("WAD file %s has no lumps, ignored\n", name);
		return NULL;
	}

	lumps = (lumpinfo_t *)malloc (numlumps * sizeof (lumpinfo_t));

	FS_fseek (fh, infotableofs, SEEK_SET);
	FS_fread (lumps, 1, numlumps * sizeof (lumpinfo_t), fh);

	// parse the directory
	for (i = 0, info = lumps; i < numlumps; i++, info++)
	{
		W_CleanupName (info->name, info->name);
		info->filepos = LittleLong (info->filepos);
		info->size = LittleLong (info->size);
		disksize = LittleLong (info->disksize);

		if (info->filepos + info->size > fh->length && !(info->filepos + disksize > fh->length))
			info->size = disksize;

		// ensure lump sanity
		if (info->filepos < 0 || info->size < 0 || info->filepos + info->size > fh->length)
		{
			if (info->filepos > fh->length || info->size < 0)
			{
				Con_Warning ("WAD file %s lump \"%.16s\" begins %li bytes beyond end of WAD\n", name, info->name, info->filepos - fh->length);

				info->filepos = 0;
				info->size = q_max (0, info->size - info->filepos);
			}
			else
			{
				Con_Warning (
					"WAD file %s lump \"%.16s\" extends %li bytes beyond end of WAD (lump size is %i)\n", name, info->name,
					(info->filepos + info->size) - fh->length, info->size);

				info->size = q_max (0, info->size - info->filepos);
			}
		}
	}

	wad = (wad_t *)malloc (sizeof (wad_t));
	q_strlcpy (wad->name, name, sizeof (wad->name));
	wad->id = id;
	wad->fh = *fh;
	wad->numlumps = numlumps;
	wad->lumps = lumps;

	Con_DPrintf ("%s\n", name);
	return wad;
}

/*
=================
W_LoadWadList
=================
*/
wad_t *W_LoadWadList (const char *names)
{
	char	  *newnames = q_strdup (names);
	char	  *name, *e;
	wad_t	  *wad, *wads = NULL;
	char	   filename[MAX_QPATH];
	fshandle_t fh;

	for (name = newnames; name && *name;)
	{
		e = strchr (name, ';');
		if (e)
			*e++ = 0;

		// remove all of the leading garbage left by the map editor
		COM_FileBase (name, filename, sizeof (filename));
		COM_AddExtension (filename, ".wad", sizeof (filename));

		if (!W_OpenWadFile (filename, &fh))
		{
			// try the "gfx" directory
			memmove (filename + 4, filename, sizeof (filename) - 4);
			memcpy (filename, "gfx/", 4);
			filename[sizeof (filename) - 1] = 0;

			if (!W_OpenWadFile (filename, &fh))
			{
				name = e;
				continue;
			}
		}

		wad = W_AddWadFile (filename, &fh);
		if (wad)
		{
			wad->next = wads;
			wads = wad;
		}
		else
			FS_fclose (&fh);

		name = e;
	}
	free (newnames);

	return wads;
}

/*
=================
W_FreeWadList
=================
*/
void W_FreeWadList (wad_t *wads)
{
	wad_t *next;

	while (wads)
	{
		FS_fclose (&wads->fh);
		free (wads->lumps);

		next = wads->next;
		free (wads);
		wads = next;
	}
}

/*
=================
W_GetLumpinfoList
=================
*/
lumpinfo_t *W_GetLumpinfoList (wad_t *wads, const char *name, wad_t **out_wad)
{
	lumpinfo_t *info;

	while (wads)
	{
		info = W_GetLumpinfo (wads->lumps, wads->numlumps, name);
		if (info)
		{
			*out_wad = wads;
			return info;
		}

		wads = wads->next;
	}

	return NULL;
}

/*
=============================================================================

automatic byte swapping

=============================================================================
*/

void SwapPic (qpic_t *pic)
{
	pic->width = LittleLong(pic->width);
	pic->height = LittleLong(pic->height);
}
