#include <stdio.h>
#include <stdlib.h>
//atexit
#include <unistd.h> 
//read
#include <termios.h> 
//struct termios,tcgetattr(),tcsetattr(),ECHO,TCSAFLUSH,ICANON,ISIG,IXON,IEXTEN,ICRNL,OPOST,BRKINT,INPCK,ISTRIP,VMIN,VTIME
#include <ctype.h>
//iscntrl
#include <errno.h>
//EAGAIN 


/*** data***/

struct termios orig_termios;


/*** terminal ***/

//error handling
void die(const char*s){
    perror(s);
    exit(1);
}

//disable raw mode at exit
void disableRawMode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcsetattr");
}

//trun off echoing
void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");

    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP ); //disable Ctrl-s/Ctrl-q
    raw.c_oflag &= ~(OPOST); //trun off all output processing features
    raw.c_cflag |= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); //trun off echoing and canonical mode and SIGINT(Ctrl-s)/SIGTSTP(Ctrl-z) signals and disable (Ctrl-v)
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/*** init ***/

int main() {
    enableRawMode();

    //read keypresses from the user
    while (1){
        char c = '\0';
        if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
        if(iscntrl(c)){ //is character a control character？
            printf("%d\r\n", c);
        }else{
            printf("%d ('%c')\r\n",c,c); //start a new line
        }
        if(c == 'q') break;
    }

  return 0;
}