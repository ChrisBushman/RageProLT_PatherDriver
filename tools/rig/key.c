#include <ApplicationServices/ApplicationServices.h>
#include <stdlib.h>
#include <unistd.h>
int main(int c, char **v){ int i; for(i=1;i<c;i++){ int k=atoi(v[i]); CGPostKeyboardEvent(0,k,1); usleep(80000); CGPostKeyboardEvent(0,k,0); usleep(400000);} return 0; }
