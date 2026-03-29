#ifndef SDL_error_h_
#define SDL_error_h_

#ifdef __cplusplus
extern "C" {
#endif


const char* SDL_GetError(void);
int SDL_SetError(const char *fmt, ...);
void SDL_ClearError(void);


#ifdef __cplusplus
}
#endif

#endif
