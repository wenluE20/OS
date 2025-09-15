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
    int child_pid;
    
    if ((child_pid = fork()) == 0) { // 子进程
        close(f2c[1]);  // 关闭父进程写入端
        close(c2f[0]);  // 关闭子进程读取端
        
        // 从父进程读取数据和父进程PID
        int parent_pid;
        read(f2c[0], &parent_pid, sizeof(parent_pid));
        printf("%d: received ping from pid %d\n", getpid(), parent_pid);
        
        // 写入到父进程
        write(c2f[1], "p", 1);
        
        close(f2c[0]);
        close(c2f[1]);
        exit(0);
    } else {
        // 父进程
        close(f2c[0]);  // 关闭父进程读取端
        close(c2f[1]);  // 关闭子进程写入端
        
        // 写入父进程PID到子进程
        int my_pid = getpid();
        write(f2c[1], &my_pid, sizeof(my_pid));
        
        // 从子进程读取
        read(c2f[0], buf, 1);
        printf("%d: received pong from pid %d\n", getpid(), child_pid);
        
        close(f2c[1]);
        close(c2f[0]);
        wait(0); // 等待子进程结束
        exit(0);
    }
    
    return 0;
}