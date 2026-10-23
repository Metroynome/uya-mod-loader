#ifndef UYA_EXCEPTION_H
#define UYA_EXCEPTION_H

/* SM/Horizon payload relocated to 0x98000; drawing scratch is inside its reservation. */
#define EXCEPTION_ADDRESS     0x00098000
#define EXCEPTION_MEMORY_SIZE 0x6000
#define EXCEPTION_FILE_SIZE   10356
#define EXCEPTION_HASH        0x65ab33baU

#ifndef EXCEPTION_INSTALL
#define EXCEPTION_INSTALL() ((void (*)(void))EXCEPTION_ADDRESS)()
#endif
#endif
