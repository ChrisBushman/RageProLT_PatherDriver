/* gltest: print renderer/version, draw a red triangle on blue, read pixels back, exit 0 if centre is red. */
#include <GLUT/glut.h>
#include <stdio.h>
#include <stdlib.h>
static int frames;
static void disp(void)
{
    unsigned char px[4] = {0,0,0,0}, bg[4] = {0,0,0,0};
    glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1,1,-1,1,-1,1);
    glBegin(GL_TRIANGLES); glColor3f(1,0,0); glVertex2f(-0.8f,-0.8f); glVertex2f(0.8f,-0.8f); glVertex2f(0,0.8f); glEnd();
    glFinish();
    glReadPixels(128,100,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    glReadPixels(5,250,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bg);
    glutSwapBuffers();
    if (++frames == 3) {
        printf("GL_VENDOR   : %s\nGL_RENDERER : %s\nGL_VERSION  : %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
        printf("centre px   : %u %u %u %u (want 255 0 0)\nbackdrop px : %u %u %u %u (want 0 0 255)\n", px[0],px[1],px[2],px[3], bg[0],bg[1],bg[2],bg[3]);
        fflush(stdout);
        exit(px[0] > 200 && px[2] < 50 && bg[2] > 200 ? 0 : 2);
    }
    glutPostRedisplay();
}
int main(int argc, char **argv)
{
    glutInit(&argc, argv); glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(256,256); glutCreateWindow("gltest"); glutDisplayFunc(disp); glutMainLoop(); return 0;
}
