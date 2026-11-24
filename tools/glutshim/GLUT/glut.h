/* Minimal GLUT API subset implemented by glutshim.c on Carbon windows + AGL, so GLUT samples run unmodified on a window we can place on a
 * chosen display (real GLUT clamps windows to the main display).  Only what the samples under test use. */
#ifndef GLUTSHIM_GLUT_H
#define GLUTSHIM_GLUT_H
#include <OpenGL/gl.h>
#define GLUT_RGBA 0
#define GLUT_DOUBLE 2
#define GLUT_DEPTH 16
#define GLUT_VISIBLE 1
#define GLUT_NOT_VISIBLE 0
#define GLUT_LEFT_BUTTON 0
#define GLUT_RIGHT_BUTTON 2
#define GLUT_BITMAP_HELVETICA_12 ((void *)7)
void glutInit(int *argc, char **argv);
void glutInitDisplayMode(unsigned int mode);
void glutInitDisplayString(const char *s);
void glutInitWindowSize(int w, int h);
void glutInitWindowPosition(int x, int y);
int  glutCreateWindow(const char *title);
void glutSetWindow(int id);
void glutDisplayFunc(void (*f)(void));
void glutReshapeFunc(void (*f)(int, int));
void glutKeyboardFunc(void (*f)(unsigned char, int, int));
void glutVisibilityFunc(void (*f)(int));
void glutIdleFunc(void (*f)(void));
void glutPostRedisplay(void);
void glutSwapBuffers(void);
void glutMainLoop(void);
int  glutCreateMenu(void (*f)(int));
void glutAddMenuEntry(const char *l, int v);
void glutAddSubMenu(const char *l, int m);
void glutAttachMenu(int b);
void glutBitmapCharacter(void *font, int c);
void glutGameModeString(const char *s);
int  glutEnterGameMode(void);
void glutLeaveGameMode(void);
#endif
