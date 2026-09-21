#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H
void diagnostics_start(void);
void diagnostics_owner(void);
int diagnostics_event(const char *event, const char *reason, unsigned long code);
int diagnostics_finish(const char *reason, int code);
void diagnostics_phase(long phase);
#endif
