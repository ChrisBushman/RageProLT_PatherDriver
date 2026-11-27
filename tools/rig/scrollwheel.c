/* scrollwheel.c x y n delay_us dy : warp the pointer to (x,y) and post n scroll-wheel events of dy lines, delay_us apart.  The target window must be under (x,y).
 * Build: cc -o scrollwheel scrollwheel.c -framework ApplicationServices */
#include <ApplicationServices/ApplicationServices.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    CGPoint p; int n, i, d, dy;
    if (argc < 6) return 1;
    p.x = atof(argv[1]); p.y = atof(argv[2]); n = atoi(argv[3]); d = atoi(argv[4]); dy = atoi(argv[5]);
    CGWarpMouseCursorPosition(p); usleep(200000);
    for (i = 0; i < n; i++) { CGPostScrollWheelEvent(1, (i / 40) % 2 ? dy : -dy); usleep(d); }   /* reverse direction every 40 events so a finite file never runs out */
    return 0;
}
