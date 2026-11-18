/******************************************************************************
 *  ATI 3D RAGE SDK sample code                                               *
 *                                                                            *
 *  Texture Compositing Example based on ex2.cpp (Example 2). A single cube   *
 *	spinning while it is changing between two textures two textures.		  *
 *																			  *
 *                                                                            *
 *  Copyright (c) 1995-1996 ATI Technologies Inc.  All rights reserved.       *
 ******************************************************************************/

#define NAME "RAGE SDK VQ Texture Cube"
#define TITLE "3D RAGE PRO Example 3"

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <ddraw.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <direct.h>
#include "resource.h"
#include "ati3dcif.h"
#include "types.h"
#include "exutil.h"
#include "glob.h"
#include "geom.h"
#include "vqlib.h"

#define PI          3.141593f
#define PI_x_2      6.2831853f
#define MAXSCALER   128
#define MINSCALER   1

#define MANDRILL_VQT        "..\\bmpetc\\mandrill.vqt"
#define STATUSBAR_BMP       "..\\bmpetc\\vqstat_tc.bmp"

void CloseApp (void);
void InitView (void);
BOOL InitObject (void);
void updateFrame (void);
void DrawFrame (void);
BOOL InitStatusBar (void);
static void rpx4dbg(const char *s);

extern BOOL   gbFive6Five;
extern VIEW   gView;
extern MATRIX gViewport;
extern float gWidth;
extern float gHeight;
extern float gHeight;
extern float gXScreenOffset;
DWORD gdwWidth = 640L;
DWORD gdwHeight = 480L;
DWORD gdwFillColor = 0x4210L;

HWND        ghWnd;              // window handle

// Frame update enable flag.
BOOL    gbUpdateFrame = FALSE;
BOOL    gbDisplayFrameRate = FALSE;

// Texture structure for textures.
VQTEXTURE gVQTex = {0};

// Object model structures for cube
OBJECT  gObj = {0};

// Rotation angles for cubes.
float   gxtheta2 = 1.0f;
float   gytheta2 = 0.9f;
float   gztheta2 = -0.1f;

float   gScaler = 0.50f;

RECT    grctStatusBar;

// Cube transformation matrices.
MATRIX  gmat;

LPDIRECTDRAWSURFACE glpDDSStatusBar = NULL; // Status bar surface

// Cube model coordinates for vertices.
VECT vlist[] = {
    {-1.0f, -1.0f,  1.0f, 1.0f},
    {1.0f, -1.0f,  1.0f, 1.0f},
    {1.0f, -1.0f, -1.0f, 1.0f},
    {-1.0f, -1.0f, -1.0f, 1.0f},

    {-1.0f, -1.0f,  1.0f, 1.0f},
    {1.0f, -1.0f,  1.0f, 1.0f},
    {1.0f,  1.0f,  1.0f, 1.0f},
    {-1.0f,  1.0f,  1.0f, 1.0f},

    {1.0f, -1.0f,  1.0f, 1.0f},
    {1.0f, -1.0f, -1.0f, 1.0f},
    {1.0f,  1.0f, -1.0f, 1.0f},
    {1.0f,  1.0f,  1.0f, 1.0f},

    {1.0f, -1.0f, -1.0f, 1.0f},
    {-1.0f, -1.0f, -1.0f, 1.0f},
    {-1.0f,  1.0f, -1.0f, 1.0f},
    {1.0f,  1.0f, -1.0f, 1.0f},

    {-1.0f, -1.0f, -1.0f, 1.0f},
    {-1.0f, -1.0f,  1.0f, 1.0f},
    {-1.0f,  1.0f,  1.0f, 1.0f},
    {-1.0f,  1.0f, -1.0f, 1.0f},

    {-1.0f,  1.0f,  1.0f, 1.0f},
    {1.0f,  1.0f,  1.0f, 1.0f},
    {1.0f,  1.0f, -1.0f, 1.0f},
    {-1.0f,  1.0f, -1.0f, 1.0f}};

// Count of facet including the vertex for each vertex.
int numfacets[] = {
    2, 1, 2, 1, 2, 1,
    2, 1, 2, 1, 2, 1,
    2, 1, 2, 1, 2, 1,
    2, 1, 2, 1, 2, 1};

// Index of facets which include the vertex for each vertex.
C3D_UINT16 facetlist[24][MAX_FACET_PER_VERTEX] = {
    {0, 1, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {2, 3, 0, 0, 0, 0, 0, 0},
    {2, 0, 0, 0, 0, 0, 0, 0},

    {2, 3, 0, 0, 0, 0, 0, 0},
    {3, 0, 0, 0, 0, 0, 0, 0},
    {4, 5, 0, 0, 0, 0, 0, 0},
    {4, 0, 0, 0, 0, 0, 0, 0},
    {4, 5, 0, 0, 0, 0, 0, 0},
    {5, 0, 0, 0, 0, 0, 0, 0},

    {6, 7, 0, 0, 0, 0, 0, 0},
    {6, 0, 0, 0, 0, 0, 0, 0},
    {6, 7, 0, 0, 0, 0, 0, 0},
    {7, 0, 0, 0, 0, 0, 0, 0},
    {8, 9, 0, 0, 0, 0, 0, 0},
    {8, 0, 0, 0, 0, 0, 0, 0},

    {8, 9, 0, 0, 0, 0, 0, 0},
    {9, 0, 0, 0, 0, 0, 0, 0},
    {10, 11, 0, 0, 0, 0, 0, 0},
    {10, 0, 0, 0, 0, 0, 0, 0},
    {10, 11, 0, 0, 0, 0, 0, 0},
    {11, 0, 0, 0, 0, 0, 0, 0}};

// Index of vertices for each facet.
C3D_UINT16 vertexlist[12][MAX_VERTEX_PER_FACET] = {
    {3, 2, 0, 0},
    {2, 1, 0, 0},
    {5, 6, 4, 0},
    {6, 7, 4, 0},
    {9, 10, 8, 0},
    {10, 11, 8, 0},
    {13, 14, 12, 0},
    {14, 15, 12, 0},
    {17, 18, 16, 0},
    {18, 19, 16, 0},
    {21, 22, 20, 0},
    {22, 23, 20, 0}};


/******************************************************************************
 * WindowProc                                                                 *
 *  Function: main window message handling procedure                          *
 *    Inputs: hWnd - handle of window                                         *
 *            message - message to window                                     *
 *            wParam - WORD length parameter for message (varies)             *
 *            lParam - DWORD length parameter for message (varies)            *
 *   Outputs: varies                                                          *
 ******************************************************************************/

long FAR PASCAL WindowProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CREATE:
            break;

        case WM_SETCURSOR:
            SetCursor (NULL);
            return TRUE;

        case WM_KEYDOWN:
            switch (wParam)
            {
                case VK_ESCAPE:
                case VK_F12:

                    // Disable frame update in WinMain.
                    gbUpdateFrame = FALSE;
                    PostMessage (hWnd, WM_CLOSE, 0, 0);
                    break;

				case VK_ADD:
                    if (gScaler < MAXSCALER) gScaler++;
                    break;

                case VK_SUBTRACT:
                    if (gScaler > MINSCALER) gScaler--;
                    break;
	        } 
            break;

	    case WM_DESTROY:
            CloseApp ();
            PostQuitMessage (0);
            break;
    } 

    return DefWindowProc (hWnd, message, wParam, lParam);
}  


/******************************************************************************
 * InitApp                                                                    *
 *  Function: do work required for every instance of the application:         *
 *            create the window, initialize data                              *
 *    Inputs: hInstance - instance handle of application                      *
 *            nCmdShow - current visibility state                             *
 *   Outputs: TRUE - initialization was successful                            *
 *            FALSE - initialization failed                                   *
 ******************************************************************************/

static BOOL InitApp (HANDLE hInstance, int nCmdShow)
{
    HWND            hwnd;
    WNDCLASS        wc;
    char            cExePath[_MAX_PATH], cExeDir[_MAX_DIR];
	BOOL			bTexEn = TRUE;;
	BOOL			bTexCompEn = TRUE;;

    // Make sure we are in the right directory so relative paths will behave.
	GetModuleFileName (hInstance, cExePath, _MAX_PATH);
    _splitpath (cExePath, NULL, cExeDir, NULL, NULL);
    _chdir (cExeDir);

    // Set up and register window class.
	wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon (hInstance, IDI_APPLICATION);
    wc.hCursor = LoadCursor (NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NAME;
    wc.lpszClassName = NAME;
    RegisterClass (&wc);

    // Create a window.
	ghWnd = hwnd = CreateWindowEx (WS_EX_TOPMOST, NAME, TITLE, WS_POPUP, 0, 0,
                                   GetSystemMetrics (SM_CXSCREEN),
                                   GetSystemMetrics (SM_CYSCREEN), NULL, NULL,
                                   hInstance, NULL);

    if (!hwnd)
    {
        return FALSE;
    }

    ShowWindow (hwnd, nCmdShow);
    UpdateWindow (hwnd);

    // Create and initialize the main DirectDraw object.
	if (!InitDirectDraw (hwnd, gdwWidth, gdwHeight, 16L))
    {
        CloseApp ();
        MessageBox (hwnd, gszErrMsg, TITLE, MB_OK);
        DestroyWindow (hwnd);
        return FALSE;
    }

    // Create an ATI CIF rendering context.
	if (!InitATI3DCIF ())
    {
        CloseApp ();
        MessageBox (hwnd, gszErrMsg, TITLE, MB_OK);
        DestroyWindow (hwnd);
        return FALSE;
    } 

    // Load the VQ texture.
	if (!LoadVQTexture (MANDRILL_VQT, &gVQTex))
    {
        CloseApp ();
        MessageBox (hwnd, gszErrMsg, TITLE, MB_OK);
        DestroyWindow (hwnd);
        return FALSE;
    } 
    
	// Enable texture mapping 
	ATI3DCIF_ContextSetState (ghRC, C3D_ERS_TMAP_EN, &bTexEn);
	
  	// Initialize the view parameters.
    InitView ();

    // Initialize the cube object description.
    if (!InitObject ())
    {
        CloseApp ();
        MessageBox (hwnd, gszErrMsg, TITLE, MB_OK);
        DestroyWindow (hwnd);
        return FALSE;
    }

    ScaleMatrix (3.0f, 3.0f, 3.0f, gmat);
    UpdateObject (&gObj, gmat);
    TexSetup (&gObj, gmat);

	// Load status bar bitmaps and display status bar.
    if (!InitStatusBar ())
    {
        CloseApp ();
        MessageBox (hwnd, gszErrMsg, TITLE, MB_OK);
        DestroyWindow (hwnd);
        return FALSE;
    } 

    // Modify the background color if the displaymode 16bpp format is RGB565.
	if (gbFive6Five)
        gdwFillColor = 0x8410L;
	 
    // Enable flag to update object geometry.
    gbUpdateFrame = TRUE;

    return TRUE;
} 


/******************************************************************************
 * WinMain                                                                    *
 *  Function: initialization, message loop                                    *
 *    Inputs: hInstance - handle for this instance of application             *
 *            hPrevInstance - handle for previous instance of application     *
 *            lpCmdLine - string containing remainder of commmand line        *
 *            nCmdShow - current visibility state                             *
 *   Outputs: varies                                                          *
 ******************************************************************************/

int PASCAL WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpCmdLine, int nCmdShow)
{
    MSG msg;

    lpCmdLine = lpCmdLine;
    hPrevInstance = hPrevInstance;

    if (!InitApp (hInstance, nCmdShow))
		return FALSE;

    while (TRUE)
    {
        if (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
        {
            if (!GetMessage (&msg, NULL, 0, 0))
            {
                return msg.wParam;
            }
            TranslateMessage (&msg);
            DispatchMessage (&msg);
        } 

        // Update object geomtery.
        if (gbUpdateFrame) DrawFrame ();
	} 
}


/******************************************************************************
 * InitView                                                                   *
 *  Function: initialize the camera view parameters                           *
 *    Inputs: none                                                            *
 *   Outputs: none                                                            *
 ******************************************************************************/

void InitView (void)
{
    // Location parameters.
    gView.camera.location.x = 0.0f;
    gView.camera.location.y = 0.0f;
    gView.camera.location.z = 20.0f;
    gView.camera.upvector.x = 0.0f;
    gView.camera.upvector.y = 1.0f;
    gView.camera.upvector.z = 0.0f;
    gView.camera.lookat.x = 0.0f;
    gView.camera.lookat.y = 0.0f;
    gView.camera.lookat.z = 0.0f;

    // Frustum parameters.
	gView.frustum.left = -3.0f;
    gView.frustum.bottom = -3.0f;
    gView.frustum.front = 3.0f;
    gView.frustum.right = 3.0f;
    gView.frustum.top = 3.0f;
    gView.frustum.back = 40.0f;
    gView.frustum.dist = 5.0f;

    // Viewport parameters.
	gView.viewport.left = 0.0f;
    gView.viewport.top = 0.0f;

    // Set viewport to a square defined by the maximum height.
	gView.viewport.right = gHeight;
    gView.viewport.bottom = gHeight;

    // Initialize view matrices in gView matrix.
    CalcViewParams ();
}


/******************************************************************************
 * InitObject                                                                 *
 *  Function: initialize the 3D objects' model data                           *
 *    Inputs: none                                                            *
 *   Outputs: TRUE - objects successfully initialized                         *
 *            FALSE - unable to initialize objects                            *
 ******************************************************************************/

BOOL InitObject (void)
{
    int i, j;
	
    // Initialize object 2.
    gObj.num_verts = 24;
    gObj.num_facets = 12;
    gObj.vertices = (PVERTEX) HeapAlloc (GetProcessHeap (), HEAP_ZERO_MEMORY,
                                          gObj.num_verts * sizeof (VERTEX));
    if (!gObj.vertices)
    {
        wsprintf (gszErrMsg, "gObj: Could not allocate memory for vertices");
        return FALSE;
    }

    gObj.facets = (PFACET) HeapAlloc (GetProcessHeap (), HEAP_ZERO_MEMORY,
                                       gObj.num_verts * sizeof (FACET));
    if (!gObj.facets)
    {
        wsprintf (gszErrMsg, "gObj: Could not allocate memory for facets");
        return FALSE;
    }

    gObj.facetlist = (PFACET*) HeapAlloc (GetProcessHeap (), HEAP_ZERO_MEMORY,
                                           gObj.num_facets * sizeof (PFACET));
    if (!gObj.facetlist)
    {
        wsprintf (gszErrMsg, "gObj: Could not allocate memory for facet list");
        return FALSE;
    }

    gObj.vlstPrimList = (C3D_VLIST) HeapAlloc (GetProcessHeap (), HEAP_ZERO_MEMORY,
                                                gObj.num_facets * sizeof (C3D_PVTCF) * 3);
    if (!gObj.vlstPrimList)
    {
        wsprintf (gszErrMsg, "gObj: Could not allocate memory for primitive list");
        return FALSE;
    }
    gObj.nNumPrimListVerts = 0;

    for (i = 0; i < (int) gObj.num_verts; i++)
    {
        gObj.vertices[i].model_coords[0] = vlist[i][0];
        gObj.vertices[i].model_coords[1] = vlist[i][1];
        gObj.vertices[i].model_coords[2] = vlist[i][2];
        gObj.vertices[i].model_coords[3] = vlist[i][3];
        gObj.vertices[i].num_facets = numfacets[i];
        for (j = 0; j < MAX_FACET_PER_VERTEX; j++)
        {
            gObj.vertices[i].facets[j] = facetlist[i][j];
        } 
        gObj.vertices[i].field_mask = 5;
    } 

    for (i = 0; i < (int) gObj.num_facets; i++)
    {
        gObj.facets[i].num_verts = 3;
        for (j = 0; j < MAX_VERTEX_PER_FACET; j++)
        {
            gObj.facets[i].vertices[j] = vertexlist[i][j];
        } 
    } 

    if (!gObj.facets)
    {
        wsprintf (gszErrMsg, "gObj3: Could not allocate memory for facets");
        return FALSE;
    }

    return TRUE;
} 


/******************************************************************************
 * CloseApp                                                                   *
 *  Function: close application components and clean up                       *
 *    Inputs: none                                                            *
 *   Outputs: none                                                            *
 ******************************************************************************/

void CloseApp (void)
{
    // Unload first texture.
	if (gVQTex.lpDDSTex[0] || gVQTex.hTX)
    {
        if (!UnloadVQTexture (&gVQTex))
        {
            MessageBox (ghWnd, gszErrMsg, TITLE, MB_OK);
        } 
    } 

	// Release surface.
	if (glpDDSStatusBar)
    {
        glpDDSStatusBar->Release ();
        glpDDSStatusBar = NULL;
    } 

    // Destroy the ATI 3D rendering context and close the 3D Driver.
	CloseATI3DCIF ();

    // Destroy the DirectDraw object.
	CloseDirectDraw ();

    // Free object 2 buffers.
	if (gObj.vertices) HeapFree (GetProcessHeap (), 0, gObj.vertices);
	if (gObj.facets) HeapFree (GetProcessHeap (), 0, gObj.facets);
    if (gObj.facetlist) HeapFree (GetProcessHeap (), 0, gObj.facetlist);
    if (gObj.vlstPrimList) HeapFree (GetProcessHeap (), 0, gObj.vlstPrimList);
}


/******************************************************************************
 * updateFrame                                                                *
 *  Function: update geometry transformation for each object                  *
 *    Inputs: none                                                            *
 *   Outputs: none                                                            *
 ******************************************************************************/

void updateFrame (void)
{
    MATRIX mat, tmp, xrot, yrot, zrot;
    static float tx2 = 1.0f;
    static float ty2 = 1.0f;
    float sinrate2;
    float tz2;
    static float tx3 = -5.5f;
    static float ty3 = -4.0f;
    static float tx4 = 7.0f;
    static float ty4 = -4.0f;
    float orbitradius = 9.0f;
    static float orbitangle = 0.0f;
    static float oscillrate = 0.0f;
	static unsigned int delayMorphCount = 0;
	static int blendDirection = 1;
    float oscillamp = 10.0f;

    // Object 2.
    gxtheta2 += (0.05f * gScaler);

    // Increment oscillation angle.
    oscillrate += (0.0075f * gScaler);

    if (gxtheta2 > PI) gxtheta2 -= PI_x_2;
    if (gxtheta2 < -PI) gxtheta2 += PI_x_2;
    if (oscillrate > PI) oscillrate -= PI_x_2;
    if (oscillrate < -PI) oscillrate += PI_x_2;

    // Compute z axis translation component.
    // z translation = oscillation amplitude x (1 - sin (oscillation angle)

    sinrate2 = (float) sin (oscillrate);
    tz2 = - oscillamp + (oscillamp * sinrate2);

    // Rotation matrices for object 2.
	XRotateMatrix (gxtheta2, xrot);
    YRotateMatrix (gytheta2, yrot);
    ZRotateMatrix (gztheta2, zrot);
    MultMatrix (xrot , gmat, mat);
    MultMatrix (yrot , mat, tmp);
    MultMatrix (zrot , tmp, mat);
    TranslateMatrix (tx2, ty2, tz2, tmp);

    // Multiply compose rotation matrix by translation matrix.
	MultMatrix (tmp, mat, xrot);

    // Transform object.
	rpx4dbg("before UpdateObject\r\n");
	UpdateObject (&gObj, xrot);
	rpx4dbg("after UpdateObject\r\n");
}


/******************************************************************************
 * DrawFrame                                                                  *
 *  Function: draw the animation frame and flip the double buffers            *
 *    Inputs: none                                                            *
 *   Outputs: none                                                            *
 ******************************************************************************/

static void rpx4dbg(const char *s)
{
    HANDLE h = CreateFileA("C:\\rpx4.log", GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w; SetFilePointer(h, 0, NULL, FILE_END);
        WriteFile(h, s, lstrlenA(s), &w, NULL); CloseHandle(h);
    }
}

void DrawFrame (void)
{
    HRESULT ddretval;
    C3D_EC ecRend;
    DDBLTFX ddbltfx;

    // Color fill the back surface with gray color close to fog color.
	ZeroMemory (&ddbltfx, sizeof (ddbltfx));
    ddbltfx.dwSize = sizeof (ddbltfx);
    ddbltfx.dwFillColor = gdwFillColor;
    glpDDSBack->Blt (NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfx);

    // Get the pointer to the drawing surface.
	DDSURFACEDESC ddsd;
    ddsd.dwSize = sizeof (ddsd);
    ddretval = glpDDSBack->Lock (NULL, &ddsd,
                                 DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
    if (ddretval == DDERR_SURFACELOST) glpDDSBack->Restore ();

    // Unlock the drawing surface.
	ddretval = glpDDSBack->Unlock (NULL);
    if (ddretval != DD_OK) return;

    // Switch to 3D rendering mode.
	if ((ecRend = ATI3DCIF_RenderBegin (ghRC)) != C3D_EC_OK) return;

    // Set the pointer to the drawing surface.
	ATI3DCIF_ContextSetState (ghRC, C3D_ERS_SURF_DRAW_PTR,
                              (C3D_PRSDATA) &(ddsd.lpSurface));

	// Select the MANDRILL texture.
	ATI3DCIF_ContextSetState (ghRC, C3D_ERS_TMAP_SELECT, &(gVQTex.hTX));

	// Select the filtering mode used for VQ texture 
	C3D_ETEXFILTER etf = C3D_ETFILT_MIN2BY2_MAG2BY2;
	ATI3DCIF_ContextSetState (ghRC, C3D_ERS_TMAP_FILTER, &etf);
	
    // Draw the primitive list.
	ecRend = ATI3DCIF_RenderPrimList (gObj.vlstPrimList, gObj.nNumPrimListVerts);

    // End the primitive operations.
    ecRend = ATI3DCIF_RenderEnd ();
    rpx4dbg("after RenderEnd\r\n");

	// Update the status bar.
    ddretval = glpDDSBack->BltFast (0, 480 - grctStatusBar.bottom, glpDDSStatusBar, &grctStatusBar, DDBLTFAST_WAIT);
    rpx4dbg("after BltFast\r\n");
    if (ddretval == DDERR_SURFACELOST)
		glpDDSBack->Restore ();

    // Flip the primary and back surfaces.
    PageFlip (glpDDSPrimary);
    rpx4dbg("after PageFlip\r\n");

    // Update the objects for the next frame.
    updateFrame ();
} 

/******************************************************************************
 * InitStatusBar                                                              *
 *  Function: initialize status bar                                           *
 *    Inputs: none                                                            *
 *   Outputs: TRUE - initialization was successful                            *
 *            FALSE - initialization failed                                   *
 ******************************************************************************/

BOOL InitStatusBar (void)
{
    HDC             hdc;
    HDC             hdcImage;
    DDSURFACEDESC   ddsd;
    HRESULT         ddretval;
    HBITMAP         hbm;
    BITMAP          bm;
 
    // Load the status bar bitmap and copy it onto the background bitmap.
	hbm = (HBITMAP) LoadImage (NULL, STATUSBAR_BMP, IMAGE_BITMAP, 0, 0,
                               LR_LOADFROMFILE | LR_CREATEDIBSECTION);

    if (!hbm)
    {
        wsprintf (gszErrMsg, "Could not load statusbar bitmap");
        return FALSE;
    }

    // Get bitmap dimensions.
	GetObject (hbm, sizeof (bm), &bm);

    // Create a plain offscreen surface for status bar based on bitmap dimensions.
	ZeroMemory (&ddsd, sizeof (ddsd));
    ddsd.dwSize = sizeof (ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    ddsd.dwWidth = bm.bmWidth;
    ddsd.dwHeight = bm.bmHeight;
    ddretval = glpDD->CreateSurface (&ddsd, &glpDDSStatusBar, NULL);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not create offscreen plain surface");
        DeleteObject (hbm);
        return FALSE;
    }

    // Init source rectangle for BltFast function.
	grctStatusBar.top = 0;
    grctStatusBar.left = 0;
    grctStatusBar.right = bm.bmWidth;
    grctStatusBar.bottom = bm.bmHeight;

    // Copy the bitmap to the offscreen surface.
	hdcImage = CreateCompatibleDC (NULL);
    SelectObject (hdcImage, hbm);

    if (glpDDSStatusBar->GetDC (&hdc) == DD_OK)
    {
        StretchBlt (hdc, 0, 0, bm.bmWidth, bm.bmHeight, hdcImage, 0, 0,
                    bm.bmWidth, bm.bmHeight, SRCCOPY);
        glpDDSStatusBar->ReleaseDC (hdc);
    } 

    // Clean up.
    DeleteDC (hdcImage);
    DeleteObject (hbm);

    return TRUE;
}

