#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    // create two channels
    // c2f: son to the father
    // f2c: father to the son
    int c2f[2];
    int f2c[2];
    
    pipe(c2f);
    pipe(f2c);
    
    char buf[1];
    
    if (fork() == 0) {//son
        close(f2c[1]);  // close the father until the son's writing
        close(c2f[0]);  // close the son until the father's reading
        
        // read from the father
        read(f2c[0], buf, 1);
        printf("%d: received ping from pid %d\n", getpid(), getpid());
        
        // write into the father
        write(c2f[1], "p", 1);
        
        close(f2c[0]);
        close(c2f[1]);
        exit(0);
    } else {
        // father
        close(f2c[0]);  // close the father until the son's reading
        close(c2f[1]);  // close the son until the father's writing
        
        // writing into the son
        write(f2c[1], "p", 1);
        
        // reading  from the son
        read(c2f[0], buf, 1);
        printf("%d: received pong from pid %d\n", getpid(), getpid() - 1); 
        
        close(f2c[1]);
        close(c2f[0]);
        wait(0); // waiting for the son finnish
        exit(0);
    }
    
    return 0;
}