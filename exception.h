#ifndef UYA_EXCEPTION_H
#define UYA_EXCEPTION_H
/* Exact optional payload copied from the SM module loader. */
#define EXCEPTION_ADDRESS 0x000c8000
#define EXCEPTION_MEMORY_SIZE 0x6000
#define EXCEPTION_FILE_SIZE 10356
#define EXCEPTION_HASH 0x16676518U

#ifndef EXCEPTION_INSTALL
#define EXCEPTION_INSTALL() ((void (*)(void))EXCEPTION_ADDRESS)()
#endif
#endif
