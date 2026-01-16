# PVM
PACKL Virtual Machine, a simple machine written in C

# Example

Below is a very simple code snippet in pasm (PACKL Assembler) that prints "Hello World" to the screen


```asm
#use "std.pasm"

#const msg "Hello World" end 

#main:	
	println $msg
	ret 

#entry: $main
```

# Note
I wanna focus on re-factoring the code and the logic of this vm, so i can extend its features more and more.