nasm -f elf assad.asm -o assad.o
gcc -no-pie -m32 assad.o -o assad
./assad