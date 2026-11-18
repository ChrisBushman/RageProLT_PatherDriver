/******************************************************************************
 * ATI 3D RAGE SDK sample code                                                *
 *                                                                            *
 * parseasc.cpp - This module contains routines to parse 3DStudio .ASC files. *
 *                                                                            *
 * Copyright (c) 1995-1996 ATI Technologies Inc.  All rights reserved.        *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include "types.h"
#include "geom.h"
#include "glob.h"
#include "parseasc.h"

static int line_num = 0;
static FILE* fp = NULL;

#define MAX_TOKEN_LENGTH    100
#define TRUE                1
#define FALSE               0

#define ERROR               0
#define SUCCESS             1
#define ELEMS_DONE          2

#define NORMAL              0
#define COMMENT             1

#define ASC_DELIM           " \t\n:"

#define ASC_TKN_ERR         0
#define ASC_TKN_VERTICES    1
#define ASC_TKN_FACES       2
#define ASC_TKN_VERTEX      3
#define ASC_TKN_FACE        4
#define ASC_TKN_X           5
#define ASC_TKN_Y           6
#define ASC_TKN_Z           7
#define ASC_TKN_A           8
#define ASC_TKN_B           9
#define ASC_TKN_C           10

#define READ_NONE           0
#define READ_VERTEX         1
#define READ_FACET          2

typedef struct {
    char asc_token[32];
    int  asc_code;
} ASC_TOKEN;

ASC_TOKEN asc_token_table[] = {
    {"Vertices", ASC_TKN_VERTICES},
    {"Faces", ASC_TKN_FACES},
    {"Vertex", ASC_TKN_VERTEX},
    {"Face", ASC_TKN_FACE},
    {"X", ASC_TKN_X},
    {"Y", ASC_TKN_Y},
    {"Z", ASC_TKN_Z},
    {"A", ASC_TKN_A},
    {"B", ASC_TKN_B},
    {"C", ASC_TKN_C}};

extern HWND ghWnd;


/******************************************************************************
 * GetAscTokenCode                                                            *
 *  Function: tokenizes a string                                              *
 *    Inputs: token - string to match                                         *
 *   Outputs: the resulting token code if matched, otherwise ASC_TKN_ERR      *
 ******************************************************************************/

int GetAscTokenCode (char *token)
{
    int i;

    for (i = 0; i < (sizeof (asc_token_table) / sizeof (asc_token_table[0])); i++)
    {
        if (strcmp (asc_token_table[i].asc_token, token) == 0) return asc_token_table[i].asc_code;
    } // for

    return ASC_TKN_ERR;
} // GetAscTokenCode


int dbgfilecount = 0;


/******************************************************************************
 * LoadAscObject                                                              *
 *  Function: loads an object defined in a .ASC file and stores it in an      *
 *            object descriptor                                               *
 *    Inputs: lpObject - pointer to object descriptor to store object         *
 *            filename - name of file to load                                 *
 *   Outputs: TRUE - object loaded successfully                               *
 *            FALSE - unable to load object                                   *
 ******************************************************************************/

BOOL LoadAscObject (POBJECT lpObject, char *filename)
{
    int     stat;
    char    token[MAX_TOKEN_LENGTH];
    BOOL    bExitWhile = FALSE;
    int     mode = READ_NONE;
    int     asccode;
    int textouttimes = 0;

    if (!lpObject)
    {
        sprintf (gszErrMsg, "LoadAscObject: invalid pointer");
        return FALSE;
    } // if

    if (!OpenAscFile (filename))
    {
        return FALSE;
    } // if

    // Initialize object bounds struct.

    lpObject->bounds.xmin = FLT_MAX;
    lpObject->bounds.xmax = FLT_MIN;
    lpObject->bounds.ymin = FLT_MAX;
    lpObject->bounds.ymax = FLT_MIN;
    lpObject->bounds.zmin = FLT_MAX;
    lpObject->bounds.zmax = FLT_MIN;
    lpObject->bounds.amin = FLT_MAX;
    lpObject->bounds.amax = FLT_MIN;

    while (!bExitWhile)
    {
        stat = GetAscToken (token, ASC_DELIM);

        if (stat == EOF)
        {
            // Error.

            stat = ERROR;
            CloseAscFile ();
            bExitWhile = TRUE;
            wsprintf (gszErrMsg, "LoadAscObject: EOF error");
        }
        else if (stat == ELEMS_DONE)
        {
            // Exit.

            stat = SUCCESS;
            CloseAscFile ();
            bExitWhile = TRUE;
        } // if

        // Get token code.

        asccode = GetAscTokenCode (token);

        // Process token code.

        switch (asccode)
        {
            case ASC_TKN_VERTICES:

                // Get number of vertices.

                stat = GetAscToken (token, ASC_DELIM);
                if ((stat == EOF) || (stat == ELEMS_DONE))
                {
                    // Error.

                    sprintf (gszErrMsg, "LoadAscObject: error reading number of vertices: %s",
                             token);
                    return FALSE;
                } // if

                // Get vertices number.

                if ((sscanf (token, "%lu", &(lpObject->num_verts))) != 1)
                {
                    // Error.

                    sprintf (gszErrMsg, "LoadAscObject: error scanning number of vertices: %s",
                             token);
                    return FALSE;
                } // if

                // Allocate memory for vertex buffer and primitive list.

                lpObject->vertices = (PVERTEX) malloc (lpObject->num_verts * sizeof (VERTEX));
                //lpObject->vertices = (PVERTEX) HeapAlloc (gHeapVertex, HEAP_ZERO_MEMORY ,lpObject->num_verts * sizeof (VERTEX));
                if (!lpObject->vertices)
                {
                    // Error.

                    sprintf (gszErrMsg, "LoadAscObject: error allocing memory for vertices: %s",
                             token);
                    return FALSE;
                } // if

                break;

            case ASC_TKN_FACES:

                // Get number of facets, allocate facet array buffer.

                stat = GetAscToken (token, ASC_DELIM);
                if ((stat == EOF) || (stat == ELEMS_DONE))
                {
                    // Error.

                    sprintf (gszErrMsg, "LoadAscObject: error getting token for facets: %s",
                             token);
                    return FALSE;
                } // if

                // Get vertices number.

                if ((sscanf (token, "%lu", &(lpObject->num_facets))) != 1)
                {
                    // Error.

                    sprintf (gszErrMsg, "LoadAscObject: error reading number of facets: %s",
                             token);
                    return FALSE;
                } // if

                lpObject->facets = (PFACET) malloc (lpObject->num_facets * sizeof (FACET));
                //lpObject->facets = (PFACET) HeapAlloc (gHeapFacet, HEAP_ZERO_MEMORY, lpObject->num_facets * sizeof (FACET));
                if (!lpObject->facets)
                {
                    // Error.

                    sprintf (gszErrMsg, "LoadAscObject: error allocating memory for facets: %s",
                             token);
                    return FALSE;
                } // if

                lpObject->facetlist = (PFACET*) malloc (lpObject->num_facets * sizeof (PFACET));
                //lpObject->facets = (PFACET) HeapAlloc (gHeapFacet, HEAP_ZERO_MEMORY, lpObject->num_facets * sizeof (FACET));
                if (!lpObject->facetlist)
                {
                    // Error.

                    sprintf (gszErrMsg, "LoadAscObject: error allocating memory for facetlist: %s",
                             token);
                    return FALSE;
                } // if

                // Allocate memory for the primitive list.

                lpObject->vlstPrimList = (C3D_VLIST) malloc (lpObject->num_facets * sizeof (C3D_PVTCF) * 3);
                //lpObject->vlstPrimList = (C3D_VLIST) HeapAlloc (gHeapVList, HEAP_ZERO_MEMORY, lpObject->num_facets * sizeof (C3D_PVTCF) * 3);
                if (!lpObject->vlstPrimList)
                {
                    sprintf (gszErrMsg, "LoadAscObject: error allocating memory for primitive list: %s",
                             token);
                    return FALSE;
                } // if

                // Initialize the number of vertices in the current primitve
                // list to be rendered.

                lpObject->nNumPrimListVerts = 0;

                break;

            case ASC_TKN_VERTEX:

                // Parse stream for vertex x, y, z model-coordinates.

                if (!ReadVertex (lpObject))
                {
                    // Error.

                    sprintf (gszErrMsg, "ReadVertex error");
                    return FALSE;
                } // if
                break;

            case ASC_TKN_FACE:

                // Parse stream for facet vertex indices.

                if (!ReadFacet (lpObject))
                {
                    // Error.

                    sprintf (gszErrMsg, "ReadFacet error");
                    return FALSE;
                } // if
                break;
        } // switch
    } // while

    if (stat == ERROR)
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    } // if
} // LoadAscObject


/******************************************************************************
 * ReadVertex                                                                 *
 *  Function: reads in vertex data from .ASC file                             *
 *    Inputs: lpObject - pointer to object descriptor to store vertex data    *
 *   Outputs: TRUE - vertex read successfully                                 *
 *            FALSE - unable to read vertex data                              *
 ******************************************************************************/

BOOL ReadVertex (POBJECT lpObject)
{
    int stat, vindex;
    char token[MAX_TOKEN_LENGTH];

    if (!lpObject || !lpObject->vertices)
    {
        sprintf (gszErrMsg, "ReadVertex: invalid pointer");
        return FALSE;
    } // if

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: EOF or ELEMS_DONE before read vindex");
        return FALSE;
    } // if

    // The first Vertex token should be for the string "Vertex list".
    // If so, return.

    if (strcmp (token, "list") == 0) return TRUE;

    if ((sscanf (token, "%d", &vindex)) != 1)
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: error reading vindex: %s", token);
        return FALSE;
    } // if

    if (vindex >= (int) lpObject->num_verts)
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: vindex too large = %d", vindex);
        return FALSE;
    } // if

    // Get x.

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: EOF or ELEMS_DONE before read X token");
        return FALSE;
    } // if

    // Ensure x.

    if (_stricmp (token, "X") != 0)
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: token not X");
        return FALSE;
    } // if

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: EOF or ELEMS_DONE before read x value");
        return FALSE;
    } // if
    if ((sscanf (token, "%f", &(lpObject->vertices[vindex].model_coords[0]))) != 1)
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: error sscanf for x");
        return FALSE;
    } // if

    // Update x bounds.

    if (lpObject->vertices[vindex].model_coords[0] <= lpObject->bounds.xmin)
    {
        lpObject->bounds.xmin = lpObject->vertices[vindex].model_coords[0];
    } // if
    if (lpObject->vertices[vindex].model_coords[0] >= lpObject->bounds.xmax)
    {
        lpObject->bounds.xmax = lpObject->vertices[vindex].model_coords[0];
    } // if

    // Get y.

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: EOF or ELEMS_DONE before read Y token");
        return FALSE;
    } // if

    // Ensure y.

    if (_stricmp (token, "Y") != 0)
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: token not Y");
        return FALSE;
    } // if

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: EOF or ELEMS_DONE before read y value");
        return FALSE;
    } // if
    if ((sscanf (token, "%f", &(lpObject->vertices[vindex].model_coords[1]))) != 1)
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: error sscanf for y");
        return FALSE;
    } // if

    // Update y bounds.

    if (lpObject->vertices[vindex].model_coords[1] <= lpObject->bounds.ymin)
    {
        lpObject->bounds.ymin = lpObject->vertices[vindex].model_coords[1];
    } // if
    if (lpObject->vertices[vindex].model_coords[1] >= lpObject->bounds.ymax)
    {
        lpObject->bounds.ymax = lpObject->vertices[vindex].model_coords[1];
    } // if

    // Get z.

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: EOF or ELEMS_DONE before read Z token");
        return FALSE;
    } // if

    // Ensure z.

    if (_stricmp (token, "Z") != 0)
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: token not Z");
        return FALSE;
    } // if

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: EOF or ELEMS_DONE before read z value");
        return FALSE;
    } // if
    if ((sscanf (token, "%f", &(lpObject->vertices[vindex].model_coords[2]))) != 1)
    {
        // Error.

        sprintf (gszErrMsg, "ReadVertex: error sscanf for z");
        return FALSE;
    } // if

    // Update z bounds.
    if (lpObject->vertices[vindex].model_coords[2] <= lpObject->bounds.zmin)
    {
        lpObject->bounds.zmin = lpObject->vertices[vindex].model_coords[2];
    } // if
    if (lpObject->vertices[vindex].model_coords[2] >= lpObject->bounds.zmax)
    {
        lpObject->bounds.zmax = lpObject->vertices[vindex].model_coords[2];
    } // if

    // Initialize vertex flags.

    lpObject->vertices[vindex].model_coords[3] = 1.0f;
    lpObject->vertices[vindex].field_mask = 5;

    return TRUE;
} // ReadVertex


/******************************************************************************
 * ReadFacet                                                                  *
 *  Function: reads in facet data from .ASC file                              *
 *    Inputs: lpObject - pointer to object descriptor to store facet data     *
 *   Outputs: TRUE - facet read successfully                                  *
 *            FALSE - unable to read facet data                               *
 ******************************************************************************/

BOOL ReadFacet (POBJECT lpObject)
{
    int stat, findex, vindex;
    char token[MAX_TOKEN_LENGTH];

    if (!lpObject || !lpObject->vertices || !lpObject->facets)
    {
        sprintf (gszErrMsg, "ReadFacet: invalid OBJECT pointers");
        return FALSE;
    } // if

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: error getting findex token");
        return FALSE;
    } // if

    // The first Face token should be for the string "Face list".
    // If so, return.

    if (strcmp (token, "list") == 0) return TRUE;

    if ((sscanf (token, "%d", &findex)) != 1)
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: error reading findex");
        return FALSE;
    } // if

    // Get A.

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: error getting A token");
        return FALSE;
    } // if

    // Ensure A.

    if (_stricmp (token, "A") != 0)
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: Not A token");
        return FALSE;
    } // if

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: error getting A index token");
        return FALSE;
    } // if

    if ((sscanf (token, "%d", &vindex)) != 1)
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: error reading A index");
        return FALSE;
    } // if

    // Validate vertex and copy to facet vertex list.

    if (vindex >= (int) lpObject->num_verts)
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: A index too large");
        return FALSE;
    } // if

    lpObject->facets[findex].vertices[0] = vindex;

    // Increment vertex's count of facets which use it.

    lpObject->vertices[vindex].num_facets++;
    lpObject->vertices[vindex].facets[lpObject->vertices[vindex].num_facets - 1] = findex;

    // Get B.
    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: error getting B token");
        return FALSE;
    } // if

    // Ensure B.

    if (_stricmp (token, "B") != 0)
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: Not B token");
        return FALSE;
    } // if

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: error getting B index token");
        return FALSE;
    } // if

    if ((sscanf (token, "%d", &vindex)) != 1)
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: error reading B index");
        return FALSE;
    } // if

    // Validate vertex and copy to facet vertex list.

    if (vindex >= (int) lpObject->num_verts)
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: B index too large");
        return FALSE;
    } // if

    lpObject->facets[findex].vertices[1] = vindex;

    // Increment vertex's count of facets which use it.

    lpObject->vertices[vindex].num_facets++;
    lpObject->vertices[vindex].facets[lpObject->vertices[vindex].num_facets - 1] = findex;

    // Get C.

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: error getting C token");
        return FALSE;
    } // if

    // Ensure C.

    if (_stricmp (token, "C") != 0)
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: Not C token");
        return FALSE;
    } // if

    stat = GetAscToken (token, ASC_DELIM);
    if ((stat == EOF) || (stat == ELEMS_DONE))
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: error getting C index token");
        return FALSE;
    } // if

    if ((sscanf (token, "%d", &vindex)) != 1)
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: error reading C index");
        return FALSE;
    } // if

    // Validate vertex and copy to facet vertex list.

    if (vindex >= (int) lpObject->num_verts)
    {
        // Error.

        sprintf (gszErrMsg, "ReadFacet: C index too large");
        return FALSE;
    } // if

    lpObject->facets[findex].vertices[2] = vindex;

    // Increment vertex's count of facets which use it.

    lpObject->vertices[vindex].num_facets++;
    lpObject->vertices[vindex].facets[lpObject->vertices[vindex].num_facets - 1] = findex;

    lpObject->facets[findex].num_verts = 3;

    return TRUE;
} // ReadFacet


/******************************************************************************
 * OpenAscFile                                                                *
 *  Function: opens a .ASC file and stores file handle in fp                  *
 *    Inputs: filename - name of file to load                                 *
 *   Outputs: TRUE - file opened successfully                                 *
 *            FALSE - unable to open file                                     *
 ******************************************************************************/

BOOL OpenAscFile (char *filename)
{
    int len;

    // Return error if a .ASC file is currently open.

    if (fp)
    {
        sprintf (gszErrMsg, "Another .ASC file is currently open");
        return FALSE;
    } // if

    // Validate file extension.

    len = strlen (filename);
    if (_strnicmp (&filename[len-4], ".asc", 4) != 0)
    {
        sprintf (gszErrMsg, "Not a .ASC file");
        return FALSE;
    } // if

    // Open file.

    if (!(fp = fopen (filename, "r")))
    {
        sprintf (gszErrMsg, "could not open file %s", filename);
        return FALSE;
    } // if

    // Reset file line counter.

    line_num = 0;

    return TRUE;
} // OpenAscFile


/******************************************************************************
 * CloseAscFile                                                               *
 *  Function: closes .ASC file and invalidates file handle fp                 *
 *    Inputs: none                                                            *
 *   Outputs: none                                                            *
 ******************************************************************************/

void CloseAscFile (void)
{
    if (fp)
    {
        fclose (fp);
        fp = NULL;
    } // if
} // CloseAscFile


/******************************************************************************
 * SkipSpace                                                                  *
 *  Function: skips past whitespace in a file                                 *
 *    Inputs: none                                                            *
 *   Outputs: SUCCESS - successfully skipped past whitespace                  *
 *            EOF - reached end-of-file                                       *
 ******************************************************************************/

int SkipSpace (void)
{
    int c;

    while (TRUE)
    {
        c = getc (fp);
        if (c == EOF)
        {
            return (EOF);
        } // if

        if (c == '\n')
        {
            // Increment newline count.

            line_num++;
        }
        else if (c != ' ' && c != '\t')
        {
            // If we are here we also know that the char is not a '\n', so we
            // are at the start of a possibly valid string, so put character of
            // string back in input stream.

            ungetc (c, fp);
            return (SUCCESS);
        } // if
    } // while
} // SkipSpace


/******************************************************************************
 * GetAscLine                                                                 *
 *  Function: reads in one line from file                                     *
 *    Inputs: str - string to store retrieved line                            *
 *   Outputs: SUCCESS - line successfully read                                *
 *            EOF - reached end-of-file                                       *
 ******************************************************************************/

int GetAscLine (char *str)
{
    int c, count = 0;

    while (TRUE)
    {
        c = getc (fp);
        count++;

        if (c == EOF)
        {
            return (EOF);
        } // if

        if ((c != '\n') && (count < MAX_TOKEN_LENGTH))
        {
            *str++ = c;
        }
        else
        {
            *str++ = '\0';
            ungetc (c, fp);
            return (SUCCESS);
        } // if
    } // while
} // GetAscLine


/******************************************************************************
 * GetAscToken                                                                *
 *  Function: reads in one token from file                                    *
 *    Inputs: str - string to store retrieved token                           *
 *            delim - string containing set of token delimiters               *
 *   Outputs: SUCCESS - token successfully read                               *
 *            EOF - reached end-of-file                                       *
 *            ELEMS_DONE - no more tokens                                     *
 ******************************************************************************/

int GetAscToken (char *str, char *delim)
{
    int c, count = 0;

    // First skip any comments or blank lines that may preceed token, stat
    // will hold number of lines skipped. If we encounter an EOF here it just
    // means there are no more tokens, so return ELEMS_DONE.

    if (SkipSpace () == EOF)
    {
        return (ELEMS_DONE);
    } // if

    while (TRUE)
    {
        c = getc (fp);
        if (c == EOF)
        {
            return (EOF);
        }
        else
        {
            // If c is in set of delimiters terminate string and return...

            if (strchr (delim, c) != NULL)
            {
                *str++ = '\0';
                //ungetc (c, fp);
                return (SUCCESS);
            } // if

            // ...otherwise add character to token.

            *str++ = c;
            count++;

            // If token is at maximum length terminate string and return.

            if (count == MAX_TOKEN_LENGTH-1)
            {
                *str++ = '\0';
                return (SUCCESS);
            } // if
        } // if
    } // while
} // GetAscToken
