#ifndef SAFE_OUTPUT_H
#define SAFE_OUTPUT_H

#include <signal.h>
#include <stdio.h>

// Affichage sûr en présence de readline() + est THREAD_SAFE

extern volatile sig_atomic_t in_console;

void safe_output_init(void);
void safe_output_destroy(void);
void safe_printf(const char *format, ...);

void spf_priority_lock();
void spf_priority_unlock();
void spf_priority(const char *format, ...);

#endif
