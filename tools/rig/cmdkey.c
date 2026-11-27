/* cmdkey.c keycode : press Command+key via CGPostKeyboardEvent (frontmost app).  Build: cc -o cmdkey cmdkey.c -framework ApplicationServices */
#include <ApplicationServices/ApplicationServices.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    int k = atoi(argv[1]);
    CGPostKeyboardEvent(0, 55, 1); usleep(50000);
    CGPostKeyboardEvent(0, k, 1); usleep(50000); CGPostKeyboardEvent(0, k, 0); usleep(50000);
    CGPostKeyboardEvent(0, 55, 0);
    return 0;
}
