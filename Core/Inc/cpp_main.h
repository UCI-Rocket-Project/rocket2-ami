#ifndef CPP_MAIN_H // Prevents multiple inclusions of this header
#define CPP_MAIN_H

#ifdef __cplusplus
extern "C" { // Disables C++ name mangling so C code can link to this
#endif

// Bridge from C startup code into your C++ application
void cpp_main(void);

#ifdef __cplusplus
} // Closes extern "C" block
#endif

#endif // CPP_MAIN_HP