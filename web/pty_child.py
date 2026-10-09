"""Small single-threaded launcher: acquire the PTY, then exec the C shell."""
import fcntl
import os
import sys
import termios
fcntl.ioctl(0, termios.TIOCSCTTY, 0)
os.execv(sys.argv[1], [sys.argv[1]])
