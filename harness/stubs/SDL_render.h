#pragma once
#include <cstdint>
typedef uint8_t Uint8;
struct SDL_Renderer; struct SDL_Texture;
struct SDL_Rect { int x,y,w,h; };
struct SDL_FRect { float x,y,w,h; };
struct SDL_FPoint { float x,y; };
enum SDL_RendererFlip { SDL_FLIP_NONE=0, SDL_FLIP_HORIZONTAL=1, SDL_FLIP_VERTICAL=2 };
int SDL_RenderCopyF(SDL_Renderer*,SDL_Texture*,const SDL_Rect*,const SDL_FRect*);
int SDL_RenderCopyExF(SDL_Renderer*,SDL_Texture*,const SDL_Rect*,const SDL_FRect*,double,const SDL_FPoint*,SDL_RendererFlip);
int SDL_RenderFillRect(SDL_Renderer*,const SDL_Rect*);
int SDL_RenderFillRects(SDL_Renderer*,const SDL_Rect*,int);
int SDL_RenderSetClipRect(SDL_Renderer*,const SDL_Rect*);
int SDL_GetRenderDrawColor(SDL_Renderer*,Uint8*,Uint8*,Uint8*,Uint8*);
int SDL_SetRenderDrawColor(SDL_Renderer*,Uint8,Uint8,Uint8,Uint8);
